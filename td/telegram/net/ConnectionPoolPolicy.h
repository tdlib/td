// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/stealth/StealthRuntimeParams.h"

namespace td {

class ConnectionPoolPolicy final {
 public:
  static double pooled_connection_retention_seconds(double default_retention_seconds,
                                                    const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);

  static bool is_pooled_connection_expired(double pooled_at, double now, double default_retention_seconds,
                                           const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy);
};

}  // namespace td