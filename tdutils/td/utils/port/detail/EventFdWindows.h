//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/port/config.h"

#ifdef TD_EVENTFD_WINDOWS

#include "td/utils/common.h"
#include "td/utils/port/EventFdBase.h"
#include "td/utils/port/Fd.h"
#include "td/utils/Status.h"

namespace td {
namespace detail {

class EventFdWindows final : public EventFdBase {
  Fd fd_;

 public:
  EventFdWindows() = default;

  void init() override;

  bool empty() override;

  void close() override;

  Status get_pending_error() override TD_WARN_UNUSED_RESULT;

  const Fd &get_fd() const override;
  Fd &get_fd() override;

  void release() override;

  void acquire() override;
};

}  // namespace detail
}  // namespace td

#endif
