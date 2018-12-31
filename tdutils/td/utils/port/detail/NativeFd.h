//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#include "td/utils/common.h"
#include "td/utils/MovableValue.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class NativeFd {
 public:
#if TD_PORT_POSIX
  using Fd = int;
  using Socket = int;
#elif TD_PORT_WINDOWS
  using Fd = HANDLE;
  using Socket = SOCKET;
#endif
  NativeFd() = default;
  NativeFd(NativeFd &&) = default;
  NativeFd &operator=(NativeFd &&) = default;
  explicit NativeFd(Fd fd);
  NativeFd(Fd fd, bool nolog);
#if TD_PORT_WINDOWS
  explicit NativeFd(Socket socket);
#endif
  NativeFd(const NativeFd &) = delete;
  NativeFd &operator=(const NativeFd &) = delete;
  ~NativeFd();

  explicit operator bool() const;

  static Fd empty_fd();

  Fd fd() const;
  Socket socket() const;

  Status set_is_blocking(bool is_blocking) const;

  Status set_is_blocking_unsafe(bool is_blocking) const;  // may drop other Fd flags on non-Windows

  Status duplicate(const NativeFd &to) const;

  void close();
  Fd release();

 private:
#if TD_PORT_POSIX
  MovableValue<Fd, -1> fd_;
#elif TD_PORT_WINDOWS
  MovableValue<Fd, INVALID_HANDLE_VALUE> fd_;
  bool is_socket_{false};
#endif
};

StringBuilder &operator<<(StringBuilder &sb, const NativeFd &fd);

}  // namespace td
