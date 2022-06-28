# Lab 0

Lab 0 is aimed at writing a `ByteStream` class. This class is used
to maintain a buffer space. The idea is simple. I use `vector<char>`
to maintain a loop queue. In the class, I use two pointers
`_write_ptr` and `_read_ptr` to indicate the position. The data structure is
easy. And I think this implementation is efficient.

In order to better utilize the pointer, I encapsulate two methods to be private:

```c++
void next_write(size_t size) {
  _write_ptr = (_write_ptr + size) % _capacity;
}
void next_read(size_t size) {
  _read_ptr = (_read_ptr + size) % _capacity;
}
```

And the tricky part is how to set EOF. Actually, when the input is end, the caller should
call the `ByteStream::end_input()`, so `ByteStream::eof()` is easy to set:

```c++
bool ByteStream::eof() const {
  return input_ended() && buffer_empty();
}
```

For the other operations, it is easy.
