// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// For each BrowserProfile, load a reviewed ServerHello fixture from
// `test/analysis/fixtures/serverhello/**/*.serverhello.json`,
// synthesize a well-formed TLS 1.3 ServerHello wire from the fixture's
// structured metadata, parse it and assert the selected TLS version
// reported via supported_versions is TLS 1.3 (0x0304). No socket pair.

#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::test::load_server_hello_fixture_relative;
using td::mtproto::test::parse_tls_server_hello;
using td::mtproto::test::representative_server_hello_path_for_family;
using td::mtproto::test::synthesize_server_hello_wire;

struct ProfileFamilyHint {
  BrowserProfile profile;
  const char *family_hint;
};

static const ProfileFamilyHint kProfilePairings[] = {
    {BrowserProfile::Chrome133, "chrome"},
    {BrowserProfile::Chrome131, "chrome"},
    {BrowserProfile::Chrome120, "chrome"},
    {BrowserProfile::Firefox148, "firefox148"},
    {BrowserProfile::Firefox149_MacOS26_3, "firefox149"},
    {BrowserProfile::Safari26_3, "safari"},
    {BrowserProfile::IOS14, "ios"},
    {BrowserProfile::Android11_OkHttp_Advisory, "android"},
};

TEST(TLS_FullHandshakeFamilyPairing, EveryProfileResolvesToReviewedTls13ServerHello) {
  for (const auto &pairing : kProfilePairings) {
    auto relative = representative_server_hello_path_for_family(td::Slice(pairing.family_hint));
    auto r_sample = load_server_hello_fixture_relative(td::CSlice(relative.str()));
    ASSERT_TRUE(r_sample.is_ok());
    auto sample = r_sample.move_as_ok();

    auto wire = synthesize_server_hello_wire(sample);
    auto parsed = parse_tls_server_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(static_cast<td::uint16>(0x0304), parsed.ok_ref().supported_version_extension_value);
    ASSERT_EQ(static_cast<td::uint8>(0x02), parsed.ok_ref().handshake_type);
  }
}

TEST(TLS_FullHandshakeFamilyPairing, ReviewedCipherSuiteIsNonZeroForEveryProfile) {
  for (const auto &pairing : kProfilePairings) {
    auto relative = representative_server_hello_path_for_family(td::Slice(pairing.family_hint));
    auto r_sample = load_server_hello_fixture_relative(td::CSlice(relative.str()));
    ASSERT_TRUE(r_sample.is_ok());
    auto sample = r_sample.move_as_ok();
    ASSERT_TRUE(sample.cipher_suite != 0);
  }
}

}  // namespace
