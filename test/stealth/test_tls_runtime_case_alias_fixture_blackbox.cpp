// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FamilyLaneMatchers.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedFamilyLaneBaselines.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/WireClassifierFeatures.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#if !TD_DARWIN

namespace {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_runtime_tls_client_hello;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::MobileOs;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::test::baselines::get_baseline;
using td::mtproto::test::FamilyLaneMatcher;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::Slice;

constexpr td::Slice kSecret = "0123456789secret";
constexpr int32 kUnixTimeBase = 1712345678;
constexpr double kWireLengthTolerancePercent = 15.0;
constexpr td::uint16 kAlpnExtType = 0x0010;

const td::string kHttp11OnlyAlpnBody("\x00\x09\x08\x68\x74\x74\x70\x2f\x31\x2e\x31", 11);

class RuntimeCaseAliasFixtureGuard final {
 public:
  RuntimeCaseAliasFixtureGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeCaseAliasFixtureGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints make_linux_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  return platform;
}

RuntimePlatformHints make_windows_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Windows;
  return platform;
}

RuntimePlatformHints make_ios_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Mobile;
  platform.mobile_os = MobileOs::IOS;
  return platform;
}

NetworkRouteHints known_non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

td::string uppercase_ascii(td::Slice input) {
  td::string out = input.str();
  for (auto &ch : out) {
    if ('a' <= ch && ch <= 'z') {
      ch = static_cast<char>(ch - 'a' + 'A');
    }
  }
  return out;
}

Slice family_id_for_linux_profile(BrowserProfile profile) {
  switch (profile) {
    case BrowserProfile::Chrome133:
    case BrowserProfile::Chrome131:
    case BrowserProfile::Chrome120:
      return Slice("chromium_linux_desktop");
    case BrowserProfile::Firefox148:
      return Slice("firefox_linux_desktop");
    default:
      UNREACHABLE();
      return Slice();
  }
}

Slice family_id_for_windows_profile(BrowserProfile profile) {
  switch (profile) {
    case BrowserProfile::Chrome147_Windows:
      return Slice("chromium_windows");
    case BrowserProfile::Firefox149_Windows:
      return Slice("firefox_windows");
    default:
      UNREACHABLE();
      return Slice();
  }
}

Slice family_id_for_ios_profile(BrowserProfile profile) {
  switch (profile) {
    case BrowserProfile::IOS14:
      return Slice("apple_ios_tls");
    case BrowserProfile::Chrome147_IOSChromium:
      return Slice("ios_chromium");
    default:
      UNREACHABLE();
      return Slice();
  }
}

bool is_grease_extension_type(td::uint16 value) {
  return (value & 0x0F0Fu) == 0x0A0Au && ((value >> 8) == (value & 0xFFu));
}

td::vector<td::uint16> non_grease_extension_types_without_padding(const td::mtproto::test::ParsedClientHello &hello) {
  td::vector<td::uint16> out;
  out.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (!is_grease_extension_type(ext.type) && ext.type != 0x0015u) {
      out.push_back(ext.type);
    }
  }
  return out;
}

