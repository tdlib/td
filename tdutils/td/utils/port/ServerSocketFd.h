//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/Fd.h"
#include "td/utils/port/SocketFd.h"

#include "td/utils/Slice.h"
#include "td/utils/Status.h"

namespace td {

class ServerSocketFd {
 public:
  ServerSocketFd() = default;
  ServerSocketFd(const ServerSocketFd &) = delete;
  ServerSocketFd &operator=(const ServerSocketFd &) = delete;
  ServerSocketFd(ServerSocketFd &&) = default;
  ServerSocketFd &operator=(ServerSocketFd &&) = default;

  static Result<ServerSocketFd> open(int32 port, CSlice addr = CSlice("0.0.0.0")) TD_WARN_UNUSED_RESULT;

  const Fd &get_fd() const;
  Fd &get_fd();
  int32 get_flags() const;
  Status get_pending_error() TD_WARN_UNUSED_RESULT;

  Result<SocketFd> accept() TD_WARN_UNUSED_RESULT;

  void close();
  bool empty() const;

 private:
  Fd fd_;

  Status init(int32 port, CSlice addr) TD_WARN_UNUSED_RESULT;
};

}  // namespace td
