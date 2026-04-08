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

TEST(TrafficClassifierAckMixAdversarial, SmallUserQueryCannotSmuggleAckFloodAsInteractive) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 0, 1, 256, 2048, false, false, 8192));
}

TEST(TrafficClassifierAckMixAdversarial, PingCannotSmuggleMixedAckFloodPastBulkClassification) {
  ASSERT_EQ(TrafficHint::BulkData, classify_session_traffic_hint(true, 1, 1, 256, 2048, false, false, 8192));
}

}  // namespace