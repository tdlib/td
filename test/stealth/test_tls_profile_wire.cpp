// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

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
                       BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp}) {
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

TEST(TlsProfileWire, FixedProfilesDoNotEmitChromiumOnlyExtensions) {
  for (auto profile : {BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp}) {
    auto spec = profile_spec(profile);
    ASSERT_FALSE(spec.allows_ech);
    ASSERT_FALSE(spec.allows_padding);
    ASSERT_FALSE(spec.has_pq);
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
    ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
    ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
    ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridGroup) == 0);
    ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) == 0);
  }
}

TEST(TlsProfileWire, FixedProfilesKeepStableExtensionOrderAcrossSeeds) {
  for (auto profile : {BrowserProfile::Safari26_3, BrowserProfile::IOS14, BrowserProfile::Android11_OkHttp}) {
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