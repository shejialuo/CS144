#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // Here, we should ensure we set the initial sequence number
    // when we first time receive the SYN.
    if (seg.header().syn) {
        _sender_isn.emplace(seg.header().seqno);
    }

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

optional<WrappingInt32> TCPReceiver::ackno() const { return _ack; }

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
