# Lab 3

First, you should read carefully read the docs provided by the lab.

> It will be your `TCPSender`'s responsibility to:
>
> + Keep track of the receiver's window
> + Fill the window when possible, by reading from the `ByteStream`, creating new TCP segments, and sending them. The sender should keep sending segments until either the window is full or the `ByteStream` is empty.
> + Keep track of which segments have been sent but not yet acknowledged by the receiver——we call these "outstanding" segments.
> + Re-send outstanding segments if enough time passes since they were sent, and they haven't been acknowledged yet.

## Retransmission timer

According to the docs, the first thing we need to do is to implement a class for retransmission timer. So I have created two files `retransmission_timer.hh` and `retransmission_timer.cc`. However, before we implement this class in action, we should understand the requirements:

+ `tick(const size_t ms_since_last_tick)` method is aimed at simulating the time. So every time the `tick` is called, we need to add the `ms_since_last_tick` to the `_accumulate_time`. When the `_accumulate_time` is greater than the `_rto`, the timer has elapsed. So we need to call a function called `tick_callback` every time `tick` is called.
+ If the timer has elapsed, and the window size is not zero: double the value of the `_rto`, so we need a function called `handle_expired`. Also, we need to set the `_accumulate_time` to be 0.
+ Every time a segment *containing data* is sent, if the timer is not running, start it running. So we need to maintain a state. I use a function called `start_timer` to start the timer.
+ When receiver gives the sender an `ackno` that acknowledges the successful receipt of *new* data: we use `reset_timer` to set the `_rto` to its initial value and clear the `_accumulate_timer`. If all the outstanding segments are received, we should stop the timer called `stop_timer`.

At now, we can code.

```c++
// file: retransmission_timer.hh
enum class State { running, stop };

class RetransmissionTimer {
  private:
    State state;                  //! the state of the timer
    size_t _initial_rto;          //! the initial retransmission timeout
    size_t _rto;                  //! current retransmission timeout
    size_t _accumulate_time = 0;  //! the accumulate time
  public:
    //! \brief constructor
    RetransmissionTimer(const size_t retx_timeout);

    //! \brief check whether the time is expired
    bool tick_callback(const size_t ms_since_last_tick);

    //! \brief when receiving a valid ack, reset the timer
    void reset_timer();

    //! \brief when the timer is expired we should handle this situation
    void handle_expired();

    //! \brief start the timer
    void start_timer();

    //! \brief stop the timer
    void stop_timer();
};
```

```c++
// file: retransmission_timer.cc
#include "retransmission_timer.hh"

RetransmissionTimer::RetransmissionTimer(const size_t retx_timeout)
    : state{State::stop}, _initial_rto{retx_timeout}, _rto{retx_timeout} {}

bool RetransmissionTimer::tick_callback(const size_t ms_since_last_tick) {
    //! Only when the timer is running, we add the `_accumulate_time`.
    if (state == State::running) {
        _accumulate_time += ms_since_last_tick;
        //! Check whether the timer has elapsed.
        return _rto <= _accumulate_time;
    }
    return false;
}

void RetransmissionTimer::reset_timer() {
    _rto = _initial_rto;
    _accumulate_time = 0;
}

void RetransmissionTimer::start_timer() {
    if (state == State::stop) {
        state = State::running;
        reset_timer();
    }
}

void RetransmissionTimer::stop_timer() {
    if (state == State::running) {
        state = State::stop;
    }
}

void RetransmissionTimer::handle_expired() {
    _rto *= 2;
    _accumulate_time = 0;
}
```

## TCP sender

There are four public interfaces we need to implement for `TCPSender`:

+ `void fill_window()`.
+ `void ack_received(const WrappingInt32 ackno, const uint16_t window_size)`.
+ `void tick(const size_t ms_since_last_tick)`.
+ `void send_empty_segment()`.

Well, the main purpose for `fill_window()` is to translate ByteStream to TCPSegment. And we need to push these segments to `_segments_out`. And writes a reference copy to `_outstanding_segments` .In this process, there are some many things we need to consider:

