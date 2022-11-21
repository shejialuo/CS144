# Lab 0

## Writing webget

It is easy to write the `webget`.

```c++
void get_URL(const string &host, const string &path) {
    TCPSocket tcpSocket{};
    const Address addr(host, "http");
    const string request = "GET " + path + " HTTP/1.1\r\nHost: " + host + " \r\nConnection: close \r\n\r\n";
    tcpSocket.connect(addr);
    tcpSocket.write(request);
    while (!tcpSocket.eof()) {
        cout << tcpSocket.read();
    }
}
```

## An in-memory reliable byte stream

First, you should carefully read the docs provided by the lab. The abstract requirement is below:

> Bytes are written on the "input" side and can be read, in the same sequence, from the
> output "side". The byte steam is finite: the writer can end the input, and then no
> bytes can be written. When the reader has read to the end of the stream, it will reach
> "EOF" and no more bytes can be read.
>
> Your byte stream will also be flow-controlled to limit its memory consumption at any given time.
> The object is initialized with a particular “capacity”: the maximum number of bytes it's willing
> to store in its own memory at any given point. The byte stream will limit the writer in how much
> it can write at any given moment, to make sure that the stream doesn’t exceed its storage capacity.
> As the reader reads bytes and drains them from the stream, the writer is allowed to write more.
> Your byte stream is for use in a single thread—you don’t have to worry about concurrent
> writers/readers, locking, or race conditions.

The idea I use is simple. I use `vector<char>` to maintain a RingBuffer. In the class,
I use two pointers `_write_ptr` and `read_ptr` to indicate the position. And some other
fields to make the implementation easier.

```c++
class ByteStream {
  private:
    int _capacity;                   //!< the capacity of the ByteStream.
    std::vector<char> ringBuffer{};  //!< the ringBuffer.
    size_t _write_ptr = 0;           //!< the write pointer.
    size_t _read_ptr = 0;            //!< the read pointer.
    size_t _write_bytes_count = 0;   //!< record how many bytes are written.
    size_t _read_bytes_count = 0;    //!< record how many bytes are read.
    size_t _size = 0;                //!< the current size of the ringBuffer
    bool _input_end = false;         //!< whether the writing should be end
    bool _error = false;             //!< Flag indicating that the stream suffered an error.
    void advance_write(size_t size) { _write_ptr = (_write_ptr + size) % _capacity; }
    void advance_read(size_t size) { _read_ptr = (_read_ptr + size) % _capacity; }
}
```

Now we could go to implementation, the public interface of `ByteStream` class is below:

+ `ByteStream(const size_t capacity)`: Construct a stream with room for `capacity` bytes.
+ `size_t write(const std::string &data)`: Write a string of bytes into the stream. Write as many
as will fit, and return how many were written.
+ `size_t remaining_capacity() const`: Returns the number of additional bytes that the stream
has space for.
+ `void end_input()`: Signal that the byte stream has reached its ending.
+ `void set_error()`: Indicate that the stream suffered an error.
+ `std::string peek_output(const size_t len) const`: Peek at next "len" bytes of the stream.
+ `void pop_output(const size_t len)`: Remove bytes from the buffer.
+ `std::string read(const size_t len)`: Read the next "len" bytes of the stream.
+ `bool input_ended() const`: returns `true` if the stream input has ended.
+ `bool error() const`: returns `true` if the stream has suffered an error
+ `size_t buffer_size() const`: returns the maximum amount that can currently be read
from the stream.
+ `bool buffer_empty() const`: returns `true` if the buffer is empty.
+ `bool eof() const`: returns `true` if the output has reached the ending.
+ `size_t bytes_written() const`: Total number of bytes written.
+ `size_t bytes_read() const`: Total number of bytes popped.

There are some easy parts we can implement immediately.

### Constructor

```c++
ByteStream::ByteStream(const size_t capacity) : _capacity(capacity) { ringBuffer.resize(_capacity); }
```

For the constructor, we set the `_capacity` value and resize the `ringBuffer` to
a fixed size. And we will never change the size of the `ringBuffer`.

### Setter and Getter

There are some easy public interfaces.

```c++
void set_error() { _error = true; }
bool error() const { return _error; }
void ByteStream::end_input() { _input_end = true; }
bool ByteStream::input_ended() const { return _input_end; }
size_t ByteStream::buffer_size() const { return _size; }
bool ByteStream::buffer_empty() const { return _size == 0; }
size_t ByteStream::bytes_written() const { return _write_bytes_count; }
size_t ByteStream::bytes_read() const { return _read_bytes_count; }
size_t ByteStream::remaining_capacity() const { return _capacity - _size; }
```

### EOF

The tricky here is when the file will reach the end:

+ Input is end.
+ The buffer is empty.

```c++
bool ByteStream::eof() const { return input_ended() && buffer_empty(); }
```

### Write Operation

It is easy to write. We just first get the maximum length we could write and
write the data into the RingBuffer. And we need to do some error handling.
When the `input_ended()` is true, we should not write.

```c++
size_t ByteStream::write(const string &data) {
    if (input_ended()) {
        set_error();
        return 0;
    }
    size_t length = std::min(remaining_capacity(), data.size());
    for (size_t i = 0; i < length; ++i) {
        ringBuffer[_write_ptr] = data[i];
        advance_write(1);
    }
    _size += length;
    _write_bytes_count += length;
    return length;
}
```

For `peek_output`, we just copy it to a new `string`.

```c++
string ByteStream::peek_output(const size_t len) const {
    std::string str{};
    const size_t length = std::min(len, buffer_size());
    for (size_t i = 0; i < length; ++i) {
        str.push_back(ringBuffer[(i + _read_ptr) % _capacity]);
    }
    return str;
}
```

### Read Operation

We first consider the `pop_output` operation, we should do nothing about
the RingBuffer itself. Just move the `_read_ptr`, changes the `_size`
and adds the `_read_bytes_count`.

```c++
void ByteStream::pop_output(const size_t len) {
    const size_t length = std::min(len, buffer_size());
    advance_read(length);
    _size -= length;
    _read_bytes_count += length;
}
```

For `read`, we should do no error check even if the file is end.
If the EOF is true, we just return an empty string. It's important
here because in reality this situation happens!

```c++
std::string ByteStream::read(const size_t len) {
    std::string str{};
    str = peek_output(len);
    pop_output(len);
    return str;
}
```
