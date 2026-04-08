//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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