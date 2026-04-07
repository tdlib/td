//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
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
  MockRng rng(17);
  return StealthConfig::default_config(rng);
}

TEST(StealthConfigIptFailClosed, RejectsZeroBurstDelayCap) {
  auto config = make_valid_config();
  config.ipt_params.burst_max_ms = 0.0;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ipt_params.burst_max_ms must be positive", status.message().c_str());
}

TEST(StealthConfigIptFailClosed, RejectsDegenerateIdleSupportAtHardCap) {
  auto config = make_valid_config();
  config.ipt_params.idle_scale_ms = config.ipt_params.idle_max_ms;

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ipt_params.idle_scale_ms must be below idle_max_ms", status.message().c_str());
}

TEST(StealthConfigIptFailClosed, DecoratorFactoryRejectsDegenerateIptConfigWithoutAbort) {
  auto config = make_valid_config();
  config.ipt_params.idle_scale_ms = config.ipt_params.idle_max_ms;

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(19), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("ipt_params.idle_scale_ms must be below idle_max_ms", result.error().message().c_str());
}

}  // namespace