//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;

TEST(TlsRuntimePlatformDefaults, CompileTimePlatformMapsToExpectedRuntimeHints) {
  auto hints = default_runtime_platform_hints();

#if TD_ANDROID
  ASSERT_TRUE(hints.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(hints.mobile_os == MobileOs::Android);
  ASSERT_TRUE(hints.desktop_os == DesktopOs::Unknown);
#elif TD_DARWIN_IOS || TD_DARWIN_TV_OS || TD_DARWIN_VISION_OS || TD_DARWIN_WATCH_OS
  ASSERT_TRUE(hints.device_class == DeviceClass::Mobile);
  ASSERT_TRUE(hints.mobile_os == MobileOs::IOS);
  ASSERT_TRUE(hints.desktop_os == DesktopOs::Unknown);
#elif TD_DARWIN
  ASSERT_TRUE(hints.device_class == DeviceClass::Desktop);
  ASSERT_TRUE(hints.mobile_os == MobileOs::None);
  ASSERT_TRUE(hints.desktop_os == DesktopOs::Darwin);
#elif TD_WINDOWS
  ASSERT_TRUE(hints.device_class == DeviceClass::Desktop);
  ASSERT_TRUE(hints.mobile_os == MobileOs::None);
  ASSERT_TRUE(hints.desktop_os == DesktopOs::Windows);
#else
  ASSERT_TRUE(hints.device_class == DeviceClass::Desktop);
  ASSERT_TRUE(hints.mobile_os == MobileOs::None);
  ASSERT_TRUE(hints.desktop_os == DesktopOs::Linux);
#endif
}

}  // namespace