TEST(TlsRuntimeCaseAliasFixtureBlackBox, CaseAliasesProduceIdenticalRuntimeWireAndPassFamilyMatchers) {
  RuntimeCaseAliasFixtureGuard guard;

  auto params = default_runtime_stealth_params();
  params.transport_confidence = td::mtproto::stealth::TransportConfidence::Partial;
  params.platform_hints = make_linux_platform();
  params.profile_weights.chrome133 = 1;
  params.profile_weights.firefox148 = 1;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  const auto route = known_non_ru_route();
  bool saw_chromium = false;
  bool saw_firefox = false;

  for (int i = 0; i < 256; i++) {
    const auto unix_time = static_cast<int32>(kUnixTimeBase + i * 137);
    const td::string lower = "runtime-case-alias-" + td::to_string(i) + ".fixture.example.com";
    const td::string upper = uppercase_ascii(lower);

    auto profile_lower = pick_runtime_profile(lower, unix_time, params.platform_hints);
    auto profile_upper = pick_runtime_profile(upper, unix_time, params.platform_hints);
    ASSERT_TRUE(profile_lower == profile_upper);

    if (profile_lower == BrowserProfile::Firefox148) {
      saw_firefox = true;
    } else {
      saw_chromium = true;
    }

    const auto *baseline = get_baseline(family_id_for_linux_profile(profile_lower), Slice("non_ru_egress"));
    ASSERT_TRUE(baseline != nullptr);
    FamilyLaneMatcher matcher(*baseline);

    MockRng rng_lower(0xA11A5000u + static_cast<td::uint64>(i));
    MockRng rng_upper(0xA11A5000u + static_cast<td::uint64>(i));

    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    auto lower_wire = build_runtime_tls_client_hello(lower, kSecret, unix_time, route, rng_lower);

    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    auto upper_wire = build_runtime_tls_client_hello(upper, kSecret, unix_time, route, rng_upper);

    auto lower_parsed_res = parse_tls_client_hello(lower_wire);
    auto upper_parsed_res = parse_tls_client_hello(upper_wire);
    ASSERT_TRUE(lower_parsed_res.is_ok());
    ASSERT_TRUE(upper_parsed_res.is_ok());
    auto lower_parsed = lower_parsed_res.move_as_ok();
    auto upper_parsed = upper_parsed_res.move_as_ok();

    // Case aliases may differ in SNI-dependent bytes, but runtime
    // selection and classifier-relevant structure must remain identical.
    ASSERT_EQ(lower_wire.size(), upper_wire.size());
    ASSERT_TRUE(non_grease_extension_types_without_padding(lower_parsed) ==
                non_grease_extension_types_without_padding(upper_parsed));
    ASSERT_TRUE(lower_parsed.supported_groups == upper_parsed.supported_groups);
    ASSERT_TRUE(lower_parsed.key_share_groups == upper_parsed.key_share_groups);
    ASSERT_EQ(lower_parsed.ech_payload_length, upper_parsed.ech_payload_length);

    auto lower_features = td::mtproto::test::wire_classifier::extract_generated_features(lower_wire);
    auto upper_features = td::mtproto::test::wire_classifier::extract_generated_features(upper_wire);
    ASSERT_EQ(lower_features.wire_length, upper_features.wire_length);
    ASSERT_EQ(lower_features.cipher_count, upper_features.cipher_count);
    ASSERT_EQ(lower_features.extension_count, upper_features.extension_count);
    ASSERT_EQ(lower_features.alpn_count, upper_features.alpn_count);
    ASSERT_EQ(lower_features.supported_groups_count, upper_features.supported_groups_count);
    ASSERT_EQ(lower_features.key_share_count, upper_features.key_share_count);
    ASSERT_EQ(lower_features.ech_payload_length, upper_features.ech_payload_length);
    ASSERT_EQ(lower_features.has_alps, upper_features.has_alps);

    ASSERT_TRUE(matcher.passes_upstream_rule_legality(lower_parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(upper_parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(lower_wire.size(), kWireLengthTolerancePercent));
    if (lower_parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(lower_parsed.ech_payload_length));
    }
    if (upper_parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(upper_parsed.ech_payload_length));
    }

    auto *lower_alpn_ext = find_extension(lower_parsed, kAlpnExtType);
    auto *upper_alpn_ext = find_extension(upper_parsed, kAlpnExtType);
    ASSERT_TRUE(lower_alpn_ext != nullptr);
    ASSERT_TRUE(upper_alpn_ext != nullptr);
    ASSERT_EQ(Slice(kHttp11OnlyAlpnBody), lower_alpn_ext->value);
    ASSERT_EQ(Slice(kHttp11OnlyAlpnBody), upper_alpn_ext->value);
  }

  ASSERT_TRUE(saw_chromium);
  ASSERT_TRUE(saw_firefox);
}

