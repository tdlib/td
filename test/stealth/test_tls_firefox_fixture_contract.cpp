// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"
#include "test/stealth/ReviewedClientHelloReferences.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <limits>

namespace {

using td::int32;
using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::ParsedClientHello;
using td::Slice;
using td::string;
using td::uint16;
using td::uint64;
using namespace td::mtproto::test::fixtures::reviewed_refs;

constexpr Slice kDomain("www.google.com");
constexpr Slice kSecret("0123456789secret");
constexpr int32 kUnixTime = 1712345678;
constexpr uint16 kSupportedVersionsExtensionType = 0x002B;
constexpr uint64 kQuickSeeds[] = {0, 1, std::numeric_limits<uint64>::max()};

string build_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  MockRng rng(seed);
  return build_tls_client_hello_for_profile(kDomain.str(), kSecret, kUnixTime, profile, ech_mode, rng);
}

ParsedClientHello parse_profile_hello(BrowserProfile profile, EchMode ech_mode, uint64 seed) {
  auto parsed = parse_tls_client_hello(build_profile_hello(profile, ech_mode, seed));
  CHECK(parsed.is_ok());
  return parsed.move_as_ok();
}

Slice require_supported_versions_body(const ParsedClientHello &hello) {
  auto *supported_versions = find_extension(hello, kSupportedVersionsExtensionType);
  CHECK(supported_versions != nullptr);
  return supported_versions->value;
}

TEST(FirefoxFixtureContract, Firefox148EchMetadataMatchesReviewedLinuxFixtureAcrossSeeds) {
  for (auto seed : kQuickSeeds) {
    auto hello = parse_profile_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, seed);
    ASSERT_EQ(firefox_linux_desktop_ref_ech_outer_type, hello.ech_outer_type);
    ASSERT_EQ(firefox_linux_desktop_ref_ech_kdf_id, hello.ech_kdf_id);
    ASSERT_EQ(firefox_linux_desktop_ref_ech_aead_id, hello.ech_aead_id);
    ASSERT_EQ(firefox_linux_desktop_ref_ech_enc_length, hello.ech_declared_enc_length);
    ASSERT_EQ(firefox_linux_desktop_ref_ech_enc_length, hello.ech_actual_enc_length);
    ASSERT_EQ(firefox_linux_desktop_ref_ech_payload_length, hello.ech_payload_length);
  }
}

TEST(FirefoxFixtureContract, Firefox148SupportedVersionsBodyMatchesReviewedFixtureExactly) {
  const string expected_supported_versions("\x04\x03\x04\x03\x03", 5);
  for (auto seed : kQuickSeeds) {
    auto hello = parse_profile_hello(BrowserProfile::Firefox148, EchMode::Disabled, seed);
    ASSERT_EQ(expected_supported_versions, require_supported_versions_body(hello).str());
  }
}

TEST(FirefoxFixtureContract, Firefox149MacosKeepsDistinctReviewedEchMetadataFromFirefox148) {
  for (auto seed : kQuickSeeds) {
    auto firefox148 = parse_profile_hello(BrowserProfile::Firefox148, EchMode::Rfc9180Outer, seed);
    auto firefox149 = parse_profile_hello(BrowserProfile::Firefox149_MacOS26_3, EchMode::Rfc9180Outer, seed);

    ASSERT_EQ(firefox149_macos26_3_ech_outer_type, firefox149.ech_outer_type);
    ASSERT_EQ(firefox149_macos26_3_ech_kdf_id, firefox149.ech_kdf_id);
    ASSERT_EQ(firefox149_macos26_3_ech_aead_id, firefox149.ech_aead_id);
    ASSERT_EQ(firefox149_macos26_3_ech_enc_length, firefox149.ech_declared_enc_length);
    ASSERT_EQ(firefox149_macos26_3_ech_enc_length, firefox149.ech_actual_enc_length);
    ASSERT_EQ(firefox149_macos26_3_ech_payload_length, firefox149.ech_payload_length);

    ASSERT_NE(firefox149.ech_aead_id, firefox148.ech_aead_id);
    ASSERT_NE(firefox149.ech_payload_length, firefox148.ech_payload_length);
  }
}

}  // namespace