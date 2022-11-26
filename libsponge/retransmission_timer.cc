#include "retransmission_timer.hh"

RetransmissionTimer::RetransmissionTimer(const size_t retx_timeout)
    : state{TimerState::stop}, _initial_rto{retx_timeout}, _rto{retx_timeout} {}

bool RetransmissionTimer::tick_callback(const size_t ms_since_last_tick) {
    //! Only when the timer is running, we add the `_accumulate_time`.
    if (state == TimerState::running) {
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
    if (state == TimerState::stop) {
        state = TimerState::running;
        reset_timer();
    }
}

void RetransmissionTimer::stop_timer() {
    if (state == TimerState::running) {
        state = TimerState::stop;
    }
}

void RetransmissionTimer::handle_expired() {
    _rto *= 2;
    _accumulate_time = 0;
}
