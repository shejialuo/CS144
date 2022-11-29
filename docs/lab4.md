# Lab 4

Before we write the code, we should think what would `TCPConnection` do. If you carefully read the docs provided by lab. You will understand the requirements easily. But the most important thing is to understand how `TCPConnection` combines the `TCPReceiver` and `TCPSender` and achieves the functionality.

Here, I give an example to illustrate the process. I named the two instances of `TCPConnection` called `TCPConnectionA` and `TCPConnectionB`. And the `TCPConnectionA` has two classes `TCPReceiverA` and `TCPSenderA`, respectively, the `TCPConnectionB` has two classes `TCPReceiverB` and `TCPSenderB`.

When `TCPConnectionA` sends the segment to `TCPConnectionB`, it will calls the `TCPSenderA`'s `fill_window()` method to extract the data to the segments. It will maintain the absolute sequence number for the next byte to be sent. However, *there is one thing we do not consider, how about the acknowledge number in the header?*. We need to do this in the `TCPConnection` class.

And when `TCPConnectionB` using `segment_received` method to handle the received segments. It should do the following things:

+ It should uses `TCPReceiverB`'s method `segment_received` method which updates its acknowledge number which should later be sent to the `TCPConnectionA`. Now we can understand the above question: the acknowledge number comes from itself receiver's stored acknowledge number.
+ Next it should use `TCPSenderB`'s `ack_received` to update the `TCPSenderA`'s acknowledge number and its window size.
+ Then we should call the `fill_window` method and pops the `TCPSenderB`'s `_segments_out` to `TCPConnectionB`'s `_segments_out`. And also we need to change the header part, set the acknowledge number to the `TCPReceiverB`'s acknowledge number.

I am really appreciating the nice abstraction for this design. You can look at the following picture for better understanding.