TEST(TlsRuntimeCaseAliasFixtureBlackBox, WindowsCaseAliasesProduceIdenticalRuntimeWireAndPassFamilyMatchers) {
  RuntimeCaseAliasFixtureGuard guard;

  auto params = default_runtime_stealth_params();
  params.transport_confidence = td::mtproto::stealth::TransportConfidence::Partial;
  params.platform_hints = make_windows_platform();
  params.profile_weights.chrome147_windows = 1;
  params.profile_weights.firefox149_windows = 1;
  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  // Keep non-darwin/darwin desktop totals non-empty for global runtime
  // params validation; Windows profile selection still uses only
  // chrome147_windows/firefox149_windows.
  params.profile_weights.firefox148 = 1;
  params.profile_weights.chrome147_ios_chromium = 0;
  params.profile_weights.safari26_3 = 0;
  params.profile_weights.ios14 = 1;
  params.profile_weights.android11_okhttp_advisory = 0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  const auto route = known_non_ru_route();
  bool saw_chromium = false;
  bool saw_firefox = false;

  for (int i = 0; i < 256; i++) {
    const auto unix_time = static_cast<int32>(kUnixTimeBase + i * 139);
    const td::string lower = "runtime-case-alias-win-" + td::to_string(i) + ".fixture.example.com";
    const td::string upper = uppercase_ascii(lower);

    auto profile_lower = pick_runtime_profile(lower, unix_time, params.platform_hints);
    auto profile_upper = pick_runtime_profile(upper, unix_time, params.platform_hints);
    ASSERT_TRUE(profile_lower == profile_upper);

    if (profile_lower == BrowserProfile::Firefox149_Windows) {
      saw_firefox = true;
    } else {
      saw_chromium = true;
    }

    const auto *baseline = get_baseline(family_id_for_windows_profile(profile_lower), Slice("non_ru_egress"));
    ASSERT_TRUE(baseline != nullptr);
    FamilyLaneMatcher matcher(*baseline);

    MockRng rng_lower(0xB11A5000u + static_cast<td::uint64>(i));
    MockRng rng_upper(0xB11A5000u + static_cast<td::uint64>(i));

    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    auto lower_wire = build_runtime_tls_client_hello(lower, kSecret, unix_time, route, rng_lower);

    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    auto upper_wire = build_runtime_tls_client_hello(upper, kSecret, unix_time, route, rng_upper);

    auto lower_parsed_res = parse_tls_client_hello(lower_wire);
    auto upper_parsed_res = parse_tls_client_hello(upper_wire);
    ASSERT_TRUE(lower_parsed_res.is_ok());
    ASSERT_TRUE(upper_parsed_res.is_ok());
    auto lower_parsed = lower_parsed_res.move_as_ok();
    auto upper_parsed = upper_parsed_res.move_as_ok();

    ASSERT_EQ(lower_wire.size(), upper_wire.size());
    ASSERT_TRUE(non_grease_extension_types_without_padding(lower_parsed) ==
                non_grease_extension_types_without_padding(upper_parsed));
    ASSERT_TRUE(lower_parsed.supported_groups == upper_parsed.supported_groups);
    ASSERT_TRUE(lower_parsed.key_share_groups == upper_parsed.key_share_groups);
    ASSERT_EQ(lower_parsed.ech_payload_length, upper_parsed.ech_payload_length);

    auto lower_features = td::mtproto::test::wire_classifier::extract_generated_features(lower_wire);
    auto upper_features = td::mtproto::test::wire_classifier::extract_generated_features(upper_wire);
    ASSERT_EQ(lower_features.wire_length, upper_features.wire_length);
    ASSERT_EQ(lower_features.cipher_count, upper_features.cipher_count);
    ASSERT_EQ(lower_features.extension_count, upper_features.extension_count);
    ASSERT_EQ(lower_features.alpn_count, upper_features.alpn_count);
    ASSERT_EQ(lower_features.supported_groups_count, upper_features.supported_groups_count);
    ASSERT_EQ(lower_features.key_share_count, upper_features.key_share_count);
    ASSERT_EQ(lower_features.ech_payload_length, upper_features.ech_payload_length);
    ASSERT_EQ(lower_features.has_alps, upper_features.has_alps);

    ASSERT_TRUE(matcher.passes_upstream_rule_legality(lower_parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(upper_parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(lower_wire.size(), kWireLengthTolerancePercent));
    if (lower_parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(lower_parsed.ech_payload_length));
    }
    if (upper_parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(upper_parsed.ech_payload_length));
    }

    auto *lower_alpn_ext = find_extension(lower_parsed, kAlpnExtType);
    auto *upper_alpn_ext = find_extension(upper_parsed, kAlpnExtType);
    ASSERT_TRUE(lower_alpn_ext != nullptr);
    ASSERT_TRUE(upper_alpn_ext != nullptr);
    ASSERT_EQ(Slice(kHttp11OnlyAlpnBody), lower_alpn_ext->value);
    ASSERT_EQ(Slice(kHttp11OnlyAlpnBody), upper_alpn_ext->value);
  }

  ASSERT_TRUE(saw_chromium);
  ASSERT_TRUE(saw_firefox);
}

