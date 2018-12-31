//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#ifdef TD_PORT_WINDOWS

#include "td/utils/common.h"
#include "td/utils/Context.h"
#include "td/utils/port/detail/NativeFd.h"
#include "td/utils/port/thread.h"
#include "td/utils/Status.h"

namespace td {
namespace detail {

class Iocp final : public Context<Iocp> {
 public:
  Iocp() = default;
  Iocp(const Iocp &) = delete;
  Iocp &operator=(const Iocp &) = delete;
  Iocp(Iocp &&) = delete;
  Iocp &operator=(Iocp &&) = delete;
  ~Iocp();

  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_iocp(Result<size_t> r_size, WSAOVERLAPPED *overlapped) = 0;
  };

  void init();
  void subscribe(const NativeFd &fd, Callback *callback);
  void post(size_t size, Callback *callback, WSAOVERLAPPED *overlapped);
  void loop();
  void interrupt_loop();
  void clear();

 private:
  NativeFd iocp_handle_;
  std::vector<td::thread> workers_;
};

}  // namespace detail
}  // namespace td

#endif
