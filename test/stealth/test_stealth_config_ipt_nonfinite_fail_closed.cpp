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

#include <limits>

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

StealthConfig make_valid_config() {
  MockRng rng(31);
  return StealthConfig::default_config(rng);
}

TEST(StealthConfigIptNonFiniteFailClosed, RejectsNonFiniteBurstParameters) {
  auto config = make_valid_config();
  config.ipt_params.burst_mu_ms = std::numeric_limits<double>::infinity();

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ipt_params.burst_mu_ms must be finite", status.message().c_str());

  config = make_valid_config();
  config.ipt_params.burst_sigma = std::numeric_limits<double>::quiet_NaN();

  status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ipt_params.burst_sigma must be finite and non-negative", status.message().c_str());
}

TEST(StealthConfigIptNonFiniteFailClosed, RejectsNonFiniteIdleAndProbabilityParameters) {
  auto config = make_valid_config();
  config.ipt_params.idle_scale_ms = std::numeric_limits<double>::infinity();

  auto status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ipt_params.idle_scale_ms must be finite and non-negative", status.message().c_str());

  config = make_valid_config();
  config.ipt_params.p_burst_stay = std::numeric_limits<double>::quiet_NaN();

  status = config.validate();
  ASSERT_TRUE(status.is_error());
  ASSERT_STREQ("ipt_params.p_burst_stay must be within [0, 1]", status.message().c_str());
}

TEST(StealthConfigIptNonFiniteFailClosed, DecoratorFactoryRejectsNonFiniteIptConfigWithoutAbort) {
  auto config = make_valid_config();
  config.ipt_params.p_idle_to_burst = std::numeric_limits<double>::infinity();

  auto result = StealthTransportDecorator::create(td::make_unique<RecordingTransport>(), config,
                                                  td::make_unique<MockRng>(37), td::make_unique<MockClock>());
  ASSERT_TRUE(result.is_error());
  ASSERT_STREQ("ipt_params.p_idle_to_burst must be within [0, 1]", result.error().message().c_str());
}

}  // namespace