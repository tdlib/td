// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include <vector>

namespace td {

class ConnectionFlowController final {
 public:
  double get_wakeup_at(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);
  bool allows_rotation_at(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);
  void on_connect_started(double now, const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);

 private:
  void prune_old_attempts(double now);

  std::vector<double> recent_connect_attempts_;
  double last_connect_started_at_{-1.0};
};

}  // namespace td