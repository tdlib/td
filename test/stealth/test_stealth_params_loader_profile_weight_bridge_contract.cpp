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
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-profile-weight-bridge-contract").move_as_ok();
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

TEST(StealthParamsLoaderProfileWeightBridgeContract,
     StrictLoadParsesExtendedFlatProfileWeightsForWindowsAndIosChromiumLanes) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             R"json({
  "version": 1,
  "platform_hints": {
    "device_class": "mobile",
    "mobile_os": "ios",
    "desktop_os": "unknown"
  },
  "transport_confidence": "strong",
  "profile_weights": {
    "chrome133": 50,
    "chrome131": 20,
    "chrome120": 15,
    "chrome147_windows": 0,
    "chromium_macos_no_alps": 10,
    "chromium_macos_4469": 25,
    "chromium_macos_44cd": 35,
    "chrome147_ios_chromium": 100,
    "firefox148": 15,
    "firefox149_android": 0,
    "firefox149_macos26_3": 10,
    "firefox149_windows": 100,
    "safari26_3": 20,
    "ios14": 0,
    "android11_okhttp_advisory": 100
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled", "allow_quic": false},
    "ru": {"ech_mode": "disabled", "allow_quic": false},
    "non_ru": {"ech_mode": "rfc9180_outer", "allow_quic": false}
  },
  "route_failure": {
    "ech_failure_threshold": 3,
    "ech_disable_ttl_seconds": 300.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 8192
})json");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(0, params.profile_weights.chrome147_windows);
  ASSERT_EQ(100, params.profile_weights.firefox149_windows);
  ASSERT_EQ(100, params.profile_weights.chrome147_ios_chromium);
  ASSERT_EQ(0, params.profile_weights.ios14);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  auto platform = params.platform_hints;
  ASSERT_TRUE(platform.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(platform.mobile_os == MobileOs::IOS);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Unknown);

  for (td::int32 day = 0; day < 32; day++) {
    auto profile = pick_runtime_profile("ios-lane-bridge.example.com", 1712345678 + day * 86400, platform);
    ASSERT_TRUE(profile == BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(StealthParamsLoaderProfileWeightBridgeContract,
     StrictLoadParsesFlatAndroidChromiumAlpsWeightForAndroidRuntimeSelection) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             R"json({
  "version": 1,
  "platform_hints": {
    "device_class": "mobile",
    "mobile_os": "android",
    "desktop_os": "unknown"
  },
  "transport_confidence": "strong",
  "profile_weights": {
    "chrome133": 50,
    "chrome131": 20,
    "chrome120": 15,
    "chrome147_windows": 0,
    "chromium_macos_no_alps": 10,
    "chromium_macos_4469": 25,
    "chromium_macos_44cd": 35,
    "chrome147_ios_chromium": 0,
    "firefox148": 15,
    "firefox149_android": 0,
    "firefox149_macos26_3": 10,
    "firefox149_windows": 0,
    "safari26_3": 20,
    "ios14": 0,
    "android_chromium_alps": 100,
    "android11_okhttp_advisory": 0
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled", "allow_quic": false},
    "ru": {"ech_mode": "disabled", "allow_quic": false},
    "non_ru": {"ech_mode": "rfc9180_outer", "allow_quic": false}
  },
  "route_failure": {
    "ech_failure_threshold": 3,
    "ech_disable_ttl_seconds": 300.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 8192
})json");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(100, params.profile_weights.android_chromium_alps);
  ASSERT_EQ(0, params.profile_weights.firefox149_android);
  ASSERT_EQ(0, params.profile_weights.android11_okhttp_advisory);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  auto platform = params.platform_hints;
  ASSERT_TRUE(platform.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(platform.mobile_os == MobileOs::Android);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Unknown);

  for (td::int32 day = 0; day < 32; day++) {
    auto profile =
        pick_runtime_profile("android-flat-verified.example.com", 1712345678 + day * 86400, platform);
    ASSERT_TRUE(profile == BrowserProfile::AndroidChromium_Alps);
  }
}

