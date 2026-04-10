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

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

StealthConfig make_valid_config() {
  MockRng rng(1);
  return StealthConfig::default_config(rng);
}

TEST(StealthConfigRecordPaddingFailClosed, RejectsOutOfBoundsSmallRecordThreshold) {
  auto config = make_valid_config();
  config.record_padding_policy.small_record_threshold = 199;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("record_padding_policy.small_record_threshold is out of allowed bounds", status.message().c_str());
}

TEST(StealthConfigRecordPaddingFailClosed, RejectsOutOfRangeSmallRecordFraction) {
  auto config = make_valid_config();
  config.record_padding_policy.small_record_max_fraction = 1.1;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("record_padding_policy.small_record_max_fraction must be within [0, 1]", status.message().c_str());
}

TEST(StealthConfigRecordPaddingFailClosed, RejectsZeroBudgetWindow) {
  auto config = make_valid_config();
  config.record_padding_policy.small_record_window_size = 0;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("record_padding_policy.small_record_window_size is out of allowed bounds", status.message().c_str());
}

TEST(StealthConfigRecordPaddingFailClosed, DecoratorFactoryRejectsInvalidRecordPaddingPolicyWithoutAbort) {
  auto config = make_valid_config();
  config.record_padding_policy.target_tolerance = -1;

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("record_padding_policy.target_tolerance is out of allowed bounds", result.error().message().c_str());
}

}  // namespace