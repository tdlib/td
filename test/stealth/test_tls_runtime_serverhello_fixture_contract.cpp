// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

// Runtime fixture contract:
// - Runtime Windows profile families must resolve to reviewed Windows
//   ServerHello captures when the corpus contains same-OS artifacts.

#include "test/stealth/ServerHelloFixtureLoader.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace runtime_serverhello_fixture_contract {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::profile_spec;
using td::mtproto::test::load_server_hello_fixture_relative;
using td::mtproto::test::representative_server_hello_path_for_family;

TEST(TlsRuntimeServerHelloFixtureContract, Chrome147WindowsResolvesToReviewedWindowsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Chrome147_Windows).name).str();

  ASSERT_EQ(td::string("windows/chrome147_0_7727_55_windows10_22h2_19045_7058_b9b21355.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Chrome133ResolvesToReviewedLinuxDesktopCapture) {
  const auto relative = representative_server_hello_path_for_family(profile_spec(BrowserProfile::Chrome133).name).str();

  ASSERT_EQ(td::string("linux_desktop/chrome144_linux_desktop.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Chrome131ResolvesToReviewedLinuxDesktopCapture) {
  const auto relative = representative_server_hello_path_for_family(profile_spec(BrowserProfile::Chrome131).name).str();

  ASSERT_EQ(td::string("linux_desktop/chrome144_linux_desktop.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Chrome120ResolvesToReviewedLinuxDesktopCapture) {
  const auto relative = representative_server_hello_path_for_family(profile_spec(BrowserProfile::Chrome120).name).str();

  ASSERT_EQ(td::string("linux_desktop/chrome144_linux_desktop.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Firefox148ResolvesToReviewedLinuxDesktopCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Firefox148).name).str();

  ASSERT_EQ(td::string("linux_desktop/firefox148_linux_desktop.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Firefox149MacOsResolvesToReviewedMacOsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Firefox149_MacOS26_3).name).str();

  ASSERT_EQ(td::string("macos/firefox149_macos26_3.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, ChromiumMacosNoAlpsResolvesToReviewedMacOsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::ChromiumMacOS_NoAlps).name).str();

  ASSERT_EQ(td::string("macos/chromium130_macos26_3_301a8e50.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, ChromiumMacos4469ResolvesToReviewedMacOsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::ChromiumMacOS_4469).name).str();

  ASSERT_EQ(td::string("macos/chromium130_macos26_3_301a8e50.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, ChromiumMacos44CDResolvesToReviewedMacOsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::ChromiumMacOS_44CD).name).str();

  ASSERT_EQ(td::string("macos/chrome147_macos26_4_81b7d4cc.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Firefox149WindowsResolvesToReviewedWindowsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Firefox149_Windows).name).str();

  ASSERT_EQ(td::string("windows/firefox149_0_2_windows10_pro_22h2_19045_6456_e32b3ddb.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Safari26_3ResolvesToReviewedMacOsCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Safari26_3).name).str();

  ASSERT_EQ(td::string("macos/safari_macos26_4_57318420.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Chrome147IosChromiumResolvesToReviewedIosChromiumCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Chrome147_IOSChromium).name).str();

  ASSERT_EQ(td::string("ios/chrome147_0_7727_47_ios26_3.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, IOS14ResolvesToReviewedAppleTlsCapture) {
  const auto relative = representative_server_hello_path_for_family(profile_spec(BrowserProfile::IOS14).name).str();

  ASSERT_EQ(td::string("ios/safari26_3_ios26_3_1_83afd3bc.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Android11OkHttpResolvesToAndroidCompatibilityFallbackCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Android11_OkHttp_Advisory).name).str();

  ASSERT_EQ(td::string("android/chrome146_177_android16.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

TEST(TlsRuntimeServerHelloFixtureContract, Firefox149AndroidResolvesToReviewedAndroidCapture) {
  const auto relative =
      representative_server_hello_path_for_family(profile_spec(BrowserProfile::Firefox149_Android).name).str();

  ASSERT_EQ(td::string("android/firefox_android16_build_bp2a_250605_015_3156bb61.serverhello.json"), relative);
  ASSERT_TRUE(load_server_hello_fixture_relative(td::CSlice(relative)).is_ok());
}

}  // namespace runtime_serverhello_fixture_contract
