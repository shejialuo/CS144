# Lab 0 source code reading

## FileDescriptor

First, the source code uses `FileDescriptor` class to wrap the file desciptor. The `FileDescriptor`
class first defines a private `FDWrapper` class, and also defines a private shared ptr
to point to the `FDWrapper` class. The code
has also provided the private constructor to duplicate the `FileDescriptor`, and a public constructor
to accept a `const int` file descriptor for initialization. Because there is no
need to copy or move the `FDWrapper`, we disable all these operations. And due to the principle
of the RAII, we should close the file in the destructor.

Thus, we can have the following code to present the functionality of the `FDWrapper`.
(The code may be not same with the source code)

```c++
class FDwrapper {
public:
  int _fd;
  bool _eof = false;
  bool _closed = false;
  unsigned _read_count = 0;
  unsigned _read_count = 0;

  explicit FDWrapper(const int fd) {
    _fd = fd;
    if(fd < 0) {
      throw runtime_error("invalid fd number:" + to_string(fd));
    }
  }
  ~FDWrapper() {
    try {
      if(_closed) {
        return;
      }
      close();
    } catch(const exception &e) {
      std::cerr << "Exception destructing FDWrapper: " << e.what() << std::endl;
    }
  }
  void close() {
    SystemCall("close", ::close(_fd));
    _eof = _closed = true;
  }
  FDWrapper(const FDWrapper &other) = delete;
  FDWrapper &operator=(const FDWrapper &other) = delete;
  FDWrapper(FDWrapper &&other) = delete;
  FDWrapper &operator=(FDWrapper &&other) = delete;
};
```

This code uses `SystemCall` in `util.cc`, it just wraps to
easily deal with the return value and error handle.

```c++
int SystemCall(const char *attempt, const int return_value, const int errno_mask = 0) {
  if(return_value >= 0 || errno == errno_mask) {
    return return_value;
  }

  throw unix_error(attempt);
}

int SystemCall(const string &attempt, const int return_value, const int errno_mask) {
  return SystemCall(attempt.c_str(), return_value errno_mask);
}
```

The code defines new exception class `unix_error`:

```c++
class unix_error : public tagged_error {
public:
  explicit unix_error(const std::string &attempt, const int error = errno)
    :tagged_error(std::system_category(), attempt, error){}
};
```

Well, let's look at the `tagged_error`, which just wraps the `attempt`
to make the information of the exception more precise.

```c++
class tagged_error : public std::system_error {
private:
  std::string _attempt_and_error;
public:
  tagged_error(const std::error_category& category,const std::string &attempt, const int error_code)
    : system_error(error_code, category), _attempt_and_error(attempt + ": " + std::system_error::what()) {}
  const char *what() const noexcept override { return _attempt_and_error.c_str(); }
};
```

Now we have explained the `FDWrapper`, the `FileDescriptor` is easy,
and the most operation we will use is to read and write the file,
however, we will talk the operation later.

```c++
class FileDescriptor {
  class FDWrapper;
  std::shared_ptr<FDWrapper> _internal_fd;
  explicit FileDescriptor(std::shared_ptr<FDWrapper> other_shared_ptr) {
    _internal_fd = move(other_shared_ptr)
  }
protected:
  void register_read() {
    ++_internal_fd->_read_count;
  }
  void register_write() {
    ++_internal_fd->_write_count;
  }
public:
  explicit FileDescriptor(const int fd) {
    _internal_fd = make_shared<FDWrapper>(fd);
  }
  FileDescriptor duplicate() const {
    return FileDescriptor(_internal_fd);
  }
  ~FileDescriptor() = default;
  void close() {
    _internal_fd->close();
  }
  int fd_num() const { return _internal_fd->_fd; }
  bool eof() const { return _internal_fd->_eof; }
  bool closed() const { return _internal_fd->_closed; }
  unsigned int read_count() const { return _internal_fd->_read_count; }
  unsigned int write_count() const { return _internal_fd->_write_count; }

    FileDescriptor(const FileDescriptor &other) = delete;
    FileDescriptor &operator=(const FileDescriptor &other) = delete;
    FileDescriptor(FileDescriptor &&other) = default;
    FileDescriptor &operator=(FileDescriptor &&other) = default;
};
```

## Buf

It's important to store a buffer, so the code first defines the basic class `Buffer`.
The `Buffer` class use `string` as the buffer, just like `FileDescriptor`,
it will holds a shared ptr to the buffer.

