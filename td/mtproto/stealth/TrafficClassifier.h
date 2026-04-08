//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/stealth/Interfaces.h"

namespace td {
namespace mtproto {
namespace stealth {

constexpr size_t kDefaultBulkThresholdBytes = 8192;

TrafficHint classify_session_traffic_hint(bool has_salt, int64 ping_id, size_t query_count, size_t query_bytes,
                                          size_t ack_count, bool has_service_queries, bool destroy_auth_key,
                                          size_t bulk_threshold_bytes) noexcept;

}  // namespace stealth
}  // namespace mtproto
}  // namespace td