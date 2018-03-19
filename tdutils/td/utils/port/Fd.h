//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#if TD_PORT_WINDOWS
#include "td/utils/port/IPAddress.h"

#include <memory>
#endif

#if TD_PORT_POSIX
#include <errno.h>

#include <atomic>
#include <type_traits>
#endif

namespace td {

class ObserverBase;

#if TD_PORT_WINDOWS
namespace detail {
class EventFdWindows;
}  // namespace detail
#endif

class Fd {
 public:
  // TODO: Close may be not enough
  // Sometimes descriptor is half-closed.

  enum Flag : int32 {
    Write = 0x001,
    Read = 0x002,
    Close = 0x004,
    Error = 0x008,
    All = Write | Read | Close | Error,
    None = 0
  };
  using Flags = int32;
  enum class Mode { Reference, Owner };

  Fd();
  Fd(const Fd &) = delete;
  Fd &operator=(const Fd &) = delete;
  Fd(Fd &&other);
  Fd &operator=(Fd &&other);
  ~Fd();

#if TD_PORT_POSIX
  Fd(int fd, Mode mode);
#endif
#if TD_PORT_WINDOWS
  static Fd create_file_fd(HANDLE handle);

  static Fd create_socket_fd(SOCKET sock);

  static Fd create_server_socket_fd(SOCKET sock, int socket_family);

  static Fd create_event_fd();
#endif

  Fd clone() const;

  static Fd &Stderr();
  static Fd &Stdout();
  static Fd &Stdin();

  static Status duplicate(const Fd &from, Fd &to);

  bool empty() const;

  const Fd &get_fd() const;
  Fd &get_fd();

  void set_observer(ObserverBase *observer);
  ObserverBase *get_observer() const;

  void close();

  void update_flags(Flags flags);

  Flags get_flags() const;

  bool has_pending_error() const;
  Status get_pending_error() TD_WARN_UNUSED_RESULT;

  Result<size_t> write(Slice slice) TD_WARN_UNUSED_RESULT;
  Result<size_t> read(MutableSlice slice) TD_WARN_UNUSED_RESULT;

  Status set_is_blocking(bool is_blocking);

#if TD_PORT_POSIX
  void update_flags_notify(Flags flags);
  void clear_flags(Flags flags);

  Result<size_t> write_unsafe(Slice slice) TD_WARN_UNUSED_RESULT;

  int get_native_fd() const;
  int move_as_native_fd();
#endif

#if TD_PORT_WINDOWS
  Result<Fd> accept() TD_WARN_UNUSED_RESULT;
  void connect(const IPAddress &addr);

  uint64 get_key() const;

  HANDLE get_read_event();
  HANDLE get_write_event();
  void on_read_event();
  void on_write_event();

  SOCKET get_native_socket() const;
  HANDLE get_io_handle() const;
#endif

 private:
  Mode mode_ = Mode::Owner;

#if TD_PORT_POSIX
  struct Info {
    std::atomic<int> refcnt;
    int32 flags;
    ObserverBase *observer;
  };
  struct InfoSet {
    InfoSet();
    Info &get_info(int32 id);

   private:
    static constexpr int MAX_FD = 1 << 18;
    Info fd_array_[MAX_FD];
  };
  static InfoSet fd_info_set_;

  static Fd stderr_;
  static Fd stdout_;
  static Fd stdin_;

  void update_flags_inner(int32 new_flags, bool notify_flag);
  Info *get_info();
  const Info *get_info() const;
  void clear_info();

  void close_ref();
  void close_own();

  int fd_ = -1;
#endif
#if TD_PORT_WINDOWS
  class FdImpl;

  enum class Type { Empty, EventFd, FileFd, StdinFileFd, SocketFd, ServerSocketFd };

  Fd(Type type, Mode mode, HANDLE handle);
  Fd(Type type, Mode mode, SOCKET sock, int socket_family);
  explicit Fd(std::shared_ptr<FdImpl> impl);

  friend class detail::EventFdWindows;  // for release and acquire

  void acquire();
  void release();

  std::shared_ptr<FdImpl> impl_;
#endif
};

#if TD_PORT_POSIX
template <class F>
auto skip_eintr(F &&f) {
  decltype(f()) res;
  static_assert(std::is_integral<decltype(res)>::value, "integral type expected");
  do {
    errno = 0;  // just in case
    res = f();
  } while (res < 0 && errno == EINTR);
  return res;
}
template <class F>
auto skip_eintr_cstr(F &&f) {
  char *res;
  do {
    errno = 0;  // just in case
    res = f();
  } while (res == nullptr && errno == EINTR);
  return res;
}
#endif

template <class FdT>
bool can_read(const FdT &fd) {
  return (fd.get_flags() & Fd::Read) != 0;
}

template <class FdT>
bool can_write(const FdT &fd) {
  return (fd.get_flags() & Fd::Write) != 0;
}

template <class FdT>
bool can_close(const FdT &fd) {
  return (fd.get_flags() & Fd::Close) != 0;
}

namespace detail {
#if TD_PORT_POSIX
Status set_native_socket_is_blocking(int fd, bool is_blocking);
#endif
#if TD_PORT_WINDOWS
Status set_native_socket_is_blocking(SOCKET fd, bool is_blocking);
#endif
}  // namespace detail

}  // namespace td
