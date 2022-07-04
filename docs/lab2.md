# Lab 2

The first task of lab 2 is to map the actual 64 bit sequence number
to the virtual 32 bit sequence number. It's an easy task.

```c++
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
  return isn + uint32_t(n);
}
```

And also we need to transform the virtual 32 bit sequence number
to the actual 64 bit sequence number. Well, the caller
should provide
a recent absolute 64-bit sequence number. Thus we should get the
closest actual 64-bit sequence number. Well, it is also easy.

Well, the most tricky thing here is bit-wise operation. You should
carefully handle
the unsigned and signed.

Well, next part is easy enough. No algorithms need.
