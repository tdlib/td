// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/StealthRuntimeParams.h"

namespace td {

class ConnectionLifecyclePolicy final {
 public:
  static double sample_active_connection_retire_at(double opened_at,
                                                   const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy,
                                                   uint32 random_value);

  static bool is_active_connection_retire_due(double retire_at, double now);
};

}  // namespace td