```c++
class Buffer {
private:
  std::shared_ptr<std::string> _storage{};
  size_t _starting_offset{};
public:
  Buffer() = default;
  Buffer(std::string &&str) noexcept
    : _storage(std::make_shared<std::string>(std::move(str))) {}
  std::string_view str() const {
    if(not _storage) {
      return {};
    }
    return {_storage->data() + _starting_offset, _storage->size() - _starting_offset};
  }
  operator std::string_view() const {
    return str();
  }
  uint8_t at(const size_t n) const {
    return str().at(n);
  }
  size_t size() const { return str().size(); }
  std::string copy() const {
    return std::string(str());
  }
  void remove_prefix(const size_t n) {
    if(n > str().size) {
      throw out_of_range("Buffer::remove_prefix");
    }
    _starting_offset += n;
    if(_storage and _starting_offset == _storage->size()) {
      _storage.reset();
    }
  }
};
```

The `Buffer` class uses c++17 `string_view` to substitute the `const string`
to avoid unnecessary copy, `string_view` don't do any copy operation,
it just points to the same memory. However, in order to manage the buffers,
the code use `BufferList` class.

```c++
class BufferList {
private:
  std::deque<Buffer> _buffers{};
public:
  BufferList() = default;
  BufferList(Buffer buffer) : _buffers{buffer} {}
  BufferList(std::string &&str) noexcept {
    Buffer buf{std::move(str)};
    append(buf);
  }
  const std::deque<Buffer> &buffers() const {
    return _buffers;
  }
  void append(const BufferList &other) {
    for(const auto& buf: other._buffers) {
      _buffers.push_back(buf);
    }
  }
  operator Buffer() const {
    switch(_buffers.size()) {
      case 0:
        return {};
      case 1:
        return _buffers[0];
      default: {
        throw runtime_error("
                BufferList: please use concatenate() to combine a multi-Buffer BufferList into one Buffer");
      }
    }
  }
  std::string concatenate() const {
    std::string ret;
    ret.reserve(size());
    for(const auto &buf : _buffers) {
      ret.append(buf);
    }
    return ret;
  }
  size_t size() const {
    size_t ret = 0;
    for(const auto& buf: _buffers) {
      ret += buf.size();
    }
    return ret;
  }
  void remove_prefix(size_t n) {
    while(n > 0) {
      if(_buffers.empty()) {
        throw std::out_of_range("BufferList::remove_prefix");
      }
      if(n < _buffers.front().size()) {
        n = 0;
      } else {
        n -= _buffers.front().size();
        _buffers.pop_front();
      }
    }
  }
};
```

Now we have `BufferList`, sometimes we just want to view the buffer content,
so we utilize the c++17 `string_view` to make a `BufferViewList` class.

```c++
class BufferViewList {
  std::deque<std::string_view> _views{};
  BufferViewList(const std::string &str) : BufferViewList(std::string_view(str)) {}
  BufferViewList(const char *s) : BufferViewList(std::string_view(s)) {}
  BufferViewList(std::string_view str) { _views.push_back({const_cast<char *>(str.data()), str.size()}); }
  BufferViewList(const BufferList &buffers) {
    for(const auto &x : buffers.buffers()) {
      _views.push_back(x);
    }
  }
  void remove_prefix(size_t n) {
    while(n > 0) {
      if (_views.empty()) {
        throw std::out_of_range("BufferListView::remove_prefix")
      }
      if(n < _views.front().size()) {
        _views.front().remove_prefix(n);
        n = 0;
      } else {
        n -= _views.front().size();
        _views.pop_front();
      }
    }
  }
  size_t size() const {
    size_t ret = 0;
    for(const auto& buf: _views) {
        ret += buf.size();
    }
    return ret;
  }
};
```

Here, I omit reading the `as_iovecs` function. Later for it.

## Reading and Writing File

We have talked about the source code of the buffer, it's time
to find how `FileDescriptor` to read and write files. Well,
actually, it doesn't use much.

```c++
class FileDescriptor {
public:
  ...
  void read(std::string &str, const size_t limit) {
    constexpr size_t BUFFER_SIZE = 1024 * 1024;
    const size_t size_to_read = min(BUFFER_SIZE, limit);
    str.resize(size_to_read);

    ssize_t bytes_read = SystemCall("read", ::read(fd_num(), str.data(), size_to_read));
    if(limit > 0 && bytes_read == 0) {
      _internal_fd->_eof = true;
    }
    if (bytes_read > static_cast<ssize_t>(size_to_read)) {
      throw runtime_error("read() read more than requested");
    }
    str.resize(bytes_read);
    register_read();
  }
  std::string read(const size_t limit = std::numeric_limits<size_t>::max()) {
    string ret;
    read(ret, limit);
    return ret;
  }
};
```

For `read` operation, it is easy. The most important thing is that
the class provides user allocation string or string allocated by class.
The encapsulation is wonderful.

