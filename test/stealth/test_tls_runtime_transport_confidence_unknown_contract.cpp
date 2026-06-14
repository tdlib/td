// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;
using td::mtproto::stealth::TransportConfidence;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints linux_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  return platform;
}

RuntimePlatformHints windows_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Windows;
  return platform;
}

RuntimePlatformHints ios_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;
  return platform;
}

TEST(TlsRuntimeTransportConfidenceUnknownContract, RejectsLinuxConfigWithoutTlsOnlyWeight) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = linux_platform();
  params.profile_weights.chrome120 = 0;
  params.profile_weights.firefox148 = 0;

  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_error());
}

TEST(TlsRuntimeTransportConfidenceUnknownContract, RejectsWindowsConfigWithoutTlsOnlyWeight) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = windows_platform();
  params.profile_weights.chrome147_windows = 100;
  params.profile_weights.firefox149_windows = 0;

  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_error());
}

TEST(TlsRuntimeTransportConfidenceUnknownContract, RejectsIosConfigWithoutTlsOnlyWeight) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.transport_confidence = TransportConfidence::Unknown;
  params.platform_hints = ios_platform();
  params.profile_weights.ios14 = 0;
  params.profile_weights.chrome147_ios_chromium = 100;
  params.profile_weights.apple_ios_tls = 0;

  auto status = set_runtime_stealth_params_for_tests(params);
  ASSERT_TRUE(status.is_error());
}

}  // namespace
