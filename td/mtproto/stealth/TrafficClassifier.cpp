// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/TrafficClassifier.h"

namespace td {
namespace mtproto {
namespace stealth {

namespace {

constexpr size_t kMinBulkThresholdBytes = 512;
constexpr size_t kMaxBulkThresholdBytes = 1 << 20;
constexpr size_t kMessageIdBytes = sizeof(int64);

size_t sanitize_bulk_threshold_bytes(size_t bulk_threshold_bytes) noexcept {
  if (bulk_threshold_bytes < kMinBulkThresholdBytes || bulk_threshold_bytes > kMaxBulkThresholdBytes) {
    return kDefaultBulkThresholdBytes;
  }
  return bulk_threshold_bytes;
}

bool is_bulk_ack_flood(size_t ack_count, size_t bulk_threshold_bytes) noexcept {
  auto ack_bulk_threshold = (bulk_threshold_bytes + kMessageIdBytes - 1) / kMessageIdBytes;
  return ack_count >= ack_bulk_threshold;
}

}  // namespace

TrafficHint classify_session_traffic_hint(bool has_salt, int64 ping_id, size_t query_count, size_t query_bytes,
                                          size_t ack_count, bool has_service_queries, bool destroy_auth_key,
                                          size_t bulk_threshold_bytes) noexcept {
  auto sanitized_bulk_threshold = sanitize_bulk_threshold_bytes(bulk_threshold_bytes);
  if (!has_salt) {
    return TrafficHint::AuthHandshake;
  }

  if (ack_count != 0 && is_bulk_ack_flood(ack_count, sanitized_bulk_threshold)) {
    return TrafficHint::BulkData;
  }

  const bool has_user_queries = query_count != 0;
  const bool pure_control = !has_user_queries && !has_service_queries && !destroy_auth_key;
  if (pure_control && (ping_id != 0 || ack_count != 0)) {
    return TrafficHint::Keepalive;
  }

  if (has_user_queries && query_bytes >= sanitized_bulk_threshold) {
    return TrafficHint::BulkData;
  }

  return TrafficHint::Interactive;
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td