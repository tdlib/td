// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#pragma once

#include "td/mtproto/stealth/StealthParamsLoader.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace td {
namespace mtproto {
namespace stealth {
namespace test_helpers {

using td::FileFd;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::RuntimePlatformHints;
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
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-multiloader").move_as_ok();
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

inline void write_file(td::Slice path, td::Slice content) {
  auto file = FileFd::open(path.str(), FileFd::Write | FileFd::Create | FileFd::Truncate, 0600).move_as_ok();
  ASSERT_EQ(content.size(), file.write(content).move_as_ok());
  ASSERT_TRUE(file.sync().is_ok());
}

inline td::string join_path(td::Slice dir, td::Slice file_name) {
  td::string result = dir.str();
  result += TD_DIR_SLASH;
  result += file_name.str();
  return result;
}

inline td::string ios_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"mobile\","
         "\"mobile_os\":\"ios\","
         "\"desktop_os\":\"unknown\"},"
         "\"profile_weights\":{"
         "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
         "\"safari26_3\":0,\"ios14\":100,\"android11_okhttp_advisory\":0},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

inline td::string android_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"mobile\","
         "\"mobile_os\":\"android\","
         "\"desktop_os\":\"unknown\"},"
         "\"profile_weights\":{"
         "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
         "\"safari26_3\":0,\"ios14\":0,\"android11_okhttp_advisory\":100},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

inline td::string linux_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"desktop\","
         "\"mobile_os\":\"none\","
         "\"desktop_os\":\"linux\"},"
         "\"profile_weights\":{"
         "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
         "\"safari26_3\":0,\"ios14\":100,\"android11_okhttp_advisory\":0},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

inline td::string darwin_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"desktop\","
         "\"mobile_os\":\"none\","
         "\"desktop_os\":\"darwin\"},"
         "\"profile_weights\":{"
         "\"chrome133\":35,\"chrome131\":25,\"chrome120\":10,\"firefox148\":10,"
         "\"safari26_3\":20,\"ios14\":100,\"android11_okhttp_advisory\":0},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

inline td::string windows_config_json() {
  return "{"
         "\"version\":1,"
         "\"platform_hints\":{"
         "\"device_class\":\"desktop\","
         "\"mobile_os\":\"none\","
         "\"desktop_os\":\"windows\"},"
         "\"profile_weights\":{"
         "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
         "\"safari26_3\":0,\"ios14\":100,\"android11_okhttp_advisory\":0},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192}";
}

inline void assert_ios_platform_published(const RuntimePlatformHints &platform) {
  ASSERT_TRUE(platform.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(platform.mobile_os == MobileOs::IOS);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Unknown);
}

inline void assert_ios_lane_stable() {
  auto platform = default_runtime_platform_hints();
  assert_ios_platform_published(platform);
  auto profile = pick_runtime_profile("multiloader-lock.example.com", 1712345678, platform);
  // iOS at the default Unknown confidence now exposes both the advisory IOS14 lane
  // and the verified Apple iOS TLS lane (both TlsOnly).
  ASSERT_TRUE(profile == BrowserProfile::IOS14 || profile == BrowserProfile::AppleIosTls);
}

}  // namespace test_helpers
}  // namespace stealth
}  // namespace mtproto
}  // namespace td