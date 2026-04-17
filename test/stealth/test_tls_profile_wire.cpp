// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/BrowserProfile.h"

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::profile_spec;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

td::string build_profile(BrowserProfile profile, EchMode ech_mode, td::uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile, ech_mode, rng);
}

std::vector<td::uint16> extension_types(const td::mtproto::test::ParsedClientHello &hello) {
  std::vector<td::uint16> result;
  result.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    result.push_back(td::mtproto::test::is_grease_value(ext.type) ? static_cast<td::uint16>(0x0A0A) : ext.type);
  }
  return result;
}

TEST(TlsProfileWire, Chrome133UsesChrome133AlpsAndHybridPq) {
  auto wire = build_profile(BrowserProfile::Chrome133, EchMode::Rfc9180Outer, 1);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) == nullptr);

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                  parsed.ok().key_share_groups.end());
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
}

TEST(TlsProfileWire, Chrome131UsesChrome131AlpsAndHybridPq) {
  auto wire = build_profile(BrowserProfile::Chrome131, EchMode::Rfc9180Outer, 2);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr);

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                  parsed.ok().key_share_groups.end());
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
}

TEST(TlsProfileWire, Chrome120OmitsPqHybridGroupsAndKeepsOnlyX25519KeyShare) {
  auto wire = build_profile(BrowserProfile::Chrome120, EchMode::Rfc9180Outer, 3);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr);

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                  parsed.ok().key_share_groups.end());
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);

  td::uint32 non_grease_entries = 0;
  for (const auto &entry : parsed.ok().key_share_entries) {
    if (td::mtproto::test::is_grease_value(entry.group)) {
      continue;
    }
    non_grease_entries++;
    ASSERT_EQ(td::mtproto::test::fixtures::kX25519Group, entry.group);
    ASSERT_EQ(td::mtproto::test::fixtures::kX25519KeyShareLength, entry.key_length);
  }
  ASSERT_EQ(1u, non_grease_entries);
}

TEST(TlsProfileWire, Chrome147WindowsHasDedicatedBrowserProfileSpec) {
  const auto &profile = td::mtproto::get_profile_spec(BrowserProfile::Chrome147_Windows);
  ASSERT_EQ("chrome147_windows", profile.name);
}

TEST(TlsProfileWire, DisabledEchModeRemovesEchExtensionForChromiumProfiles) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    auto wire = build_profile(profile, EchMode::Disabled, static_cast<td::uint64>(profile) + 17);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
  }
}

TEST(TlsProfileWire, EnabledEchModeUsesFe0dOnlyForChromiumProfiles) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120}) {
    auto wire = build_profile(profile, EchMode::Rfc9180Outer, static_cast<td::uint64>(profile) + 33);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
    ASSERT_EQ(parsed.ok().ech_declared_enc_length, parsed.ok().ech_actual_enc_length);
  }
}

TEST(TlsProfileWire, Firefox148MatchesObservedModernFirefoxCaptureFamily) {
  auto spec = profile_spec(BrowserProfile::Firefox148);
  ASSERT_TRUE(spec.allows_ech);
  ASSERT_FALSE(spec.allows_padding);
  ASSERT_TRUE(spec.has_pq);
  ASSERT_EQ(0x11ECu, spec.pq_group_id);
  ASSERT_EQ(0x4001u, spec.record_size_limit);

  auto wire = build_profile(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, 81);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0015) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0022) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x001c) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x001b) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0023) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x002d) != nullptr);

  std::vector<td::uint16> expected_order = {0x0000,
                                            0x0017,
                                            0xff01,
                                            0x000a,
                                            0x000b,
                                            0x0023,
                                            0x0010,
                                            0x0005,
                                            0x0022,
                                            0x0012,
                                            0x0033,
                                            0x002b,
                                            0x000d,
                                            0x002d,
                                            0x001c,
                                            0x001b,
                                            td::mtproto::test::fixtures::kEchExtensionType};
  ASSERT_EQ(expected_order, extension_types(parsed.ok()));

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kX25519Group) != 0);
  ASSERT_TRUE(supported_groups.count(0x0017) != 0);
  ASSERT_TRUE(supported_groups.count(0x0018) != 0);
  ASSERT_TRUE(supported_groups.count(0x0019) != 0);
  ASSERT_TRUE(supported_groups.count(0x0100) != 0);
  ASSERT_TRUE(supported_groups.count(0x0101) != 0);

  std::unordered_map<td::uint16, td::uint16> key_lengths;
  for (const auto &entry : parsed.ok().key_share_entries) {
    key_lengths[entry.group] = entry.key_length;
  }
  ASSERT_EQ(td::mtproto::test::fixtures::kPqHybridKeyShareLength,
            key_lengths[td::mtproto::test::fixtures::kPqHybridGroup]);
  ASSERT_EQ(td::mtproto::test::fixtures::kX25519KeyShareLength, key_lengths[td::mtproto::test::fixtures::kX25519Group]);
  ASSERT_EQ(65u, key_lengths[0x0017]);
}

TEST(TlsProfileWire, NonFirefoxProfilesDoNotAdvertiseRecordSizeLimitMetadata) {
  for (auto profile : {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120,
                       BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory}) {
    ASSERT_EQ(0u, profile_spec(profile).record_size_limit);
  }
}

