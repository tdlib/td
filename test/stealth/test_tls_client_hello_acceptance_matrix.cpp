// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// For every BrowserProfile x EchMode combination, generate a
// ClientHello via the builder and assert the in-tree parser accepts
// the resulting wire. No socket pair is driven by this test.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

static const BrowserProfile kAllProfiles[] = {BrowserProfile::Chrome133,
                                              BrowserProfile::Chrome131,
                                              BrowserProfile::Chrome120,
                                              BrowserProfile::Chrome147_Windows,
                                              BrowserProfile::Chrome147_IOSChromium,
                                              BrowserProfile::Firefox148,
                                              BrowserProfile::Firefox149_MacOS26_3,
                                              BrowserProfile::Safari26_3,
                                              BrowserProfile::IOS14,
                                              BrowserProfile::Android11_OkHttp_Advisory};

static const EchMode kAllEchModes[] = {EchMode::Disabled, EchMode::Rfc9180Outer};

TEST(TLS_ClientHelloAcceptanceMatrix, BrowserProfileTimesEchModeRoundTripsThroughParser) {
  for (auto profile : kAllProfiles) {
    for (auto ech : kAllEchModes) {
      MockRng rng(1234567u);
      auto wire =
          build_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678, profile, ech, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
      ASSERT_EQ(static_cast<td::uint8>(0x16), parsed.ok_ref().record_type);
      ASSERT_EQ(static_cast<td::uint8>(0x01), parsed.ok_ref().handshake_type);
    }
  }
}

TEST(TLS_ClientHelloAcceptanceMatrix, ProxyBuilderVariantAlsoRoundTripsForAllProfiles) {
  for (auto profile : kAllProfiles) {
    for (auto ech : kAllEchModes) {
      MockRng rng(7654321u);
      auto wire = build_proxy_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678, profile,
                                                           ech, rng);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

}  // namespace
