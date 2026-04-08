// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
    reset_runtime_ech_failure_state_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

TEST(TlsRuntimeParamsPlatformFailClosed, IosPlatformRejectsZeroAllowedProfileWeight) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.platform_hints.device_class = DeviceClass::Mobile;
  params.platform_hints.mobile_os = MobileOs::IOS;
  params.platform_hints.desktop_os = DesktopOs::Unknown;
  params.profile_weights.ios14 = 0;
  params.profile_weights.android11_okhttp = 100;

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_error());
}

TEST(TlsRuntimeParamsPlatformFailClosed, AndroidPlatformRejectsZeroAllowedProfileWeight) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params;
  params.platform_hints.device_class = DeviceClass::Mobile;
  params.platform_hints.mobile_os = MobileOs::Android;
  params.platform_hints.desktop_os = DesktopOs::Unknown;
  params.profile_weights.ios14 = 100;
  params.profile_weights.android11_okhttp = 0;

  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_error());
}

}  // namespace
