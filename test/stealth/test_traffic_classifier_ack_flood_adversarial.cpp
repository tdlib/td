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

TEST(TrafficClassifierAckFloodAdversarial, SmallAckOnlyControlStaysKeepalive) {
  ASSERT_EQ(TrafficHint::Keepalive, classify_session_traffic_hint(true, 0, 0, 0, 1023, false, false, 8192));
}

TEST(TrafficClassifierAckFloodAdversarial, AckOnlyFloodAtBulkThresholdEscalatesToBulkData) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 0, 0, 1024, false, false, 8192));
}

TEST(TrafficClassifierAckFloodAdversarial, PingCannotSmuggleAckFloodThroughKeepaliveBypass) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 1, 0, 0, 2048, false, false, 8192));
}

TEST(TrafficClassifierAckFloodAdversarial, InvalidThresholdStillFailsClosedForAckFloods) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 0, 0, 1024, false, false, 1));
}

}  // namespace