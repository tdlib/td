//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include <vector>

namespace td {

class ConnectionFlowController final {
 public:
  double get_wakeup_at(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);
  void on_connect_started(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);

 private:
  void prune_old_attempts(double now);

  std::vector<double> recent_connect_attempts_;
  double last_connect_started_at_{-1.0};
};

}  // namespace td