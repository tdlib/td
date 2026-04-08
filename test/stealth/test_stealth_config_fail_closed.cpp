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

TEST(StealthConfigFailClosed, RejectsOversizedRingCapacity) {
  auto config = make_valid_config();
  config.ring_capacity = StealthConfig::kMaxRingCapacity + 1;
  config.high_watermark = StealthConfig::kMaxRingCapacity;
  config.low_watermark = StealthConfig::kMaxRingCapacity - 1;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ring_capacity exceeds fail-closed maximum", status.message().c_str());
}

TEST(StealthConfigFailClosed, DecoratorFactoryRejectsInvalidConfigWithoutAbort) {
  auto config = make_valid_config();
  config.ring_capacity = StealthConfig::kMaxRingCapacity + 1;
  config.high_watermark = StealthConfig::kMaxRingCapacity;
  config.low_watermark = StealthConfig::kMaxRingCapacity - 1;

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("ring_capacity exceeds fail-closed maximum", result.error().message().c_str());
}

TEST(StealthConfigFailClosed, RejectsTooSmallBulkThresholdBytes) {
  auto config = make_valid_config();
  config.bulk_threshold_bytes = 511;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("bulk_threshold_bytes is out of allowed bounds", status.message().c_str());
}

TEST(StealthConfigFailClosed, RejectsTooLargeBulkThresholdBytes) {
  auto config = make_valid_config();
  config.bulk_threshold_bytes = (static_cast<size_t>(1) << 20) + 1;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("bulk_threshold_bytes is out of allowed bounds", status.message().c_str());
}

TEST(StealthConfigFailClosed, DecoratorFactoryRejectsInvalidBulkThresholdWithoutAbort) {
  auto config = make_valid_config();
  config.bulk_threshold_bytes = 511;

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("bulk_threshold_bytes is out of allowed bounds", result.error().message().c_str());
}

TEST(StealthConfigFailClosed, DecoratorFactoryRejectsMissingDependencies) {
  auto config = make_valid_config();

  auto missing_inner =
      StealthTransportDecorator::create(nullptr, config, td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(missing_inner.is_error());
  ASSERT_STREQ("inner transport must not be null", missing_inner.error().message().c_str());

  auto missing_rng = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config, nullptr,
                                                       td::make_unique<MockClock>());
  ASSERT_TRUE(missing_rng.is_error());
  ASSERT_STREQ("rng must not be null", missing_rng.error().message().c_str());

  auto missing_clock = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                         td::make_unique<MockRng>(7), nullptr);
  ASSERT_TRUE(missing_clock.is_error());
  ASSERT_STREQ("clock must not be null", missing_clock.error().message().c_str());
}

TEST(StealthConfigFailClosed, DecoratorFactoryConstructsValidatedInputs) {
  auto config = make_valid_config();

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_ok());
}

}  // namespace