```c++
class FileDescriptor {
public:
  ...
  size_t write(BufferViewList buffer, const bool write_all = true) {
    size_t total_ bytes_written = 0;

    do {
      auto iovecs = buffer.as_iovecs();
      const ssize_t bytes_written = SystemCall("writev", ::writev(fd_num(), iovecs.data(), iovecs.size()));

      if(bytes_written == 0 and buffer.size() != 0) {
        throw runtime_error("write returned 0 given non-empty input buffer");
      }
      if(bytes_written > ssize_t(buffer.size())) {
        throw runtime_error("write wrote more than length of input buffer");
      }
      register_write();
      buffer.remove_prefix(bytes_written);
      total_bytes_written += bytes_written;
    }while (write_all and buffer.size());

    return total_bytes_written;
  }
  size_t write(const char *str, const bool write_all = true) {
    return write(BufferViewList(str), write_all);
  }
  size_t write(const std::string &str, const bool write_all = true) {
    return write(BufferViewList(str), write_all);
  }
};
```

From the code above, we have known that for `string` and `c-string`, we all
convert them into the `BufferViewList` class. Now, we don't consider
how `BufferViewList::as_iovecs()` works. First, we look at the `iovec`:

```c
struct iovec {
  ptr_t iov_base;
  size_t iov_len;
}
```

So we could make an array of `iovec` for system call `writev`. Now
let's what `BufferViewList::as_iovecs()` does:

```c++
vector<iovec> BufferViewList::as_iovecs() const {
  vector<iovec> ret;
  ret.reserve(_views.size());
  for (const auto &x : _views) {
    ret.push_back({const_cast<char *>(x.data()), x.size()});
  }
  return ret;
}
```

Well, it converts the `_views` to be a vector of the struct `iovec`.
What a beautiful abstraction!

## Address

Well, for `Address` class, the content is tedious. Because it does the wrapper
for the system call. And it is not difficult to understand.

## EventLoop

Well, I think the most interesting part is the `EventLoop` class, which
waits for events on file descriptors and executing callbacks.

```c++
class EventLoop {
public:
  enum class Direction: short {
    In = POLLIN,
    Out = POLLOUT
  }
private:
  using CallbackT = std::function<void(void)>;
  using InterestT = std::function<bool(void)>;

  class Rule {
  public:
    FileDescriptor fd;
    Direction direction;
    CallbackT callback;
    InterestT interest;
    CallbackT cancel;

    unsigned int service_count() const {
      return direction == Direction::In ? fd.read_count() : fd.write_count();
    }
  }
  std::list<Rule> _rules{};
}
```

At this time, we have known that `EventLoop` abstracts the `Rule` class,
which holds the activity.

```c++
class EventLoop {
...
public:
  enum class Result {
    Success,
    Timeout,
    Exit
  }
  void add_rule(const FileDescriptor &fd,
                const Direction direction,
                const CallbackT &callback,
                const InterestT &interest = [] { return true; },
                const CallbackT &cancel = [] {}) {
    _rules.push_back({fd.duplicate(), direction, callback, interest, cancel});
  }
};
```

Well, the logic is simple, when we want to do a new activity, we add a new rule.

How should we do the loop? Well, this is the multiplexing I/O,
the code use `poll` to deal with that, first, it creates the `vector<pollfd>` to
wrap the array of the `pollfd`. How we process the event loop,
the idea is simple, we just iterate the `_rules`:

+ When the event is done, we delete it from the `list<_rules>`.
+ When the event's file descriptor is closed, we do the above.
+ When event's `interest()` is true, add it to the `vector<pollfd>`.

Now we use system call `poll` to do I/O. Then process the I/O result.

```c++
EventLoop::Result EventLoop::wait_next_event(const int timeout_ms) {
  vector<pollfd> pollfds{};
  pollfds.reserve(_rules.size());
  bool something_to_poll = false;
  for(auto it = _rules.cbegin(); it != _rules.cend();) {
    const auto &this_rule = *it;
    if (this_rule.direction == Direction::In && this_rule.fd.eof()) {
      this_rule.cancel();
      it = _rules.erase(it);
      continue;
    }
    if(this_rule.fd.closed()) {
      this_rule.cancel();
      it = _rules.erase(it);
      continue;
    }
    if (this_rule.interest()) {
      pollfds.push_back({this_rule.fd.fd_num(), static_cast<short>(this_rule.direction), 0});
        something_to_poll = true;
      } else {
      pollfds.push_back({this_rule.fd.fd_num(), 0, 0});
      }
    ++it;
    if(not something_to_poll) {
      return Result::Exit;
    }

    try {
      if (0 == SystemCall("poll", ::poll(pollfds.data(), pollfds.size(), timeout_ms))) {
        return Result::Timeout;
      }
    } catch (unix_error const &e) {
      if (e.code().value() == EINTR) {
        return Result::Exit;
      }
    }

    // do the polling
  }
}
```
## Socket

Well, `Socket` is also a file descriptor, but there are many more details.
But I don't think it is meaningful to talk about the source code.
Because file descriptor is a wonderful wrapper which contains the
most trivial part.
