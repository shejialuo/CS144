#include "stream_reassembler.hh"

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
    _stream.resize(capacity, 0);
    _dirty.resize(capacity, false);
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // When the string is out of the `_next_index+_capacity` or the end of the string
    // is before the `_next_index`, we should do NOTHING.
    if (index >= _capacity + _next_index || _next_index > index + data.size())
        return;

    // Here we should consider the situation that we should accept part of
    // the string, when `actual_index < _next_index`. We should set the `actual_index`
    // to be the `_next_index` to drop the previous string, and make `data_index`
    // to be the `_next_index-index`.
    size_t actual_index = index;
    size_t data_index = 0;
    if (index < _next_index) {
        actual_index = _next_index;
        data_index += _next_index - index;
    }

    // Here we need to find the start index and loop index
    size_t start_index = _next_index % _capacity;
    size_t loop_index = actual_index % _capacity;

    // Corner case: when the data is empty. This is important
    // Because the below iteration does not consider
    if (data.empty()) {
        if (eof) {
            _should_eof = true;
        }
    }

    // Here, when `_dirty[index] == false` We store the byte into
    // the `_stream[index]` and set the `_dirty[index]` to be true.
    for (size_t i = loop_index, j = data_index; j < data.size(); i = next(i), j++) {
        if (!_dirty[i]) {
            _stream[i] = data[j];
            _unassembly++;
            _dirty[i] = true;
        }
        if (j + 1 == data.size()) {
            if (eof) {
                _should_eof = true;
            }
        }

        if (next(i) == start_index)
            break;
    }

    // We should calculate consecutive `_dirty[index]` from `_next_index`.
    // Pay attention, you shouldn't set the `dirty[index]` to false,
    // because we DO NOT KNOW the ByteStream's size, so we
    // need to get the bytes actually written into the `_output`.
    string send_str{};
    for (size_t i = start_index; _dirty[i]; i = next(i)) {
        send_str.push_back(_stream[i]);
        if (next(i) == start_index)
            break;
    }

    // If there is need to send the bytes
    if (!send_str.empty()) {
        size_t write_num = stream_out().write(send_str);
        for (size_t i = start_index, j = 0; j < write_num; i = next(i), ++j) {
            _dirty[i] = false;
        }
        _next_index += write_num;
        _unassembly -= write_num;
    }

    if (_should_eof && empty()) {
        stream_out().end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return {_unassembly}; }

bool StreamReassembler::empty() const { return {_unassembly == 0}; }
