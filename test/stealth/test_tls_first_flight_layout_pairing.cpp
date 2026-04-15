// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// For each BrowserProfile, generate a ClientHello, parse it, then load
// a reviewed ServerHello fixture for the same family, synthesize a
// ServerHello wire and parse it. Assert that the cipher_suite chosen by
// the reviewed ServerHello appears in the ClientHello's advertised
// cipher_suites list — the core first-flight layout invariant that the
// server never selects a cipher the client didn't offer.
// No socket pair driven by this test.

#include "test/stealth/MockRng.h"
#include "test/stealth/ServerHelloFixtureLoader.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::load_server_hello_fixture_relative;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
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

bool cipher_suite_advertised(td::Slice cipher_suites_bytes, td::uint16 suite) {
  // cipher_suites is a raw byte slice of u16 values (big-endian).
  if ((cipher_suites_bytes.size() % 2) != 0) {
    return false;
  }
  for (size_t i = 0; i + 1 < cipher_suites_bytes.size(); i += 2) {
    td::uint16 advertised =
        static_cast<td::uint16>((static_cast<td::uint8>(cipher_suites_bytes[i]) << 8) |
                                 static_cast<td::uint8>(cipher_suites_bytes[i + 1]));
    if (advertised == suite) {
      return true;
    }
  }
  return false;
}

TEST(TLS_FirstFlightLayoutPairing, ReviewedServerCipherAppearsInClientHelloOffer) {
  for (const auto &pairing : kProfilePairings) {
    MockRng rng(555u);
    auto ch_wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678,
                                                      pairing.profile, EchMode::Disabled, rng);
    auto ch_parsed = parse_tls_client_hello(ch_wire);
    ASSERT_TRUE(ch_parsed.is_ok());

    auto relative = representative_server_hello_path_for_family(td::Slice(pairing.family_hint));
    auto r_sample = load_server_hello_fixture_relative(td::CSlice(relative.str()));
    ASSERT_TRUE(r_sample.is_ok());
    auto sample = r_sample.move_as_ok();

    auto sh_wire = synthesize_server_hello_wire(sample);
    auto sh_parsed = parse_tls_server_hello(sh_wire);
    ASSERT_TRUE(sh_parsed.is_ok());

    auto &ch = ch_parsed.ok_ref();
    auto &sh = sh_parsed.ok_ref();
    ASSERT_TRUE(cipher_suite_advertised(ch.cipher_suites, sh.cipher_suite));
  }
}

TEST(TLS_FirstFlightLayoutPairing, ClientHelloAdvertisesAtLeastOneReviewedTls13Suite) {
  // Sanity mirror: every profile must advertise one of the three
  // reviewed TLS 1.3 cipher suites (0x1301/0x1302/0x1303).
  for (const auto &pairing : kProfilePairings) {
    MockRng rng(777u);
    auto ch_wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678,
                                                      pairing.profile, EchMode::Disabled, rng);
    auto ch_parsed = parse_tls_client_hello(ch_wire);
    ASSERT_TRUE(ch_parsed.is_ok());
    auto &ch = ch_parsed.ok_ref();
    bool any = cipher_suite_advertised(ch.cipher_suites, 0x1301) ||
               cipher_suite_advertised(ch.cipher_suites, 0x1302) ||
               cipher_suite_advertised(ch.cipher_suites, 0x1303);
    ASSERT_TRUE(any);
  }
}

}  // namespace
