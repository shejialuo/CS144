#include "tcp_connection.hh"

#include <iostream>
#include <limits>

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

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

void TCPConnection::send_rst_flag_segment() {
    _sender.send_empty_segment();
    TCPSegment segment = _sender.segments_out().front();
    _sender.segments_out().pop();
    set_ack_and_window(segment);
    segment.header().rst = true;
    _segments_out.push(segment);
}

void TCPConnection::set_error() {
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _active = false;
}

bool TCPConnection::check_inbound_stream_assembled_and_ended() { return _receiver.stream_out().eof(); }

bool TCPConnection::check_outbound_stream_ended_and_send_fin() { return _sender.stream_in().eof() && _sender.is_end(); }

bool TCPConnection::check_outbound_fully_acknowledged() { return _sender.bytes_in_flight() == 0; }

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

bool TCPConnection::active() const { return _active; }

size_t TCPConnection::write(const string &data) {
    size_t length = _sender.stream_in().write(data);
    _sender.fill_window();
    send_new_segments();
    return length;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
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

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_new_segments();
}

void TCPConnection::connect() {
    _sender.fill_window();
    send_new_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            set_error();
            send_rst_flag_segment();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