TEST(TlsRuntimeCaseAliasFixtureBlackBox, IosCaseAliasesProduceIdenticalRuntimeWireAndPassFamilyMatchers) {
  RuntimeCaseAliasFixtureGuard guard;

  auto params = default_runtime_stealth_params();
  params.transport_confidence = td::mtproto::stealth::TransportConfidence::Partial;
  params.platform_hints = make_ios_platform();
  params.profile_weights.ios14 = 1;
  params.profile_weights.chrome147_ios_chromium = 1;
  // Pin the verified Apple iOS TLS lane to 0 so this case-alias test keeps
  // exercising exactly the IOS14 and Chrome147_IOSChromium lanes it asserts on.
  params.profile_weights.apple_ios_tls = 0;
  params.profile_weights.chrome133 = 0;
  params.profile_weights.chrome131 = 0;
  params.profile_weights.chrome120 = 0;
  params.profile_weights.chrome147_windows = 0;
  // Runtime params validation requires desktop profile totals to stay
  // non-empty even on mobile test platforms. These ballast weights are
  // not reachable on iOS because allowed_profiles_for_platform(iOS)
  // only exposes IOS14/Chrome147_IOSChromium.
  params.profile_weights.firefox148 = 1;
  params.profile_weights.firefox149_windows = 0;
  params.profile_weights.safari26_3 = 0;
  params.profile_weights.android11_okhttp_advisory = 0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  const auto route = known_non_ru_route();
  bool saw_ios14 = false;
  bool saw_ios_chromium = false;

  for (int i = 0; i < 256; i++) {
    const auto unix_time = static_cast<int32>(kUnixTimeBase + i * 149);
    const td::string lower = "runtime-case-alias-ios-" + td::to_string(i) + ".fixture.example.com";
    const td::string upper = uppercase_ascii(lower);

    auto profile_lower = pick_runtime_profile(lower, unix_time, params.platform_hints);
    auto profile_upper = pick_runtime_profile(upper, unix_time, params.platform_hints);
    ASSERT_TRUE(profile_lower == profile_upper);

    if (profile_lower == BrowserProfile::IOS14) {
      saw_ios14 = true;
    } else {
      saw_ios_chromium = true;
    }

    const auto *baseline = get_baseline(family_id_for_ios_profile(profile_lower), Slice("non_ru_egress"));
    ASSERT_TRUE(baseline != nullptr);
    FamilyLaneMatcher matcher(*baseline);

    MockRng rng_lower(0xC11A5000u + static_cast<td::uint64>(i));
    MockRng rng_upper(0xC11A5000u + static_cast<td::uint64>(i));

    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    auto lower_wire = build_runtime_tls_client_hello(lower, kSecret, unix_time, route, rng_lower);

    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    auto upper_wire = build_runtime_tls_client_hello(upper, kSecret, unix_time, route, rng_upper);

    auto lower_parsed_res = parse_tls_client_hello(lower_wire);
    auto upper_parsed_res = parse_tls_client_hello(upper_wire);
    ASSERT_TRUE(lower_parsed_res.is_ok());
    ASSERT_TRUE(upper_parsed_res.is_ok());
    auto lower_parsed = lower_parsed_res.move_as_ok();
    auto upper_parsed = upper_parsed_res.move_as_ok();

    ASSERT_EQ(lower_wire.size(), upper_wire.size());
    ASSERT_TRUE(non_grease_extension_types_without_padding(lower_parsed) ==
                non_grease_extension_types_without_padding(upper_parsed));
    ASSERT_TRUE(lower_parsed.supported_groups == upper_parsed.supported_groups);
    ASSERT_TRUE(lower_parsed.key_share_groups == upper_parsed.key_share_groups);
    ASSERT_EQ(lower_parsed.ech_payload_length, upper_parsed.ech_payload_length);

    auto lower_features = td::mtproto::test::wire_classifier::extract_generated_features(lower_wire);
    auto upper_features = td::mtproto::test::wire_classifier::extract_generated_features(upper_wire);
    ASSERT_EQ(lower_features.wire_length, upper_features.wire_length);
    ASSERT_EQ(lower_features.cipher_count, upper_features.cipher_count);
    ASSERT_EQ(lower_features.extension_count, upper_features.extension_count);
    ASSERT_EQ(lower_features.alpn_count, upper_features.alpn_count);
    ASSERT_EQ(lower_features.supported_groups_count, upper_features.supported_groups_count);
    ASSERT_EQ(lower_features.key_share_count, upper_features.key_share_count);
    ASSERT_EQ(lower_features.ech_payload_length, upper_features.ech_payload_length);
    ASSERT_EQ(lower_features.has_alps, upper_features.has_alps);

    ASSERT_TRUE(matcher.passes_upstream_rule_legality(lower_parsed));
    ASSERT_TRUE(matcher.passes_upstream_rule_legality(upper_parsed));
    ASSERT_TRUE(matcher.within_wire_length_envelope(lower_wire.size(), kWireLengthTolerancePercent));
    if (lower_parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(lower_parsed.ech_payload_length));
    }
    if (upper_parsed.ech_payload_length != 0) {
      ASSERT_TRUE(matcher.covers_observed_ech_payload_length(upper_parsed.ech_payload_length));
    }

    auto *lower_alpn_ext = find_extension(lower_parsed, kAlpnExtType);
    auto *upper_alpn_ext = find_extension(upper_parsed, kAlpnExtType);
    ASSERT_TRUE(lower_alpn_ext != nullptr);
    ASSERT_TRUE(upper_alpn_ext != nullptr);
    ASSERT_EQ(Slice(kHttp11OnlyAlpnBody), lower_alpn_ext->value);
    ASSERT_EQ(Slice(kHttp11OnlyAlpnBody), upper_alpn_ext->value);
  }

  ASSERT_TRUE(saw_ios14);
  ASSERT_TRUE(saw_ios_chromium);
}

}  // namespace

#endif
