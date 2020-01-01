//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#ifdef TD_POLL_SELECT

#include "td/utils/common.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/PollBase.h"
#include "td/utils/port/PollFlags.h"

#include <sys/select.h>

namespace td {
namespace detail {

class Select final : public PollBase {
 public:
  Select() = default;
  Select(const Select &) = delete;
  Select &operator=(const Select &) = delete;
  Select(Select &&) = delete;
  Select &operator=(Select &&) = delete;
  ~Select() override = default;

  void init() override;

  void clear() override;

  void subscribe(PollableFd fd, PollFlags flags) override;

  void unsubscribe(PollableFdRef fd) override;

  void unsubscribe_before_close(PollableFdRef fd) override;

  void run(int timeout_ms) override;

  static bool is_edge_triggered() {
    return false;
  }

 private:
  struct FdInfo {
    PollableFd fd;
    PollFlags flags;
  };
  vector<FdInfo> fds_;
  fd_set all_fd_;
  fd_set read_fd_;
  fd_set write_fd_;
  fd_set except_fd_;
  int max_fd_;
};

}  // namespace detail
}  // namespace td

#endif
