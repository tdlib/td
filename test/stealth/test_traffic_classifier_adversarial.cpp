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

TEST(TrafficClassifierAdversarial, MixedPingWithSmallUserQueriesStaysInteractive) {
  ASSERT_EQ(TrafficHint::Interactive, classify_session_traffic_hint(true, 1, 1, 256, 0, false, false, 8192));
}

TEST(TrafficClassifierAdversarial, PureControlWithoutPingOrAckStaysInteractive) {
  ASSERT_EQ(TrafficHint::Interactive, classify_session_traffic_hint(true, 0, 0, 0, 0, false, false, 8192));
}

TEST(TrafficClassifierAdversarial, ThresholdBelowMinimumFailsClosedToDefaultClassifierRange) {
  ASSERT_EQ(TrafficHint::Interactive, classify_session_traffic_hint(true, 0, 1, 600, 0, false, false, 1));
}

TEST(TrafficClassifierAdversarial, ThresholdAboveMaximumFailsClosedToDefaultClassifierRange) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 1, 9000, 0, false, false, (1u << 20) + 1u));
}

}  // namespace