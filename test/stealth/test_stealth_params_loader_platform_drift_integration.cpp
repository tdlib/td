// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
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
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-platform-drift").move_as_ok();
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

const char *linux_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"desktop\","
         "\"mobile_os\":\"none\","
         "\"desktop_os\":\"linux\"},"
         "\"flow_behavior\":{"
         "\"max_connects_per_10s_per_destination\":6,"
         "\"min_reuse_ratio\":0.55,"
         "\"min_conn_lifetime_ms\":1500,"
         "\"max_conn_lifetime_ms\":180000,"
         "\"max_destination_share\":0.70,"
         "\"sticky_domain_rotation_window_sec\":900,"
         "\"anti_churn_min_reconnect_interval_ms\":300},"
         "\"profile_weights\":{"
         "\"allow_cross_class_rotation\":false,"
         "\"desktop_darwin\":{"
         "\"Chrome133\":35,\"Chrome131\":25,\"Chrome120\":10,\"Safari26_3\":20,\"Firefox148\":10},"
         "\"desktop_non_darwin\":{"
         "\"Chrome133\":50,\"Chrome131\":20,\"Chrome120\":15,\"Safari26_3\":0,\"Firefox148\":15},"
         "\"mobile\":{\"IOS14\":70,\"Android11_OkHttp_Advisory\":30}},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\"},"
         "\"ru\":{\"ech_mode\":\"disabled\"},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,"
         "\"ech_disable_ttl_seconds\":1800,"
         "\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

const char *ios_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"mobile\","
         "\"mobile_os\":\"ios\","
         "\"desktop_os\":\"unknown\"},"
         "\"flow_behavior\":{"
         "\"max_connects_per_10s_per_destination\":6,"
         "\"min_reuse_ratio\":0.55,"
         "\"min_conn_lifetime_ms\":1500,"
         "\"max_conn_lifetime_ms\":180000,"
         "\"max_destination_share\":0.70,"
         "\"sticky_domain_rotation_window_sec\":900,"
         "\"anti_churn_min_reconnect_interval_ms\":300},"
         "\"profile_weights\":{"
         "\"allow_cross_class_rotation\":false,"
         "\"desktop_darwin\":{"
         "\"Chrome133\":35,\"Chrome131\":25,\"Chrome120\":10,\"Safari26_3\":20,\"Firefox148\":10},"
         "\"desktop_non_darwin\":{"
         "\"Chrome133\":50,\"Chrome131\":20,\"Chrome120\":15,\"Safari26_3\":0,\"Firefox148\":15},"
         "\"mobile\":{\"IOS14\":100,\"Android11_OkHttp_Advisory\":0}},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\"},"
         "\"ru\":{\"ech_mode\":\"disabled\"},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,"
         "\"ech_disable_ttl_seconds\":1800,"
         "\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

TEST(StealthParamsLoaderPlatformDriftIntegration, FailedPlatformMutationKeepsRuntimeConsumersOnLastKnownGoodPlatform) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path, td::Slice(linux_config_json()));
  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());

  auto initial_platform = default_runtime_platform_hints();
  auto initial_profile = pick_runtime_profile("platform-drift.example.com", 1712345678, initial_platform);

  write_file(path, td::Slice(ios_config_json()));
  ASSERT_FALSE(loader.try_reload());

  auto stable_platform = default_runtime_platform_hints();
  auto stable_profile = pick_runtime_profile("platform-drift.example.com", 1712345678, stable_platform);
  ASSERT_TRUE(stable_profile == initial_profile);
}

}  // namespace
