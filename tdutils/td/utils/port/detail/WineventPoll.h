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
#include "td/utils/port/Fd.h"
#include "td/utils/port/PollBase.h"

namespace td {
namespace detail {

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

  void subscribe(const Fd &fd, Fd::Flags flags) override;

  void unsubscribe(const Fd &fd) override;

  void unsubscribe_before_close(const Fd &fd) override;

  void run(int timeout_ms) override;

 private:
  struct FdInfo {
    Fd fd_ref;
    Fd::Flags flags;
  };
  vector<FdInfo> fds_;
};

}  // namespace detail
}  // namespace td

#endif