![TCPConnection Example](https://s2.loli.net/2022/11/28/Hcbp5rQ6qK8fCzF.png)

## Easy functions

There are some easy functions which just some getters. We can implement these functions immediately.

```c++
class TCPConnection {
  private:
    //! the flag to indicate whether it is alive
    bool _active{true};

    //! the time interval since last segment received.
    size_t _time_since_last_segment_received{};
};
```

```c++
size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

bool TCPConnection::active() const { return _active; }
```

## TCP Connection

It's not hard to write the code about sending the data or receiving the data. I have already talked about a lot above. However, we need to handle the connection and close carefully.

### Connect

In the `TCPSender` class, we actually do not consider the ack. So we need to use `_sender.send_empty_segment()` to produce a new segment when connecting.

### Close

The most difficult part is how to gracefully close the `TCPConnection`. For unclean shutdown, it's easy. We just send or receive a segment with the `RST` flag set.

However, for clean shutdown. There are so many things we need to do.

+ *Prereq #1* The inbound stream has been fully assembled and has ended. I use a private function called `check_inbound_stream_assembled_and_ended`.

  ```c++
  bool TCPConnection::check_inbound_stream_assembled_and_ended() { return _receiver.stream_out().eof(); }
  ```

+ *Prereq #2* The outbound stream has been ended by the local application and fully sent the fact that it ended to the remote peer. Remember, I have provided a `end` flag but without no public method to get that value. So I do this and uses a function called `check_outbound_stream_ended_and_send_fin`.

  ```c++
  bool TCPConnection::check_outbound_stream_ended_and_send_fin() { return _sender.stream_in().eof() && _sender.is_end(); }
  ```

+ *Prereq #3* The outbound stream has been fully acknowledged by the remote peer. I use a function called `check_outbound_fully_acknowledged`.

  ```c++
  bool TCPConnection::check_outbound_fully_acknowledged() { return _sender.bytes_in_flight() == 0; }
  ```

+ *Prereq #4* It's important to understand the reason why there are two situations. We would implement this in the `tick` function

We can code now.

## Auxiliary functions

Here, I first define a function called `set_ack_and_window`, it inspects the `_receiver`'s acknowledge number and window size. And updates the corresponding fields of itself.

```c++
void TCPConnection::set_ack_and_window(TCPSegment &seg) {
    if (_receiver.ackno().has_value()) {
        seg.header().ack = true;
        seg.header().ackno = _receiver.ackno().value();
    }
    size_t window_size = _receiver.window_size();
    if (window_size > numeric_limits<uint16_t>::max()) {
        window_size = numeric_limits<uint16_t>::max();
    }

    seg.header().win = window_size;
}
```

And I define `send_new_segments`, which recursively adds new segments to the `_segments_out`. The most importantly, we should indicate whether we could write new segments. There are situations the segment we need to send with no payload but with SYN or FIN set or a keep-alive message.

```c++
bool TCPConnection::send_new_segments() {
    bool is_really_send = false;
    while (!_sender.segments_out().empty()) {
        is_really_send = true;
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_window(segment);
        _segments_out.push(segment);
    }
    return is_really_send;
}
```

Also, there are two situations we should send a segment with RST set. So I use a function named `send_rst_flag_segment`:

```c++
void TCPConnection::send_rst_flag_segment() {
    _sender.send_empty_segment();
    TCPSegment segment = _sender.segments_out().front();
    _sender.segments_out().pop();
    set_ack_and_window(segment);
    segment.header().rst = true;
    _segments_out.push(segment);
}
```

Also, I define `set_error` function to handle the RST set segment or we want to send a new RST set segment.

```c++
void TCPConnection::set_error() {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
}
```

## connect

Now we comes to the most important part, when the client wants to connect the server, it calls the `connect`. It is simple enough, because I have done the job in the `TCPSender.

```c++
void TCPConnection::connect() {
    _sender.fill_window();
    send_new_segments();
}

// file: tcp_sender.cc
void TCPSender::fill_window() {
    ...
    if (_next_seqno == 0) {
        segment.header().syn = true;
        segment.header().seqno = _isn + _next_seqno;
        _next_seqno += 1;
    }
    ...
}
```

## segment_received

For `segment_received`, it is tricky.

1. We handle the segment with RST set. It is easy.
2. We should call `_receiver.segment_received` method to update the acknowledge number and window size.
3. We check whether the inbound stream is end (the opposite sender would tell us this information), if so, we are the passive, we don't need the `TIME_WAIT` timer, set the `_linger_after_streams_finish` to be `false`.
4. When the received segment with ACK set, we should first checkout whether we should accept the segment. If the `receiver_ackno()` doesn't exist, we just return. Otherwise, we should call `_sender.ack_received` and `_fill_window()` and calls `send_new_segments`.
5. Next, we need to handle the situation with no payload.

```c++
void TCPConnection::segment_received(const TCPSegment &seg) {
    // Reset the accumulated time
    _time_since_last_segment_received = 0;

    // If the `rst` flag is set, sets both the inbound and outbound
    // streams to the error state and kills the connection permanently.
    if (seg.header().rst) {
        set_error();
        return;
    }

    // the receiver would update the acknowledge number and window size
    // of itself.
    _receiver.segment_received(seg);

    // If the inbound stream ends before the `TCPConnection` has reached EOF
    // on its outbound stream, `_linger_after_streams_finish` should be false
    if (check_inbound_stream_assembled_and_ended() && !_sender.stream_in().eof()) {
        _linger_after_streams_finish = false;
    }

    if (seg.header().ack) {
        // Corner case: When listening, we should drop all the ACK.
        if (!_receiver.ackno().has_value())
            return;
        _sender.ack_received(seg.header().ackno, seg.header().win);
        _sender.fill_window();
        send_new_segments();
    }

    if (seg.length_in_sequence_space() > 0) {
        _sender.fill_window();
        if (!send_new_segments()) {
            _sender.send_empty_segment();
            TCPSegment segment = _sender.segments_out().front();
            _sender.segments_out().pop();
            set_ack_and_window(segment);
            _segments_out.push(segment);
        }
    }
}
```

## write

Simple enough.

```c++
size_t TCPConnection::write(const string &data) {
    size_t length = _sender.stream_in().write(data);
    _sender.fill_window();
    send_new_segments();
    return length;
}
```

## end_input_stream

`end_input_stream` aims at doing active close. So we first signal the `_sender`'s ByteStream to be end and calls `_fill_window`.

```c++
void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_new_segments();
}
```

## tick

For `tick`, we should retransmit the segment. If the retransmission time `consecutive_retransmissions()` is greater than `_cfg.MAX_RETX_ATTEMPTS`, we should produce a segment with RST set. However, this is not the most important point. When `tick` is called, for passive closer, it just returns. For active closer we need to make sure that the passive closer has successfully received the `ACK` sent by the active closer. But we have no idea, so if the passive closer doesn't retransmit the segment in a period of time, we can think that passive closer has already been closed. Thus we can close the active closer.

```c++
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);

    // We need to retransmit the segments
    if (!_sender.segments_out().empty()) {
        TCPSegment segment = _sender.segments_out().front();
        _sender.segments_out().pop();
        set_ack_and_window(segment);
        if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS) {
            set_error();
            segment.header().rst = true;
        }
        _segments_out.push(segment);
    }

    if (check_inbound_stream_assembled_and_ended() && check_outbound_stream_ended_and_send_fin() &&
        check_outbound_fully_acknowledged()) {
        if (!_linger_after_streams_finish) {
            _active = false;
        } else if (_time_since_last_segment_received >= 10 * _cfg.rt_timeout) {
            _active = false;
        }
    }
}
```
