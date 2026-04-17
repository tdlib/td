// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::all_profiles;
using td::mtproto::stealth::allowed_profiles_for_platform;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::default_profile_weights;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::ech_mode_for_route;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_profile_sticky;
using td::mtproto::stealth::profile_fixture_metadata;
using td::mtproto::stealth::ProfileFixtureSourceKind;
using td::mtproto::stealth::ProfileTrustTier;
using td::mtproto::stealth::RouteFailureState;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::SelectionKey;
using td::mtproto::test::MockRng;

SelectionKey make_selection_key(td::Slice destination, td::uint32 time_bucket) {
  SelectionKey key;
  key.destination = destination.str();
  key.time_bucket = time_bucket;
  return key;
}

TEST(TlsProfileRegistry, FixtureMetadataExposesExplicitSourceKind) {
  for (auto profile : all_profiles()) {
    auto metadata = profile_fixture_metadata(profile);
    ASSERT_FALSE(metadata.source_id.empty());
    ASSERT_TRUE(metadata.source_kind != ProfileFixtureSourceKind::AdvisoryCodeSample);
  }
}

TEST(TlsProfileRegistry, VerifiedProfilesCarryNetworkCorroboration) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                       BrowserProfile::Firefox148, BrowserProfile::Firefox149_MacOS26_3}) {
    auto metadata = profile_fixture_metadata(profile);
    ASSERT_TRUE(metadata.trust_tier == ProfileTrustTier::Verified);
    ASSERT_TRUE(metadata.source_kind == ProfileFixtureSourceKind::BrowserCapture ||
                metadata.source_kind == ProfileFixtureSourceKind::CurlCffiCapture);
    ASSERT_TRUE(metadata.has_independent_network_provenance);
    ASSERT_TRUE(metadata.has_utls_snapshot_corroboration);
    ASSERT_TRUE(metadata.release_gating);
  }
}

TEST(TlsProfileRegistry, SafariAndMobileProfilesRemainAdvisoryUntilNetworkFixturesLand) {
  for (auto profile : {BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory}) {
    auto metadata = profile_fixture_metadata(profile);
    ASSERT_TRUE(metadata.trust_tier == ProfileTrustTier::Advisory);
    ASSERT_FALSE(metadata.has_independent_network_provenance);
    ASSERT_FALSE(metadata.has_utls_snapshot_corroboration);
    ASSERT_FALSE(metadata.release_gating);
  }
}

TEST(TlsProfileRegistry, StickySelectionIsStableForSameKey) {
  MockRng rng(42);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(platform);
  auto key = make_selection_key("www.google.com", 20260406);

  auto first = pick_profile_sticky(default_profile_weights(), key, platform, allowed, rng);
  auto second = pick_profile_sticky(default_profile_weights(), key, platform, allowed, rng);
  ASSERT_TRUE(first == second);
}

TEST(TlsProfileRegistry, MobileClassUsesOnlyMobileProfiles) {
  MockRng rng(7);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::Android;

  auto allowed = allowed_profiles_for_platform(platform);
  auto key = make_selection_key("www.google.com", 20260406);

  for (td::uint32 bucket = 0; bucket < 128; bucket++) {
    key.time_bucket = 20260406 + bucket;
    auto profile = pick_profile_sticky(default_profile_weights(), key, platform, allowed, rng);
    ASSERT_TRUE(profile == BrowserProfile::IOS14 || profile == BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(TlsProfileRegistry, IosMobileClassAllowsAppleTlsAndChromiumOnly) {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;

  auto allowed = allowed_profiles_for_platform(platform);
  bool saw_ios14 = false;
  bool saw_ios_chromium = false;
  for (auto profile : allowed) {
    saw_ios14 = saw_ios14 || profile == BrowserProfile::IOS14;
    saw_ios_chromium = saw_ios_chromium || profile == BrowserProfile::Chrome147_IOSChromium;
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
  ASSERT_TRUE(saw_ios14);
  ASSERT_TRUE(saw_ios_chromium);
}

TEST(TlsProfileRegistry, DesktopClassNeverUsesMobileProfiles) {
  MockRng rng(9);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Windows;

  auto allowed = allowed_profiles_for_platform(platform);
  auto key = make_selection_key("www.google.com", 20260406);

  for (td::uint32 bucket = 0; bucket < 128; bucket++) {
    key.time_bucket = 20260406 + bucket;
    auto profile = pick_profile_sticky(default_profile_weights(), key, platform, allowed, rng);
    ASSERT_TRUE(profile != BrowserProfile::IOS14);
    ASSERT_TRUE(profile != BrowserProfile::Chrome147_IOSChromium);
    ASSERT_TRUE(profile != BrowserProfile::Android11_OkHttp_Advisory);
  }
}

TEST(TlsProfileRegistry, NonDarwinDesktopNeverUsesSafari) {
  MockRng rng(11);
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;

  auto allowed = allowed_profiles_for_platform(platform);
  auto key = make_selection_key("www.google.com", 20260406);

  for (td::uint32 bucket = 0; bucket < 128; bucket++) {
    key.time_bucket = 20260406 + bucket;
    auto profile = pick_profile_sticky(default_profile_weights(), key, platform, allowed, rng);
    ASSERT_TRUE(profile != BrowserProfile::Safari26_3);
  }
}

TEST(TlsProfileRegistry, RuAndUnknownRoutesDisableEch) {
  RouteFailureState route_failures;

  NetworkRouteHints unknown_route;
  unknown_route.is_known = false;
  unknown_route.is_ru = false;
  ASSERT_TRUE(EchMode::Disabled == ech_mode_for_route(unknown_route, route_failures));

  NetworkRouteHints ru_route;
  ru_route.is_known = true;
  ru_route.is_ru = true;
  ASSERT_TRUE(EchMode::Disabled == ech_mode_for_route(ru_route, route_failures));
}

TEST(TlsProfileRegistry, CircuitBreakerDisablesEchForKnownNonRuRoutes) {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;

  RouteFailureState route_failures;
  route_failures.recent_ech_failures = 3;
  ASSERT_TRUE(EchMode::Disabled == ech_mode_for_route(route, route_failures));

  route_failures.recent_ech_failures = 0;
  route_failures.ech_block_suspected = true;
  ASSERT_TRUE(EchMode::Disabled == ech_mode_for_route(route, route_failures));
}

TEST(TlsProfileRegistry, KnownNonRuRoutesAllowRfc9180OuterWhenHealthy) {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;

  RouteFailureState route_failures;
  ASSERT_TRUE(EchMode::Rfc9180Outer == ech_mode_for_route(route, route_failures));
}

}  // namespace