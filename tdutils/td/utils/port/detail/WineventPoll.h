//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#ifdef TD_POLL_WINEVENT

#include "td/utils/common.h"
#include "td/utils/Context.h"
#include "td/utils/port/detail/NativeFd.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/PollBase.h"
#include "td/utils/port/thread.h"

namespace td {
namespace detail {

class IOCP final : public Context<IOCP> {
 public:
  IOCP() = default;
  IOCP(const IOCP &) = delete;
  IOCP &operator=(const IOCP &) = delete;
  IOCP(IOCP &&) = delete;
  IOCP &operator=(IOCP &&) = delete;
  ~IOCP();

  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_iocp(Result<size_t> r_size, OVERLAPPED *overlapped) = 0;
  };

  void init();
  void subscribe(const NativeFd &fd, Callback *callback);
  void post(size_t size, Callback *callback, OVERLAPPED *overlapped);
  void loop();
  void interrupt_loop();
  void clear();

 private:
  NativeFd iocp_handle_;
  std::vector<td::thread> workers_;
};

class WineventPoll final : public PollBase {
 public:
  WineventPoll() = default;
  WineventPoll(const WineventPoll &) = delete;
  WineventPoll &operator=(const WineventPoll &) = delete;
  WineventPoll(WineventPoll &&) = delete;
  WineventPoll &operator=(WineventPoll &&) = delete;
  ~WineventPoll() override = default;

  void init() override;

  void clear() override;

  void subscribe(PollableFd fd, PollFlags flags) override;

  void unsubscribe(PollableFdRef fd) override;

  void unsubscribe_before_close(PollableFdRef fd) override;

  void run(int timeout_ms) override;

  static bool is_edge_triggered() {
    return true;
  }
};

}  // namespace detail
}  // namespace td

#endif
