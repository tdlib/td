//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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