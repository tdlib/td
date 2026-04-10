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

TEST(StealthConfigBidirectionalFailClosed, RejectsZeroSmallResponseThreshold) {
  auto config = make_valid_config();
  config.bidirectional_correlation_policy.small_response_threshold_bytes = 0;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("bidirectional_correlation_policy.small_response_threshold_bytes is out of allowed bounds",
               status.message().c_str());
}

TEST(StealthConfigBidirectionalFailClosed, RejectsTooSmallNextRequestFloor) {
  auto config = make_valid_config();
  config.bidirectional_correlation_policy.next_request_min_payload_cap = 255;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("bidirectional_correlation_policy.next_request_min_payload_cap is out of allowed bounds",
               status.message().c_str());
}

TEST(StealthConfigBidirectionalFailClosed, RejectsInvertedJitterRange) {
  auto config = make_valid_config();
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = 12.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = 11.0;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("bidirectional_correlation_policy.post_response_delay_jitter_ms_min must not exceed max",
               status.message().c_str());
}

TEST(StealthConfigBidirectionalFailClosed, DecoratorFactoryRejectsInvalidBidirectionalPolicyWithoutAbort) {
  auto config = make_valid_config();
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_min = 12.0;
  config.bidirectional_correlation_policy.post_response_delay_jitter_ms_max = 11.0;

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(7), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("bidirectional_correlation_policy.post_response_delay_jitter_ms_min must not exceed max",
               result.error().message().c_str());
}

}  // namespace