+ We should record the receiver's acknowledge number `_receiver_ack` and window size `_receiver_window_size` to know whether we could transmit the new segments to the receiver.
+ We also need to consider about the ByteStream size.
+ We need to think about the data structure of the `_outstanding_segments`. In `full_window()`, it should support insertion.

For `ack_received()`, when we receiving the acknowledge number from the receiver, first we update `_receiver_ack` and delete the fully acknowledged segments from `_outstanding_segments` and calls `fill_window()`. So the data structure for `_outstanding_segments` should support deletion.

For `tick()`, it should check whether the retransmission timer has expired. If so, it should retransmit the *earliest* (lowest sequence number) segment. So we need to make the `_outstanding_segments` sorted.

### The data structure of outstanding segments

I decide to use `list` to represent the data structure of outstanding segments. The reasons are as follows:

+ We could easily make it sorted.
+ We could add and delete a segment fast.

### Some private members

Before implementing the interfaces, we can define the following fields:

+ `_retransmission_timer`: the timer defined above.
+ `_receiver_ack`: the absolute receiver ack.
+ `_receiver_window_size`: the receiver window size which should be 1 when initialized.
+ `_outstanding_segments`: the outstanding segments
+ `_consecutive_retransmissions`: the consecutive retransmissions.
+ `window_not_full`: a helper function to tell whether the window is not full.

```c++
class TCPSender {
  private:
    ...
    //! the retransmission timer
    RetransmissionTimer _retransmission_timer;

    //! the (absolute) receiver ack.
    uint64_t _receiver_ack{0};

    //! the initial window size should be 1
    uint64_t _receiver_window_size{1};

    //! the outstanding segments
    std::list<TCPSegment> _outstanding_segments{};

    //! the consecutive retransmissions
    unsigned int _consecutive_retransmissions{0};

    //! a helper function to tell whether the window is not full
    bool window_not_full(uint64_t window_size) const { return window_size > bytes_in_flight(); }
}
```

### fill_window

There are two special cases we should consider for the `fill_window` functions:

+ TCP connection
+ TCP disconnection

For the TCP connection where `_next_seqno == 0`, we should set the `syn` to be `true` and sends the datagram out.

For the TCP disconnection, things could be much more complicated. There are two cases:

+ An empty payload which indicates the disconnection where `stream_in().eof()` is `true`.
+ Nonempty payload which indicates the disconnection where `stream_in().eof()` is `true`.

For transferring the data, we want to transfer as much as we want. We should consider about the following three things:

+ For a TCP segment, the length which should not exceed `TCPConfig::MAX_PAYLOAD_SIZE`.
+ We can only transfer the length which should not exceed `_receiver_window_size - bytes_in_flight()`.
+ We can only transfer the length which should not exceed `stream_in().buffer_size()`.

At the end, we need to start the timer.

Well, It may seem easy, however, there are some many corner cases. Please look at the following code for details.

```c++
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
```

### ack_received

For receiving new acknowledge number from the receiver, which means the `ackno` is greater than the next sequence number need to be sent. And `ackno` is less than the current `_receiver_ack`. We just return.

And next we need to step by step to check the `_outstanding_segments`, if the `ackno` can accept the full segments, we update the `_receiver_ack` value. If not, we break the loop. Also, we need to update the value of `_receiver_window_size`. However, there would be a corner case: *what if the window size is zero*:

> If the receiver has announced a window size of zero, the fill window method should act like the window size is one. The sender might end up sending a single byte that gets rejected (and not acknowledged) by the receiver, but this can also provoke the receiver into sending a new acknowledgment segment where it reveals that more space has opened up in its window. Without this, the sender would never learn that it was allowed to start sending again.

And also we should not double the retransmission timeout in this case. So we need to test whether the `_receiver_window_size` equals to 0 in `tick` method.

```c++
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
```

### tick

`tick` is easy, because the most important logic we have already implemented:

```c++
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
```

### send_empty_segment

It is easy to implement `send_empty_segment`. Just change the `seq` of the segment.

```c++
void TCPSender::send_empty_segment() {
    TCPSegment empty{};
    empty.header().seqno = _isn + _next_seqno;
    segments_out().push(empty);
}
```
