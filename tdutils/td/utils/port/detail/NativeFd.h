//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
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
  using Raw = int;
#elif TD_PORT_WINDOWS
  using Raw = HANDLE;
#endif
  NativeFd() = default;
  NativeFd(NativeFd &&) = default;
  NativeFd &operator=(NativeFd &&) = default;
  explicit NativeFd(Raw raw);
  NativeFd &operator=(const NativeFd &) = delete;
  NativeFd(Raw raw, bool nolog);
#if TD_PORT_WINDOWS
  explicit NativeFd(SOCKET raw);
#endif
  ~NativeFd();

  explicit operator bool() const;

  static Raw empty_raw();

  Raw raw() const;
  Raw fd() const;
#if TD_PORT_WINDOWS
  Raw io_handle() const;
  SOCKET socket() const;
#elif TD_PORT_POSIX
  Raw socket() const;
#endif

  Status set_is_blocking(bool is_blocking) const;

  Status set_is_blocking_unsafe(bool is_blocking) const;  // may drop other Fd flags on non-Windows

  Status duplicate(const NativeFd &to) const;

  void close();
  Raw release();

 private:
#if TD_PORT_POSIX
  MovableValue<Raw, -1> fd_;
#elif TD_PORT_WINDOWS
  MovableValue<Raw, INVALID_HANDLE_VALUE> fd_;
  bool is_socket_{false};
#endif
};

StringBuilder &operator<<(StringBuilder &sb, const NativeFd &fd);

}  // namespace td
