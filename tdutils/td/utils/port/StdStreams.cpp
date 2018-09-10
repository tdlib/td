//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/port/StdStreams.h"

#include "td/utils/port/detail/NativeFd.h"

namespace td {

namespace {
template <class T>
FileFd create(T handle) {
  return FileFd::from_native_fd(NativeFd(handle, true));
}
}  // namespace
FileFd &Stdin() {
  static FileFd res = create(
#if TD_PORT_POSIX
      0
#elif TD_PORT_WINDOWS
      GetStdHandle(STD_INPUT_HANDLE)
#endif
  );
  return res;
}
FileFd &Stdout() {
  static FileFd res = create(
#if TD_PORT_POSIX
      1
#elif TD_PORT_WINDOWS
      GetStdHandle(STD_OUTPUT_HANDLE)
#endif
  );
  return res;
}
FileFd &Stderr() {
  static FileFd res = create(
#if TD_PORT_POSIX
      2
#elif TD_PORT_WINDOWS
      GetStdHandle(STD_ERROR_HANDLE)
#endif
  );
  return res;
}

#if TD_WINDOWS
namespace detail {
class BufferedStdinImpl {
 public:
  BufferedStdinImpl() {
    file_fd_ = FileFd::from_native_fd(NativeFd(Stdin().get_native_fd().raw()));
    read_thread_ = td::thread([this] { this->read_loop(); });
  }
  BufferedStdinImpl(const BufferedStdinImpl &) = delete;
  BufferedStdinImpl &operator=(const BufferedStdinImpl &) = delete;
  BufferedStdinImpl(BufferedStdinImpl &&) = delete;
  BufferedStdinImpl &operator=(BufferedStdinImpl &&) = delete;
  ~BufferedStdinImpl() {
    file_fd_.move_as_native_fd().release();
  }
  void close() {
    close_flag_ = true;
  }

  ChainBufferReader &input_buffer() {
    return reader_;
  }

  PollableFdInfo &get_poll_info() {
    return file_fd_.get_poll_info();
  }
  const PollableFdInfo &get_poll_info() const {
    return file_fd_.get_poll_info();
  }

  Result<size_t> flush_read(size_t max_read = std::numeric_limits<size_t>::max()) TD_WARN_UNUSED_RESULT {
    reader_.sync_with_writer();
    return reader_.size();
  }

 private:
  FileFd file_fd_;
  ChainBufferWriter writer_;
  ChainBufferReader reader_ = writer_.extract_reader();
  td::thread read_thread_;

  std::atomic<bool> close_flag_{false};

  void read_loop() {
    while (!close_flag_) {
      auto slice = writer_.prepare_append();
      auto size = file_fd_.read(slice).move_as_ok();
      writer_.confirm_append(size);
      file_fd_.get_poll_info().add_flags_from_poll(td::PollFlags::Read());
    }
    //TODO delete
  }
};
void BufferedStdinImplDeleter::operator()(BufferedStdinImpl *impl) {
  impl->close();
}
}  // namespace detail
#else
namespace detail {
class BufferedStdinImpl {
 public:
  BufferedStdinImpl() {
    file_fd_ = FileFd::from_native_fd(NativeFd(Stdin().get_native_fd().raw()));
    file_fd_.get_native_fd().set_is_blocking(false);
  }
  BufferedStdinImpl(const BufferedStdinImpl &) = delete;
  BufferedStdinImpl &operator=(const BufferedStdinImpl &) = delete;
  BufferedStdinImpl(BufferedStdinImpl &&) = delete;
  BufferedStdinImpl &operator=(BufferedStdinImpl &&) = delete;
  ~BufferedStdinImpl() {
    file_fd_.move_as_native_fd().release();
  }

  ChainBufferReader &input_buffer() {
    return reader_;
  }

  PollableFdInfo &get_poll_info() {
    return file_fd_.get_poll_info();
  }
  const PollableFdInfo &get_poll_info() const {
    return file_fd_.get_poll_info();
  }

  Result<size_t> flush_read(size_t max_read = std::numeric_limits<size_t>::max()) TD_WARN_UNUSED_RESULT {
    size_t result = 0;
    while (::td::can_read(*this) && max_read) {
      MutableSlice slice = writer_.prepare_append().truncate(max_read);
      TRY_RESULT(x, file_fd_.read(slice));
      slice.truncate(x);
      writer_.confirm_append(x);
      result += x;
      max_read -= x;
    }
    if (result) {
      reader_.sync_with_writer();
    }
    return result;
  }

 private:
  FileFd file_fd_;
  ChainBufferWriter writer_;
  ChainBufferReader reader_ = writer_.extract_reader();
};
void BufferedStdinImplDeleter::operator()(BufferedStdinImpl *impl) {
  delete impl;
}
}  // namespace detail
#endif

BufferedStdin::BufferedStdin() : impl_(std::make_unique<detail::BufferedStdinImpl>().release()) {
}
BufferedStdin::BufferedStdin(BufferedStdin &&) = default;
BufferedStdin &BufferedStdin::operator=(BufferedStdin &&) = default;
BufferedStdin::~BufferedStdin() = default;

ChainBufferReader &BufferedStdin::input_buffer() {
  return impl_->input_buffer();
}
PollableFdInfo &BufferedStdin::get_poll_info() {
  return impl_->get_poll_info();
}
const PollableFdInfo &BufferedStdin::get_poll_info() const {
  return impl_->get_poll_info();
}
Result<size_t> BufferedStdin::flush_read(size_t max_read) {
  return impl_->flush_read(max_read);
}

}  // namespace td
