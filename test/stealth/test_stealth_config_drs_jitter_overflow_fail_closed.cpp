// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

constexpr td::int32 kMaxSafeLocalJitter = (std::numeric_limits<td::int32>::max() - 1) / 2;

StealthConfig make_valid_config() {
  MockRng rng(31);
  return StealthConfig::default_config(rng);
}

TEST(StealthConfigDrsJitterOverflowFailClosed, AcceptsLargestRepresentableLocalJitter) {
  auto config = make_valid_config();
  config.drs_policy.slow_start.local_jitter = kMaxSafeLocalJitter;

  auto status = config.validate();
  ASSERT_TRUE(status.is_ok());
}

TEST(StealthConfigDrsJitterOverflowFailClosed, RejectsLocalJitterThatWouldOverflowSamplingRange) {
  auto config = make_valid_config();
  config.drs_policy.slow_start.local_jitter = kMaxSafeLocalJitter + 1;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("drs_policy.slow_start local_jitter exceeds supported range", status.message().c_str());
}

TEST(StealthConfigDrsJitterOverflowFailClosed, DecoratorFactoryRejectsOverflowingLocalJitterWithoutAbort) {
  auto config = make_valid_config();
  config.drs_policy.steady_state.local_jitter = std::numeric_limits<td::int32>::max();

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(29), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("drs_policy.steady_state local_jitter exceeds supported range", result.error().message().c_str());
}

}  // namespace
