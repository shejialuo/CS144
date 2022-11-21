# Lab 1

First, you should carefully read the docs provided by the lab

> The TCP sender is dividing its byte stream up into short *segments* so that they can fit inside a
> datagram. But the network might reorder these datagrams, or drop them, or deliver them more than
> once. The receiver must reassemble the segments into the contiguous stream of bytes that they
> started out as.
>
> In this lab you'll write the data structure that will be responsible for this reassembly: a
> `StreamReassembler`. It will receive substrings, consisting of a string of bytes, and the index of
> the first byte of that string within the larger stream. Each byte of the steam has its own unique
> index, starting from zero and counting upwards. The `StreamReassembler` will own a ByteStream for
> the output: as soon as the reassembler knows the next byte of the stream, it will write it into the
> `ByteStream`. The owner can access and read from the `ByteStream` whenever it wants.

And it provides the following public interface:

+ `StreamReassembler(const size_t capacity)`: Construct a `StreamReassembler` that will store up to `capacity` bytes.
+ `push_substring(const string &data, const size_t index, const bool eof)`: Receive a substring and write any newly contiguous bytes into the stream.
+ `ByteStream &stream_out()`: Access the reassembled ByteStream (Lab 0)
+ `size_t unaseembled_bytes() const`: The number of bytes in the substrings stored but not yet reassembled.

So, the requirement is obvious. We need to maintain a window. When the datagram comes, we should check
whether we should accept the datagram. In order to achieve the functionality, we should consider the
following things:

+ Indicate the start index of the current window we want to accept.
+ The capacity of the window.

So we need an array to hold the data which I named `_stream`. And for this array, the logical index is
`_next_index` which means the next index we need. The logical index must exceed the actual length of
the `stream` but we could easily deal with that using `_next_index % _capacity`. Also, we need a value
`_unassembly` to record the current number of bytes stored but not yet reassembled. And we need a way
to know whether the array index is accessed. So maybe hash is a good idea, however, I think here I can
only need an array called `_dirty` to indicate whether the array index is accessed. Simple idea.

```c++
class StreamReassembler {
  private:
    ByteStream _output;           //!< The reassembled in-order byte stream
    size_t _capacity;             //!< The maximum number of bytes
    size_t _next_index = 0;       //!< The next index we except
    size_t _unassembly = 0;       //!< The number of bytes in the substrings stored but not yet reassembled
    bool _should_eof = false;     //!< Flag about telling ByteStream to end input
    std::vector<char> _stream{};  //!< The window
    std::vector<bool> _dirty{};   //!< A table to indicate whether the element is stored
    size_t next(size_t ptr) { return (ptr + 1) % _capacity; }
  ...
};
```

## Constructor

For constructor, it is easy, we should initialize the `_capacity` and `_output`, and initialize the
`_stream` and `_dirty`.

```c++
StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
    _stream.resize(capacity, 0);
    _dirty.resize(capacity, false);
}
```

## Easy Functions

There are some extremely easy functions:

```c++
size_t StreamReassembler::unassembled_bytes() const { return {_unassembly}; }
bool StreamReassembler::empty() const { return {_unassembly == 0}; }
```

## Reassembler

Now we come to the most interesting part. We should consider the following situations:

+ When the data is totally before the `_next_index`: we should reject.
+ When the data is totally after the `_next_index + _capacity`: we should reject.
+ When the data is inside the window: we should store the value to `_stream`, and sets the
corresponding `_dirty` to `true`. If we can find continuos range from `_next_index`, we should write
the data to the ByteStream and move the `_next_index` pointer.
+ When the data is partially inside the window: do as above.

And next we should indicate whether we should signal the ByteStream that the input is end. The
`push_substring` function provides a boolean value `eof` to indicate this string is the end. However,
when should we accept this information. This should be a *valid* data, which means that we could write
the data to the ByteStream. However, pay attention to the corner case: the data could be empty in this
situation.

However, there are some trivial details. You could see the following code with descriptive comments.

```c++
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
```
