// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimeActivePolicy;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthParamsLoader;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-plan").move_as_ok();
  }

  ~ScopedTempDir() {
    td::rmrf(dir_).ignore();
  }

  td::Slice path() const {
    return dir_;
  }

 private:
  td::string dir_;
};

void write_file(td::Slice path, td::Slice content) {
  auto file = FileFd::open(path.str(), FileFd::Write | FileFd::Create | FileFd::Truncate, 0600).move_as_ok();
  ASSERT_EQ(content.size(), file.write(content).move_as_ok());
  ASSERT_TRUE(file.sync().is_ok());
}

td::string join_path(td::Slice dir, td::Slice file_name) {
  td::string result = dir.str();
  result += TD_DIR_SLASH;
  result += file_name.str();
  return result;
}

TEST(StealthParamsLoaderPlan, StrictLoadAcceptsPlanStyleSchemaAndPublishesPlatformAwareWeights) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             "{"
             "\"version\":1,"
             "\"active_policy\":\"ru_egress\","
             "\"flow_behavior\":{"
             "\"max_connects_per_10s_per_destination\":6,"
             "\"min_reuse_ratio\":0.55,"
             "\"min_conn_lifetime_ms\":1500,"
             "\"max_conn_lifetime_ms\":180000,"
             "\"max_destination_share\":0.70,"
             "\"sticky_domain_rotation_window_sec\":900,"
             "\"anti_churn_min_reconnect_interval_ms\":300},"
             "\"platform_hints\":{"
             "\"device_class\":\"desktop\","
             "\"mobile_os\":\"none\","
             "\"desktop_os\":\"linux\"},"
             "\"profile_weights\":{"
             "\"allow_cross_class_rotation\":false,"
             "\"desktop_darwin\":{"
             "\"Chrome133\":0,\"Chrome131\":0,\"Chrome120\":0,\"Safari26_3\":100,\"Firefox148\":0},"
             "\"desktop_non_darwin\":{"
             "\"Chrome133\":0,\"Chrome131\":0,\"Chrome120\":0,\"Safari26_3\":0,\"Firefox148\":100},"
             "\"mobile\":{\"IOS14\":0,\"Android11_OkHttp\":100}},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru_egress\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru_egress\":{\"ech_mode\":\"grease_draft17\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":3,"
             "\"ech_disable_ttl_sec\":1800,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\",\"hello_timeout\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_TRUE(params.active_policy == RuntimeActivePolicy::RuEgress);
  ASSERT_TRUE(params.platform_hints.device_class == DeviceClass::Desktop);
  ASSERT_TRUE(params.platform_hints.mobile_os == MobileOs::None);
  ASSERT_TRUE(params.platform_hints.desktop_os == DesktopOs::Linux);
  ASSERT_EQ(6u, params.flow_behavior.max_connects_per_10s_per_destination);
  ASSERT_EQ(300u, params.flow_behavior.anti_churn_min_reconnect_interval_ms);
  ASSERT_EQ(100, params.profile_weights.firefox148);
  ASSERT_EQ(0, params.profile_weights.chrome133);
  ASSERT_EQ(static_cast<size_t>(16384), params.bulk_threshold_bytes);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  for (td::int32 day = 0; day < 32; day++) {
    auto profile = pick_runtime_profile("plan-runtime.example.com", 1712345678 + day * 86400, params.platform_hints);
    ASSERT_TRUE(profile == BrowserProfile::Firefox148);
  }
}

TEST(StealthParamsLoaderPlan, StrictLoadRejectsPlanStyleCrossClassRotationAndInvalidPlatformMix) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             "{"
             "\"version\":1,"
             "\"flow_behavior\":{"
             "\"max_connects_per_10s_per_destination\":6,"
             "\"min_reuse_ratio\":0.55,"
             "\"min_conn_lifetime_ms\":1500,"
             "\"max_conn_lifetime_ms\":180000,"
             "\"max_destination_share\":0.70,"
             "\"sticky_domain_rotation_window_sec\":900,"
             "\"anti_churn_min_reconnect_interval_ms\":300},"
             "\"platform_hints\":{"
             "\"device_class\":\"desktop\","
             "\"mobile_os\":\"android\","
             "\"desktop_os\":\"linux\"},"
             "\"profile_weights\":{"
             "\"allow_cross_class_rotation\":true,"
             "\"desktop_darwin\":{"
             "\"Chrome133\":0,\"Chrome131\":0,\"Chrome120\":0,\"Safari26_3\":100,\"Firefox148\":0},"
             "\"desktop_non_darwin\":{"
             "\"Chrome133\":0,\"Chrome131\":0,\"Chrome120\":0,\"Safari26_3\":0,\"Firefox148\":100},"
             "\"mobile\":{\"IOS14\":0,\"Android11_OkHttp\":100}},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru_egress\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru_egress\":{\"ech_mode\":\"grease_draft17\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":3,"
             "\"ech_disable_ttl_sec\":1800,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

}  // namespace