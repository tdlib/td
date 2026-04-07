//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;

TEST(StealthConfigDrsWeightOverflowFailClosed, RejectsPhaseWeightSumThatWouldOverflowSelectionAccumulator) {
  MockRng rng(17);
  auto config = StealthConfig::default_config(rng);

  config.drs_policy.slow_start.bins.clear();
  config.drs_policy.slow_start.bins.reserve(65538);
  for (int i = 0; i < 65538; i++) {
    config.drs_policy.slow_start.bins.push_back(RecordSizeBin{900, 900, 65535});
  }

  ASSERT_TRUE(config.validate().is_error());
}

}  // namespace