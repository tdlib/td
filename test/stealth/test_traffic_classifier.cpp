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

TEST(TrafficClassifier, NoSaltIsAuthHandshake) {
  ASSERT_EQ(TrafficHint::AuthHandshake, classify_session_traffic_hint(false, 0, 0, 0, 0, false, false, 8192));
}

TEST(TrafficClassifier, SaltAloneDoesNotLookLikeAuthHandshake) {
  ASSERT_NE(TrafficHint::AuthHandshake, classify_session_traffic_hint(true, 0, 0, 0, 0, false, false, 8192));
}

TEST(TrafficClassifier, PurePingOrAckIsKeepalive) {
  ASSERT_EQ(TrafficHint::Keepalive, classify_session_traffic_hint(true, 1, 0, 0, 0, false, false, 8192));
  ASSERT_EQ(TrafficHint::Keepalive, classify_session_traffic_hint(true, 0, 0, 0, 1, false, false, 8192));
}

TEST(TrafficClassifier, MixedPingWithUserQueriesDoesNotBypassAsKeepalive) {
  ASSERT_NE(TrafficHint::Keepalive, classify_session_traffic_hint(true, 1, 2, 1200, 0, false, false, 8192));
}

TEST(TrafficClassifier, LargeUserBurstIsBulkData) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 2, 8192, 0, false, false, 8192));
}

TEST(TrafficClassifier, ServiceTrafficPreventsKeepaliveBypass) {
  ASSERT_NE(TrafficHint::Keepalive, classify_session_traffic_hint(true, 0, 0, 0, 1, true, false, 8192));
}

TEST(TrafficClassifier, DestroyAuthKeyPreventsKeepaliveBypass) {
  ASSERT_NE(TrafficHint::Keepalive, classify_session_traffic_hint(true, 1, 0, 0, 0, false, true, 8192));
}

TEST(TrafficClassifier, InvalidBulkThresholdFailsClosedToDefaultRange) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 1, 8192, 0, false, false, 0));
}

}  // namespace