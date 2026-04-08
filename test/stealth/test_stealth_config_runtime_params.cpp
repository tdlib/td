// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::test::MockRng;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "runtime-policy.example.com";
  return secret;
}

RuntimePlatformHints make_linux_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  return platform;
}

TEST(StealthConfigRuntimeParams, RuntimeBulkThresholdOverridesNewConnectionConfig) {
  RuntimeParamsGuard guard;
  MockRng rng(17);

  StealthRuntimeParams params;
  params.bulk_threshold_bytes = 16384;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto config = StealthConfig::from_secret(ProxySecret::from_raw(make_tls_secret()), rng, 1712345678, make_linux_platform());
  ASSERT_EQ(static_cast<size_t>(16384), config.bulk_threshold_bytes);
  ASSERT_TRUE(config.validate().is_ok());
}

TEST(StealthConfigRuntimeParams, InvalidRuntimeBulkThresholdFailsClosedWithoutPartialApply) {
  RuntimeParamsGuard guard;
  MockRng rng(23);

  StealthRuntimeParams params;
  params.bulk_threshold_bytes = 128;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_error());

  auto config = StealthConfig::from_secret(ProxySecret::from_raw(make_tls_secret()), rng, 1712345678, make_linux_platform());
  ASSERT_EQ(static_cast<size_t>(8192), config.bulk_threshold_bytes);
  ASSERT_TRUE(config.validate().is_ok());
}

TEST(StealthConfigRuntimeParams, RuntimeIptOverridesApplyToNewConnectionConfig) {
  RuntimeParamsGuard guard;
  MockRng rng(29);

  StealthRuntimeParams params;
  params.ipt_params.burst_mu_ms = 4.25;
  params.ipt_params.burst_sigma = 1.1;
  params.ipt_params.burst_max_ms = 240.0;
  params.ipt_params.idle_alpha = 1.8;
  params.ipt_params.idle_scale_ms = 650.0;
  params.ipt_params.idle_max_ms = 3400.0;
  params.ipt_params.p_burst_stay = 0.91;
  params.ipt_params.p_idle_to_burst = 0.22;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto config =
      StealthConfig::from_secret(ProxySecret::from_raw(make_tls_secret()), rng, 1712345678, make_linux_platform());
  ASSERT_EQ(4.25, config.ipt_params.burst_mu_ms);
  ASSERT_EQ(1.1, config.ipt_params.burst_sigma);
  ASSERT_EQ(240.0, config.ipt_params.burst_max_ms);
  ASSERT_EQ(1.8, config.ipt_params.idle_alpha);
  ASSERT_EQ(650.0, config.ipt_params.idle_scale_ms);
  ASSERT_EQ(3400.0, config.ipt_params.idle_max_ms);
  ASSERT_EQ(0.91, config.ipt_params.p_burst_stay);
  ASSERT_EQ(0.22, config.ipt_params.p_idle_to_burst);
  ASSERT_TRUE(config.validate().is_ok());
}

TEST(StealthConfigRuntimeParams, RuntimeDrsOverridesApplyToNewConnectionConfig) {
  RuntimeParamsGuard guard;
  MockRng rng(31);

  StealthRuntimeParams params;
  params.drs_policy.slow_start.bins = {{1200, 1460, 3}, {1461, 2200, 2}};
  params.drs_policy.slow_start.max_repeat_run = 4;
  params.drs_policy.slow_start.local_jitter = 24;
  params.drs_policy.congestion_open.bins = {{2200, 4800, 3}, {4801, 7600, 2}};
  params.drs_policy.congestion_open.max_repeat_run = 5;
  params.drs_policy.congestion_open.local_jitter = 32;
  params.drs_policy.steady_state.bins = {{4096, 8192, 4}, {8193, 12000, 1}};
  params.drs_policy.steady_state.max_repeat_run = 6;
  params.drs_policy.steady_state.local_jitter = 48;
  params.drs_policy.slow_start_records = 5;
  params.drs_policy.congestion_bytes = 65536;
  params.drs_policy.idle_reset_ms_min = 333;
  params.drs_policy.idle_reset_ms_max = 1444;
  params.drs_policy.min_payload_cap = 900;
  params.drs_policy.max_payload_cap = 12000;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto config =
      StealthConfig::from_secret(ProxySecret::from_raw(make_tls_secret()), rng, 1712345678, make_linux_platform());
  ASSERT_EQ(5, config.drs_policy.slow_start_records);
  ASSERT_EQ(65536, config.drs_policy.congestion_bytes);
  ASSERT_EQ(333, config.drs_policy.idle_reset_ms_min);
  ASSERT_EQ(1444, config.drs_policy.idle_reset_ms_max);
  ASSERT_EQ(900, config.drs_policy.min_payload_cap);
  ASSERT_EQ(12000, config.drs_policy.max_payload_cap);
  ASSERT_EQ(2u, config.drs_policy.slow_start.bins.size());
  ASSERT_EQ(2u, config.drs_policy.congestion_open.bins.size());
  ASSERT_EQ(2u, config.drs_policy.steady_state.bins.size());
  ASSERT_TRUE(config.validate().is_ok());
}

}  // namespace