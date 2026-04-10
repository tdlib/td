// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: ALPN extension presence for all profiles.
//
// From the research findings: "Modern browsers hard-prioritize h2 or h3,
// making the value 00 an immediate indicator of non-browser scripts,
// malware, or non-standard user protocols." [JA4 spec]
//
// Missing ALPN (extension 0x0010) is the single easiest DPI signal
// to distinguish Telegram proxy traffic from legitimate browsers.
// These tests verify ALPN is present for ALL profiles with correct values.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::build_proxy_tls_client_hello;
using td::mtproto::stealth::build_proxy_tls_client_hello_for_profile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::TlsReader;

constexpr td::uint16 kAlpnExtType = 0x0010;

struct AlpnProto {
  td::string name;
};

td::vector<AlpnProto> parse_alpn_protocols(td::Slice ext_value) {
  td::vector<AlpnProto> result;
  if (ext_value.empty()) {
    return result;
  }
  TlsReader reader(ext_value);
  auto r_list_len = reader.read_u16();
  if (r_list_len.is_error()) {
    return result;
  }
  while (reader.left() > 0) {
    auto r_proto_len = reader.read_u8();
    if (r_proto_len.is_error()) {
      break;
    }
    auto proto_len = r_proto_len.ok();
    auto r_proto = reader.read_slice(proto_len);
    if (r_proto.is_error()) {
      break;
    }
    result.push_back({r_proto.ok().str()});
  }
  return result;
}

TEST(TlsAlpnPresenceAllProfiles, AllBrowserProfilesMustAdvertiseAlpn) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), kAlpnExtType);
    // Missing ALPN = JA4 "00" = immediate DPI detection flag
    ASSERT_TRUE(alpn != nullptr);
  }
}

TEST(TlsAlpnPresenceAllProfiles, BrowserDefaultMustIncludeH2) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), kAlpnExtType);
    ASSERT_TRUE(alpn != nullptr);
    auto protos = parse_alpn_protocols(alpn->value);
    bool has_h2 = false;
    for (const auto &p : protos) {
      if (p.name == "h2") {
        has_h2 = true;
      }
    }
    ASSERT_TRUE(has_h2);
  }
}

TEST(TlsAlpnPresenceAllProfiles, BrowserDefaultMustIncludeHttp11) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), kAlpnExtType);
    ASSERT_TRUE(alpn != nullptr);
    auto protos = parse_alpn_protocols(alpn->value);
    bool has_http11 = false;
    for (const auto &p : protos) {
      if (p.name == "http/1.1") {
        has_http11 = true;
      }
    }
    ASSERT_TRUE(has_http11);
  }
}

TEST(TlsAlpnPresenceAllProfiles, ProxyAlpnMustOnlyContainHttp11) {
  BrowserProfile chrome_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Chrome131, BrowserProfile::Chrome120};
  for (auto profile : chrome_profiles) {
    MockRng rng(42);
    auto wire = build_proxy_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                         EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), kAlpnExtType);
    ASSERT_TRUE(alpn != nullptr);
    auto protos = parse_alpn_protocols(alpn->value);
    // Proxy mode: http/1.1 only, no h2
    ASSERT_EQ(1u, protos.size());
    ASSERT_EQ(td::string("http/1.1"), protos[0].name);
  }
}

TEST(TlsAlpnPresenceAllProfiles, BrowserDefaultAlpnMustHaveH2First) {
  // Real Chrome/Firefox/Safari all place h2 before http/1.1 in ALPN.
  // Reversed order would be a DPI anomaly.
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133,
                                   BrowserProfile::Chrome131,
                                   BrowserProfile::Chrome120,
                                   BrowserProfile::Firefox148,
                                   BrowserProfile::Safari26_3,
                                   BrowserProfile::IOS14,
                                   BrowserProfile::Android11_OkHttp_Advisory};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), kAlpnExtType);
    ASSERT_TRUE(alpn != nullptr);
    auto protos = parse_alpn_protocols(alpn->value);
    ASSERT_TRUE(protos.size() >= 2u);
    ASSERT_EQ(td::string("h2"), protos[0].name);
    ASSERT_EQ(td::string("http/1.1"), protos[1].name);
  }
}

TEST(TlsAlpnPresenceAllProfiles, DefaultBuildRoutesMustHaveAlpn) {
  // Test build_default and build_proxy paths as well
  NetworkRouteHints non_ru_hints;
  non_ru_hints.is_known = true;
  non_ru_hints.is_ru = false;

  auto wire_default = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, non_ru_hints);
  auto parsed_default = parse_tls_client_hello(wire_default);
  ASSERT_TRUE(parsed_default.is_ok());
  ASSERT_TRUE(find_extension(parsed_default.ok(), kAlpnExtType) != nullptr);

  auto wire_proxy = build_proxy_tls_client_hello("www.google.com", "0123456789secret", 1712345678, non_ru_hints);
  auto parsed_proxy = parse_tls_client_hello(wire_proxy);
  ASSERT_TRUE(parsed_proxy.is_ok());
  ASSERT_TRUE(find_extension(parsed_proxy.ok(), kAlpnExtType) != nullptr);
}

TEST(TlsAlpnPresenceAllProfiles, RuRoutesMustStillHaveAlpn) {
  NetworkRouteHints ru_hints;
  ru_hints.is_known = true;
  ru_hints.is_ru = true;

  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, ru_hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  // Even on RU routes (ECH disabled), ALPN must be present
  ASSERT_TRUE(find_extension(parsed.ok(), kAlpnExtType) != nullptr);
}

TEST(TlsAlpnPresenceAllProfiles, UnknownRoutesMustStillHaveAlpn) {
  NetworkRouteHints unknown_hints;
  unknown_hints.is_known = false;

  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, unknown_hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), kAlpnExtType) != nullptr);
}

TEST(TlsAlpnPresenceAllProfiles, AlpnProtocolStringsMustNotContainNullBytes) {
  BrowserProfile all_profiles[] = {BrowserProfile::Chrome133, BrowserProfile::Firefox148, BrowserProfile::Safari26_3};
  for (auto profile : all_profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto *alpn = find_extension(parsed.ok(), kAlpnExtType);
    ASSERT_TRUE(alpn != nullptr);
    auto protos = parse_alpn_protocols(alpn->value);
    for (const auto &p : protos) {
      for (char c : p.name) {
        ASSERT_NE('\0', c);
      }
    }
  }
}

}  // namespace
