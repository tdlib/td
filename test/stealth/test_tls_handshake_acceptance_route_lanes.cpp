// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Route-lane invariants for the ECH extension (0xFE0D) in the
// ClientHello builder:
//   (a) non_ru_egress lane with ECH mode on  -> ECH extension present
//   (b) ru_egress lane with ECH mode off     -> ECH extension absent
//   (c) unknown route with ECH mode off      -> ECH extension absent
// No socket pair driven by this test.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::profile_spec;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::uint16 kEchExtType = 0xFE0D;

static const BrowserProfile kAllProfiles[] = {
    BrowserProfile::Chrome133, BrowserProfile::Chrome131,           BrowserProfile::Chrome120,
    BrowserProfile::Firefox148, BrowserProfile::Firefox149_MacOS26_3, BrowserProfile::Safari26_3,
    BrowserProfile::IOS14,     BrowserProfile::Android11_OkHttp_Advisory};

TEST(TLS_HandshakeAcceptanceRouteLanes, NonRuEgressWithEchOnPresentsEchExtension) {
  // Profiles whose spec disables ECH (e.g. Safari26_3, IOS14,
  // Android11_OkHttp_Advisory) must never emit it regardless of the
  // requested EchMode; profiles whose spec allows it must emit it when
  // the caller asks for Rfc9180Outer. Assert both halves.
  for (auto profile : kAllProfiles) {
    MockRng rng(111u);
    auto wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Rfc9180Outer, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *ext = find_extension(parsed.ok_ref(), kEchExtType);
    if (profile_spec(profile).allows_ech) {
      ASSERT_TRUE(ext != nullptr);
    } else {
      ASSERT_TRUE(ext == nullptr);
    }
  }
}

TEST(TLS_HandshakeAcceptanceRouteLanes, RuEgressOmitsEchExtension) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;
  auto wire = build_default_tls_client_hello("www.example.com", "0123456789secret", 1712345678, ru_hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok_ref(), kEchExtType) == nullptr);
}

TEST(TLS_HandshakeAcceptanceRouteLanes, UnknownRouteOmitsEchExtension) {
  NetworkRouteHints unknown_hints;
  unknown_hints.is_known = false;
  auto wire = build_default_tls_client_hello("www.example.com", "0123456789secret", 1712345678, unknown_hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok_ref(), kEchExtType) == nullptr);
}

TEST(TLS_HandshakeAcceptanceRouteLanes, ProfileRouteLanesFoldIntoThreeExpectations) {
  // For every profile, disabling ECH at the per-profile builder must
  // drop the ECH extension regardless of other route hints, mirroring
  // the default-builder ru_egress / unknown lanes above.
  for (auto profile : kAllProfiles) {
    MockRng rng(222u);
    auto wire = build_tls_client_hello_for_profile("www.example.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok_ref(), kEchExtType) == nullptr);
  }
}

}  // namespace