TEST(TlsProfileWire, Firefox148DisablesFe0dOnFailClosedRoutes) {
  auto wire = build_profile(BrowserProfile::Firefox148, EchMode::Disabled, 82);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0022) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x001c) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x001b) != nullptr);
}

TEST(TlsProfileWire, Safari26_3UsesFixedWebkitProfileWithoutEchOrAlps) {
  auto spec = profile_spec(BrowserProfile::Safari26_3);
  ASSERT_FALSE(spec.allows_ech);
  ASSERT_FALSE(spec.allows_padding);
  ASSERT_TRUE(spec.has_pq);
  ASSERT_EQ(td::mtproto::test::fixtures::kPqHybridGroup, spec.pq_group_id);
  ASSERT_EQ(0u, spec.alps_type);

  auto wire = build_profile(BrowserProfile::Safari26_3, EchMode::Rfc9180Outer, 51);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0015) == nullptr);

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                  parsed.ok().key_share_groups.end());
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
}

TEST(TlsProfileWire, IosAndAndroidFixedProfilesDoNotEmitChromiumOnlyExtensions) {
  // IOS14 represents the Apple TLS family on iOS 26.x, which DOES carry
  // X25519MLKEM768 in both supported_groups and key_share per real
  // captures under test/analysis/fixtures/clienthello/ios/ — see
  // `test_profile_spec_pq_consistency_invariants.cpp` and
  // `test_pq_hybrid_key_share_format_invariants.cpp` for the regression
  // guards. The Chromium-only ECH/ALPS/padding checks below still apply
  // (Apple TLS does NOT carry those), but PQ is now part of the Apple TLS
  // family contract and is asserted positively elsewhere.
  for (auto profile : {BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory}) {
    auto spec = profile_spec(profile);
    ASSERT_FALSE(spec.allows_ech);
    ASSERT_FALSE(spec.allows_padding);
    ASSERT_EQ(0u, spec.alps_type);

    auto wire = build_profile(profile, EchMode::Rfc9180Outer, static_cast<td::uint64>(profile) + 51);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome131) == nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) == nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), 0x0015) == nullptr);

    std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                    parsed.ok().supported_groups.end());
    std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                    parsed.ok().key_share_groups.end());
    // Legacy Kyber draft codepoint (0x6399) MUST never appear; only the
    // IANA-final X25519MLKEM768 (0x11EC) is acceptable when PQ is present.
    ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
    ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
    if (profile == BrowserProfile::Android11_OkHttp_Advisory) {
      // Android OkHttp predates PQ adoption — must not advertise it.
      ASSERT_TRUE(spec.has_pq == false);
      ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
      ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
    } else {
      // IOS14 (Apple TLS family on iOS 26.x) DOES advertise PQ.
      ASSERT_TRUE(spec.has_pq == true);
      ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
      ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
    }
  }
}

TEST(TlsProfileWire, IosChromiumProfileUsesChromiumMobileEchShape) {
  auto spec = profile_spec(BrowserProfile::Chrome147_IOSChromium);
  ASSERT_TRUE(spec.allows_ech);
  ASSERT_FALSE(spec.allows_padding);
  ASSERT_TRUE(spec.has_pq);
  ASSERT_EQ(td::mtproto::test::fixtures::kPqHybridGroup, spec.pq_group_id);
  ASSERT_EQ(td::mtproto::test::fixtures::kAlpsChrome133Plus, spec.alps_type);
  ASSERT_TRUE(spec.extension_order_policy == td::mtproto::stealth::ExtensionOrderPolicy::FixedFromFixture);

  auto wire = build_profile(BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, 93);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0029) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0023) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0015) == nullptr);

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                  parsed.ok().key_share_groups.end());
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kX25519Group) != 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kX25519Group) != 0);
}

TEST(TlsProfileWire, IosChromiumFailClosedRouteDropsEchAndPskExtensions) {
  auto wire = build_profile(BrowserProfile::Chrome147_IOSChromium, EchMode::Disabled, 94);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0029) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kAlpsChrome133Plus) != nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0x0015) == nullptr);
}

TEST(TlsProfileWire, IosChromiumKeepsStableExtensionOrderAcrossSeeds) {
  auto first = parse_tls_client_hello(build_profile(BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, 95));
  auto second = parse_tls_client_hello(build_profile(BrowserProfile::Chrome147_IOSChromium, EchMode::Rfc9180Outer, 96));
  ASSERT_TRUE(first.is_ok());
  ASSERT_TRUE(second.is_ok());
  ASSERT_EQ(extension_types(first.ok()), extension_types(second.ok()));
}

TEST(TlsProfileWire, FixedProfilesKeepStableExtensionOrderAcrossSeeds) {
  for (auto profile : {BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp_Advisory}) {
    auto spec = profile_spec(profile);
    ASSERT_TRUE(spec.extension_order_policy == td::mtproto::stealth::ExtensionOrderPolicy::FixedFromFixture);

    auto first =
        parse_tls_client_hello(build_profile(profile, EchMode::Disabled, static_cast<td::uint64>(profile) + 71));
    auto second =
        parse_tls_client_hello(build_profile(profile, EchMode::Disabled, static_cast<td::uint64>(profile) + 72));
    ASSERT_TRUE(first.is_ok());
    ASSERT_TRUE(second.is_ok());
    ASSERT_EQ(extension_types(first.ok()), extension_types(second.ok()));
  }
}

}  // namespace