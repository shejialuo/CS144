# Lab 2

First, you should read carefully read the docs provided by the lab.

> In addition to writing to the incoming stream, the `TCPReceiver` is responsible for telling the sender two things:
>
> 1. the index of the "first unassembled" byte which is called the "acknowledgment number".
> 2. the distance between the "first unassembled" index and the "first unacceptable" index. This is called the "window size "

For these requirements, we have already done in the lab 0 and lab1.

## Translating between 64-bit indexes and 32-bit seqnos

In `StreamReassembler`, each individual datagram has a 64-bit *stream index* and a 64-bit index is big enough that we can treat it as never overflowing. In the TCP headers, we only have a 32-bit sequence number. So the requirement is below:

+ *Your implementation needs to plan for 32-bit integers to wrap around*.
+ *TCP sequence numbers start at a random value*.
+ *The logical beginning and ending each occupy one sequence number*

The public interface is below:

+ `WrappingInt32 wrap(uint64_t n, WrappingInt32 isn)`: give an absolute sequence number and an Initial Sequence Number, produce the relative sequence number for `n`.
+ `uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint)`: Given a sequence number, the Initial Sequence Number, and an absolute checkpoint sequence number, compute the absolute sequence number that corresponds to `n` that is closes to the checkpoint.

For `wrap`, it may seem that we need to round the value. However, the addition of the two unsigned integer would automatically overflow, we could use this feature to gracefully deal with this problem.

```c++
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // Because adding unsigned integer would automatically overflow
    // thus we can utilize this feature to gracefully deal with this problem.
    return isn + static_cast<uint32_t>(n);
}
```

For `unwrap`, we need to find the closest, we just use the overflow of the unsigned again.

```c++
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint32_t offset = n.raw_value() - wrap(checkpoint, isn).raw_value();
    uint64_t result = checkpoint + offset;
    if (offset > (1u << 31) && result >= (1ul << 32))
        result -= (1ul << 32);
    return result;
}
```

## TCP receiver

The public interface for `TCPReceiver` class is:

+ `TCPReceiver(const size_t capacity)`: Construct a `TCPReceiver` that will store up to `capacity` bytes.
+ `void segment_received(const TCPSegment &seg)`: Handle an inbound TCP segment.
+ `sdt::optional<WrappingInt32> ackno() const`: The acknowledgment number should be sent to the peer.
+ `size_t window_size() const`: The window size that should be sent to the peer.
+ `size_t unassembled_bytes() const`: number of bytes stored but not yet reassembled.
+ `ByteStream &stream_out()`: Access the reassembled byte stream.

Well, we need to think what data structure we need when receiving the TCP datagram. First, we need to record the sender's Initial Sequence Number `_sender_isn`. Because, we need this to use `wrap` to calculate the absolute acknowledge number `_ack`. We just need this two:

```c++
class TCPReceiver {
    //! Our data structure for re-assembling bytes.
    StreamReassembler _reassembler;
    //! The maximum number of bytes we'll store.
    size_t _capacity;
    std::optional<WrappingInt32> _sender_isn{};  //! The initial sequence number from the sender
    std::optional<WrappingInt32> _ack{};         //! The acknowledge number
}
```

From the perspective of receiver, connection is easy. We will receive two datagrams from the sender. The first datagram is important because the `SYN` bit is set to 1. So we need to handle this case.

```c++
void TCPReceiver::segment_received(const TCPSegment &seg) {
    // Here, we should ensure we set the initial sequence number
    // when we first time receive the SYN.
    if (seg.header().syn) {
        _sender_isn.emplace(seg.header().seqno);
    }
}
```

When we receive the `syn`, we just set the `_sender_isn` to the `seg.header().seqno`. You may wonder why we not set the `_ack` to be the `_sender_sin.value() + 1`. The reason is that:

> Note that the SYN flag is just one flag in the header. The same segment could also carry data and could even have the FIN flag set.

Before transferring the data, we need to understand how to get the window size, actually, it is super easy. Remember what we have done in the lab 0, yes, there is a method called `remaining_capacity` which means that the ringBuffer remain size, we use this as the window size. So you could know what we use `stream_out().bytes_written()` as the acknowledge number.

```c++
size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
optional<WrappingInt32> TCPReceiver::ackno() const { return _ack; }
```

However, this is not the reality. Here, I assume all the operation is not async. However, in kernel, things would be much more complicate.

So, at now it is easy for us to implement transferring the data. However, there is a corner case, the process of three-way handshake, the ACK should increment, so I use a *stupid* way to substitute the `_sender_isn` and `_ack`. (Please see the comment for more descriptive explanation).

And how should we close the TCP connection. We just add 1 to `_ack`.

```c++
void TCPReceiver::segment_received(const TCPSegment &seg) {
    ...
    // Do the operation
    // Pay attention to `stream_out().bytes_written()`, this is one
    // of the most interesting abstraction provided by ByteStream class.
    // When we use `push_substring`, we would push the bytes to ByteStream.
    // Thus, we can get one important idea:
    // stream_out().bytes_written() is always pointing to the
    // absolute current window size start
    if (_sender_isn.has_value()) {
        _reassembler.push_substring(seg.payload().copy(),
                                    unwrap(seg.header().seqno, _sender_isn.value(), stream_out().bytes_written()),
                                    seg.header().fin);
        _ack.emplace(wrap(stream_out().bytes_written(), _sender_isn.value()));

        // When we first accept the SYN, we should pay attention there is no
        // payload, in this situation, `_ack` would be equal to `_isn`. We should
        // avoid this situation.
        if (seg.header().syn) {
            _ack.emplace(_ack.value() + 1);
            _sender_isn.emplace(_ack.value());
        }

        // Only we have initialized the connection and we have made all
        // the bytes written into the ByteStream. We can plus one to `_ack`.

        // The reason why we should plus one to `_ack` is simple. Because
        // when closing the TCP connection, we receive the sender's seq
        // number, because of no payload, our `_ack` is equal to `seg.header().seqno`
        // Don't consider the sending, if we need the second FIN from the sender,
        // we should plus one to the `_ack` thus receiving the second FIN.

        if (stream_out().input_ended()) {
            _ack = WrappingInt32(_ack.value() + 1);
        }
    }
}
```
