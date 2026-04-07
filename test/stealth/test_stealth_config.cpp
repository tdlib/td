//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthConfig.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::test::MockRng;

TEST(StealthConfig, DefaultSecretConfigValidatesAndSamplesWithinRange) {
  MockRng rng(1);
  auto config = StealthConfig::from_secret(ProxySecret::from_raw("0123456789abcdef"), rng);

  ASSERT_TRUE(config.validate().is_ok());

  for (int i = 0; i < 64; i++) {
    auto sample = config.sample_initial_record_size(rng);
    ASSERT_TRUE(sample >= config.record_size_policy.slow_start_min);
    ASSERT_TRUE(sample <= config.record_size_policy.slow_start_max);
  }
}

TEST(StealthConfig, RejectsInvalidWatermarkRelationships) {
  MockRng rng(2);
  auto config = StealthConfig::default_config(rng);

  config.high_watermark = config.ring_capacity;
  ASSERT_TRUE(config.validate().is_error());

  config = StealthConfig::default_config(rng);
  config.low_watermark = config.high_watermark + 1;
  ASSERT_TRUE(config.validate().is_error());

  config = StealthConfig::default_config(rng);
  config.ring_capacity = 0;
  ASSERT_TRUE(config.validate().is_error());
}

TEST(StealthConfig, RejectsInvalidIptAndRecordRanges) {
  MockRng rng(3);
  auto config = StealthConfig::default_config(rng);

  config.ipt_params.idle_scale_ms = config.ipt_params.idle_max_ms + 1.0;
  ASSERT_TRUE(config.validate().is_error());

  config = StealthConfig::default_config(rng);
  config.ipt_params.p_idle_to_burst = 1.5;
  ASSERT_TRUE(config.validate().is_error());

  config = StealthConfig::default_config(rng);
  config.ipt_params.idle_alpha = 0.0;
  ASSERT_TRUE(config.validate().is_error());

  config = StealthConfig::default_config(rng);
  config.drs_policy.record_size_min = 2048;
  config.drs_policy.record_size_max = 1024;
  ASSERT_TRUE(config.validate().is_error());

  config = StealthConfig::default_config(rng);
  config.record_size_policy.slow_start_min = 128;
  ASSERT_TRUE(config.validate().is_error());
}

TEST(StealthConfig, InitialRecordSamplingKeepsPerConnectionEntropy) {
  std::unordered_set<td::int32> observed;
  for (td::uint64 seed = 1; seed <= 32; seed++) {
    MockRng rng(seed);
    auto config = StealthConfig::default_config(rng);
    ASSERT_TRUE(config.validate().is_ok());
    observed.insert(config.sample_initial_record_size(rng));
  }

  ASSERT_TRUE(observed.size() > 1u);
}

}  // namespace