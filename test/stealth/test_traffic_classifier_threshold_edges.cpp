// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/TrafficClassifier.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::classify_session_traffic_hint;
using td::mtproto::stealth::TrafficHint;

constexpr size_t kMinValidBulkThresholdBytes = 512;
constexpr size_t kMaxValidBulkThresholdBytes = 1 << 20;
constexpr size_t kAckBytesPerMessageId = sizeof(td::int64);

size_t ack_count_ceil_boundary(size_t bulk_threshold_bytes) {
  return (bulk_threshold_bytes + kAckBytesPerMessageId - 1) / kAckBytesPerMessageId;
}

TEST(TrafficClassifierThresholdEdges, ValidMinimumThresholdUsesExactAckCeilBoundary) {
  auto threshold_ack_count = ack_count_ceil_boundary(kMinValidBulkThresholdBytes);

  ASSERT_EQ(TrafficHint::Keepalive, classify_session_traffic_hint(true, 0, 0, 0, threshold_ack_count - 1, false, false,
                                                                  kMinValidBulkThresholdBytes));
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 0, 0, threshold_ack_count, false, false,
                                                                 kMinValidBulkThresholdBytes));
}

TEST(TrafficClassifierThresholdEdges, ValidMaximumThresholdUsesExactAckCeilBoundary) {
  auto threshold_ack_count = ack_count_ceil_boundary(kMaxValidBulkThresholdBytes);

  ASSERT_EQ(TrafficHint::Keepalive, classify_session_traffic_hint(true, 0, 0, 0, threshold_ack_count - 1, false, false,
                                                                  kMaxValidBulkThresholdBytes));
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 0, 0, threshold_ack_count, false, false,
                                                                 kMaxValidBulkThresholdBytes));
}

TEST(TrafficClassifierThresholdEdges, ValidMinimumThresholdDoesNotFailClosedToDefaultRange) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 1, kMinValidBulkThresholdBytes, 0, false,
                                                                 false, kMinValidBulkThresholdBytes));
}

TEST(TrafficClassifierThresholdEdges, ValidMaximumThresholdDoesNotFailClosedToDefaultRange) {
  ASSERT_EQ(TrafficHint::Interactive,
            classify_session_traffic_hint(true, 0, 1, 8192, 0, false, false, kMaxValidBulkThresholdBytes));
}

TEST(TrafficClassifierThresholdEdges, MixedServiceControlCannotDowngradeMinimumThresholdAckFlood) {
  auto threshold_ack_count = ack_count_ceil_boundary(kMinValidBulkThresholdBytes);

  ASSERT_EQ(TrafficHint::BulkData,
            classify_session_traffic_hint(true, 1, 0, 0, threshold_ack_count, true, true, kMinValidBulkThresholdBytes));
}

}  // namespace