TEST(StealthParamsLoaderProfileWeightBridgeContract,
     StrictLoadBridgesLegacyAndroidMobileShareIntoVerifiedAndAdvisoryRuntimeLanes) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             R"json({
  "version": 1,
  "platform_hints": {
    "device_class": "mobile",
    "mobile_os": "android",
    "desktop_os": "unknown"
  },
  "transport_confidence": "strong",
  "profile_weights": {
    "allow_cross_class_rotation": false,
    "desktop_darwin": {
      "Chrome133": 35,
      "Chrome131": 25,
      "Chrome120": 10,
      "Safari26_3": 20,
      "Firefox148": 10
    },
    "desktop_non_darwin": {
      "Chrome133": 50,
      "Chrome131": 20,
      "Chrome120": 15,
      "Safari26_3": 0,
      "Firefox148": 15
    },
    "mobile": {
      "IOS14": 70,
      "Android11_OkHttp_Advisory": 30
    }
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled", "allow_quic": false},
    "ru": {"ech_mode": "disabled", "allow_quic": false},
    "non_ru": {"ech_mode": "rfc9180_outer", "allow_quic": false}
  },
  "route_failure": {
    "ech_failure_threshold": 3,
    "ech_disable_ttl_seconds": 300.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 8192
})json");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(20, params.profile_weights.android_chromium_alps);
  ASSERT_EQ(5, params.profile_weights.firefox149_android);
  ASSERT_EQ(5, params.profile_weights.android11_okhttp_advisory);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  auto platform = params.platform_hints;
  ASSERT_TRUE(platform.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(platform.mobile_os == MobileOs::Android);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Unknown);

  bool saw_verified = false;
  bool saw_firefox = false;
  bool saw_advisory = false;
  for (td::int32 day = 0; day < 256 && !(saw_verified && saw_firefox && saw_advisory); day++) {
    auto profile =
        pick_runtime_profile("android-legacy-bridge.example.com", 1714345678 + day * 86400, platform);
    saw_verified = saw_verified || profile == BrowserProfile::AndroidChromium_Alps;
    saw_firefox = saw_firefox || profile == BrowserProfile::Firefox149_Android;
    saw_advisory = saw_advisory || profile == BrowserProfile::Android11_OkHttp_Advisory;
  }
  ASSERT_TRUE(saw_verified);
  ASSERT_TRUE(saw_firefox);
  ASSERT_TRUE(saw_advisory);
}

TEST(StealthParamsLoaderProfileWeightBridgeContract, StrictLoadAllowsIosChromiumOnlyLaneWithoutAndroidFallbackWeight) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             R"json({
  "version": 1,
  "platform_hints": {
    "device_class": "mobile",
    "mobile_os": "ios",
    "desktop_os": "unknown"
  },
  "transport_confidence": "strong",
  "profile_weights": {
    "chrome133": 50,
    "chrome131": 20,
    "chrome120": 15,
    "chrome147_windows": 0,
    "chrome147_ios_chromium": 100,
    "firefox148": 15,
    "firefox149_windows": 100,
    "safari26_3": 20,
    "ios14": 0,
    "android11_okhttp_advisory": 0
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled", "allow_quic": false},
    "ru": {"ech_mode": "disabled", "allow_quic": false},
    "non_ru": {"ech_mode": "rfc9180_outer", "allow_quic": false}
  },
  "route_failure": {
    "ech_failure_threshold": 3,
    "ech_disable_ttl_seconds": 300.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 8192
})json");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(100, params.profile_weights.chrome147_ios_chromium);
  ASSERT_EQ(0, params.profile_weights.ios14);
  ASSERT_EQ(0, params.profile_weights.android11_okhttp_advisory);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  auto platform = params.platform_hints;
  ASSERT_TRUE(platform.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(platform.mobile_os == MobileOs::IOS);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Unknown);

  for (td::int32 day = 0; day < 32; day++) {
    auto profile = pick_runtime_profile("ios-lane-no-android-fallback.example.com", 1713345678 + day * 86400, platform);
    ASSERT_TRUE(profile == BrowserProfile::Chrome147_IOSChromium);
  }
}

TEST(StealthParamsLoaderProfileWeightBridgeContract,
     StrictLoadAllowsWindowsExplicitLaneWithoutLegacyNonDarwinDesktopWeights) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             R"json({
  "version": 1,
  "platform_hints": {
    "device_class": "desktop",
    "mobile_os": "none",
    "desktop_os": "windows"
  },
  "transport_confidence": "partial",
  "profile_weights": {
    "chrome133": 0,
    "chrome131": 0,
    "chrome120": 0,
    "chrome147_windows": 100,
    "chrome147_ios_chromium": 0,
    "firefox148": 0,
    "firefox149_windows": 0,
    "safari26_3": 0,
    "ios14": 100,
    "android11_okhttp_advisory": 0
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled", "allow_quic": false},
    "ru": {"ech_mode": "disabled", "allow_quic": false},
    "non_ru": {"ech_mode": "rfc9180_outer", "allow_quic": false}
  },
  "route_failure": {
    "ech_failure_threshold": 3,
    "ech_disable_ttl_seconds": 300.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 8192
})json");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(100, params.profile_weights.chrome147_windows);
  ASSERT_EQ(0, params.profile_weights.firefox149_windows);
  ASSERT_EQ(0, params.profile_weights.chrome133);
  ASSERT_EQ(0, params.profile_weights.firefox148);

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  auto platform = params.platform_hints;
  ASSERT_TRUE(platform.device_class == DeviceClass::Desktop);
  ASSERT_TRUE(platform.mobile_os == MobileOs::None);
  ASSERT_TRUE(platform.desktop_os == DesktopOs::Windows);

  for (td::int32 day = 0; day < 32; day++) {
    auto profile =
        pick_runtime_profile("windows-explicit-no-legacy-non-darwin.example.com", 1716345678 + day * 86400, platform);
    ASSERT_TRUE(profile == BrowserProfile::Chrome147_Windows);
  }
}

}  // namespace
