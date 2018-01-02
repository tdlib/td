//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/net/HttpQuery.h"
#include "td/net/HttpReader.h"

#include "td/utils/buffer.h"
#include "td/utils/BufferedFd.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class FdInterface {
 public:
  FdInterface() = default;
  FdInterface(const FdInterface &) = delete;
  FdInterface &operator=(const FdInterface &) = delete;
  FdInterface(FdInterface &&) = default;
  FdInterface &operator=(FdInterface &&) = default;
  virtual ~FdInterface() = default;
  virtual const Fd &get_fd() const = 0;
  virtual Fd &get_fd() = 0;
  virtual int32 get_flags() const = 0;
  virtual Status get_pending_error() TD_WARN_UNUSED_RESULT = 0;

  virtual Result<size_t> write(Slice slice) TD_WARN_UNUSED_RESULT = 0;
  virtual Result<size_t> read(MutableSlice slice) TD_WARN_UNUSED_RESULT = 0;

  virtual void close() = 0;
  virtual bool empty() const = 0;
};

template <class FdT>
class FdToInterface : public FdInterface {
 public:
  FdToInterface() = default;
  explicit FdToInterface(FdT fd) : fd_(std::move(fd)) {
  }

  const Fd &get_fd() const final {
    return fd_.get_fd();
  }
  Fd &get_fd() final {
    return fd_.get_fd();
  }
  int32 get_flags() const final {
    return fd_.get_flags();
  }
  Status get_pending_error() final TD_WARN_UNUSED_RESULT {
    return fd_.get_pending_error();
  }

  Result<size_t> write(Slice slice) final TD_WARN_UNUSED_RESULT {
    return fd_.write(slice);
  }
  Result<size_t> read(MutableSlice slice) final TD_WARN_UNUSED_RESULT {
    return fd_.read(slice);
  }

  void close() final {
    fd_.close();
  }
  bool empty() const final {
    return fd_.empty();
  }

 private:
  FdT fd_;
};

template <class FdT>
std::unique_ptr<FdInterface> make_fd_interface(FdT fd) {
  return make_unique<FdToInterface<FdT>>(std::move(fd));
}

class FdProxy {
 public:
  FdProxy() = default;
  explicit FdProxy(std::unique_ptr<FdInterface> fd) : fd_(std::move(fd)) {
  }

  const Fd &get_fd() const {
    return fd_->get_fd();
  }
  Fd &get_fd() {
    return fd_->get_fd();
  }
  int32 get_flags() const {
    return fd_->get_flags();
  }
  Status get_pending_error() TD_WARN_UNUSED_RESULT {
    return fd_->get_pending_error();
  }

  Result<size_t> write(Slice slice) TD_WARN_UNUSED_RESULT {
    return fd_->write(slice);
  }
  Result<size_t> read(MutableSlice slice) TD_WARN_UNUSED_RESULT {
    return fd_->read(slice);
  }

  void close() {
    fd_->close();
  }
  bool empty() const {
    return fd_->empty();
  }

 private:
  std::unique_ptr<FdInterface> fd_;
};

template <class FdT>
FdProxy make_fd_proxy(FdT fd) {
  return FdProxy(make_fd_interface(std::move(fd)));
}

namespace detail {
class HttpConnectionBase : public Actor {
 public:
  void write_next(BufferSlice buffer);
  void write_ok();
  void write_error(Status error);

 protected:
  enum class State { Read, Write, Close };
  template <class FdT>
  HttpConnectionBase(State state, FdT fd, size_t max_post_size, size_t max_files, int32 idle_timeout)
      : HttpConnectionBase(state, make_fd_proxy(std::move(fd)), max_post_size, max_files, idle_timeout) {
  }
  HttpConnectionBase(State state, FdProxy fd, size_t max_post_size, size_t max_files, int32 idle_timeout);

 private:
  using StreamConnection = BufferedFd<FdProxy>;
  State state_;
  StreamConnection stream_connection_;
  size_t max_post_size_;
  size_t max_files_;
  int32 idle_timeout_;
  HttpReader reader_;
  HttpQueryPtr current_query_;
  bool close_after_write_ = false;

  void live_event();

  void start_up() override;
  void tear_down() override;
  void timeout_expired() override;
  void loop() override;

  virtual void on_query(HttpQueryPtr) = 0;
  virtual void on_error(Status error) = 0;
};
}  // namespace detail
}  // namespace td
