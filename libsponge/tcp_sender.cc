#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _retransmission_timer{retx_timeout} {}

uint64_t TCPSender::bytes_in_flight() const { return _next_seqno - _receiver_ack; }

void TCPSender::fill_window() {
    // Special case: we have already sent the `FIN`.
    if (end) {
        return;
    }

    TCPSegment segment{};

    // Special case: when the `_receiver_window_size` equals 0
    uint64_t window_size = _receiver_window_size == 0 ? 1 : _receiver_window_size;

    // Special case : TCP connection
    if (_next_seqno == 0) {
        segment.header().syn = true;
        segment.header().seqno = _isn + _next_seqno;
        _next_seqno += 1;
    } else {
        // Find the length to read from the `stream_in()`
        uint64_t length =
            std::min(std::min(window_size - bytes_in_flight(), stream_in().buffer_size()), TCPConfig::MAX_PAYLOAD_SIZE);
        segment.payload() = Buffer{std::move(stream_in().read(length))};
        segment.header().seqno = _isn + _next_seqno;
        _next_seqno += length;

        // When the `stream_in` is end of file,  we need to set the `fin` to `true`.
        // Pay attention, we should check whether there is an enough window size
        if (stream_in().eof() && window_not_full(window_size)) {
            segment.header().fin = true;
            end = true;
            _next_seqno++;
        }

        // We should do nothing
        if (length == 0 && !end)
            return;
    }
    segments_out().push(segment);
    _outstanding_segments.push_back(segment);
    _retransmission_timer.start_timer();
    if (window_not_full(window_size)) {
        fill_window();
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // When receiving unneeded ack, just return.
    if (unwrap(ackno, _isn, next_seqno_absolute()) > _next_seqno ||
        unwrap(ackno, _isn, next_seqno_absolute()) < _receiver_ack) {
        return;
    }

    uint64_t absolute_ack = unwrap(ackno, _isn, next_seqno_absolute());
    _receiver_window_size = window_size;
    bool is_ack_update = false;

    auto iter = _outstanding_segments.begin();
    while (iter != _outstanding_segments.end()) {
        uint64_t sequence_num = unwrap(iter->header().seqno, _isn, next_seqno_absolute());
        if (sequence_num + iter->length_in_sequence_space() <= absolute_ack) {
            _receiver_ack = sequence_num + iter->length_in_sequence_space();
            iter = _outstanding_segments.erase(iter);
            is_ack_update = true;
        } else {
            iter++;
        }
    }

    // When there is no outstanding segments, we should stop the timer
    if (_outstanding_segments.empty()) {
        _retransmission_timer.stop_timer();
    }

    // When the receiver gives the sender an ackno that acknowledges
    // the successful receipt of new data
    if (is_ack_update) {
        _retransmission_timer.reset_timer();
        _consecutive_retransmissions = 0;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    if (_retransmission_timer.tick_callback(ms_since_last_tick)) {
        if (_receiver_window_size == 0) {
            _retransmission_timer.reset_timer();
        } else {
            _retransmission_timer.handle_expired();
        }
        _consecutive_retransmissions++;
        segments_out().push(_outstanding_segments.front());
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _consecutive_retransmissions; }

void TCPSender::send_empty_segment() {
    TCPSegment empty{};
    empty.header().seqno = _isn + _next_seqno;
    segments_out().push(empty);
}
