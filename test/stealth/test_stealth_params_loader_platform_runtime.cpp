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
using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
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
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-platform-runtime").move_as_ok();
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

TEST(StealthParamsLoaderPlatformRuntime, ReloadPublishesPlatformHintsToRuntimeConsumers) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             "{"
             "\"version\":1,"
             "\"platform_hints\":{"
             "\"device_class\":\"mobile\","
             "\"mobile_os\":\"android\","
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
             "\"bulk_threshold_bytes\":8192}");

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());

  auto platform = default_runtime_platform_hints();
  ASSERT_TRUE(platform.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(platform.mobile_os == MobileOs::Android);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Unknown);

  for (td::int32 day = 0; day < 32; day++) {
    auto profile = pick_runtime_profile("runtime-platform.example.com", 1712345678 + day * 86400, platform);
    ASSERT_TRUE(profile == BrowserProfile::Android11_OkHttp);
  }
}

}  // namespace