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

using td::mtproto::stealth::ChaffPolicy;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_exact_record_model(td::int32 target_bytes) {
  return DrsPhaseModel{{RecordSizeBin{target_bytes, target_bytes, 1}}, 1, 0};
}

StealthConfig make_valid_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.chaff_policy = ChaffPolicy{};
  config.chaff_policy.enabled = true;
  config.chaff_policy.idle_threshold_ms = 5000;
  config.chaff_policy.min_interval_ms = 10.0;
  config.chaff_policy.max_bytes_per_minute = 4096;
  config.chaff_policy.record_model = make_exact_record_model(320);
  return config;
}

TEST(StealthConfigChaffNonFiniteFailClosed, RejectsNaNMinimumIntervalWhenEnabled) {
  auto config = make_valid_config();
  config.chaff_policy.min_interval_ms = std::numeric_limits<double>::quiet_NaN();

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("chaff_policy.min_interval_ms must be finite and non-negative", status.message().c_str());
}

TEST(StealthConfigChaffNonFiniteFailClosed, RejectsInfiniteMinimumIntervalWhenEnabled) {
  auto config = make_valid_config();
  config.chaff_policy.min_interval_ms = std::numeric_limits<double>::infinity();

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("chaff_policy.min_interval_ms must be finite and non-negative", status.message().c_str());
}

TEST(StealthConfigChaffNonFiniteFailClosed, DecoratorFactoryRejectsNonFiniteIntervalWithoutAbort) {
  auto config = make_valid_config();
  config.chaff_policy.min_interval_ms = std::numeric_limits<double>::infinity();

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("chaff_policy.min_interval_ms must be finite and non-negative", result.error().message().c_str());
}

}  // namespace