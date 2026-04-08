// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Security tests: Verify that TlsHelloCalcLength and TlsHelloStore always
// agree on total wire length for ALL profiles and parameter combinations.
// A disagreement could cause buffer overrun/underrun — a critical memory
// safety vulnerability exploitable by a MitM or malicious proxy server.

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::BrowserProfile;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsBuilderLengthAgreementSecurity, AllProfilesAllEchModesMustProduceValidWireImages) {
  // Exhaustive: every profile x ECH mode x 50 seeds must produce a
  // parseable ClientHello (implying CalcLength == Store length).
  auto profiles = td::mtproto::stealth::all_profiles();
  for (auto profile : profiles) {
    for (auto ech_mode : {EchMode::Disabled, EchMode::Rfc9180Outer}) {
      for (td::uint64 seed = 1; seed <= 50; seed++) {
        MockRng rng(seed);
        auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                       ech_mode, rng);
        ASSERT_TRUE(wire.size() >= 43u);

        // The parser performs full structural validation including scope
        // length checks. If CalcLength and Store disagreed, the parser
        // would detect the structural corruption.
        auto parsed = parse_tls_client_hello(wire);
        ASSERT_TRUE(parsed.is_ok());
      }
    }
  }
}

TEST(TlsBuilderLengthAgreementSecurity, RecordLengthMustMatchActualPayload) {
  // The first 5 bytes are TLS record header: type(1) + version(2) + length(2).
  // Verify length field matches actual remaining bytes.
  auto profiles = td::mtproto::stealth::all_profiles();
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 5u);
    auto declared_length =
        (static_cast<td::uint16>(static_cast<td::uint8>(wire[3])) << 8) | static_cast<td::uint8>(wire[4]);
    ASSERT_EQ(static_cast<size_t>(declared_length), wire.size() - 5u);
  }
}

TEST(TlsBuilderLengthAgreementSecurity, HandshakeLengthMustMatchActualPayload) {
  // Bytes 5-8: handshake type(1) + length(3).
  auto profiles = td::mtproto::stealth::all_profiles();
  for (auto profile : profiles) {
    MockRng rng(42);
    auto wire = build_tls_client_hello_for_profile("www.google.com", "0123456789secret", 1712345678, profile,
                                                   EchMode::Disabled, rng);
    ASSERT_TRUE(wire.size() >= 9u);
    auto handshake_length = (static_cast<td::uint32>(static_cast<td::uint8>(wire[6])) << 16) |
                            (static_cast<td::uint32>(static_cast<td::uint8>(wire[7])) << 8) |
                            static_cast<td::uint32>(static_cast<td::uint8>(wire[8]));
    // Handshake payload starts at byte 9 and extends to end of record.
    ASSERT_EQ(static_cast<size_t>(handshake_length), wire.size() - 9u);
  }
}

TEST(TlsBuilderLengthAgreementSecurity, WithOptionsPathMustProduceValidWire) {
  // The detail::build_default_tls_client_hello_with_options path is used
  // for deterministic testing. It must also produce valid wire images.
  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  td::mtproto::stealth::detail::TlsHelloBuildOptions options;
  options.padding_extension_payload_length = 0;
  options.ech_payload_length = 176;
  options.pq_group_id = 0x11EC;
  options.ech_enc_key_length = 32;
  options.alps_extension_type = 0x44CD;

  for (td::uint64 seed = 1; seed <= 50; seed++) {
    MockRng rng(seed);
    auto wire = td::mtproto::stealth::detail::build_default_tls_client_hello_with_options(
        "www.google.com", "0123456789secret", 1712345678, non_ru, rng, options);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
  }
}

TEST(TlsBuilderLengthAgreementSecurity, LongDomainMustNotCorruptWire) {
  // Domain is truncated to ProxySecret::MAX_DOMAIN_LENGTH. Verify that
  // a very long domain doesn't cause buffer issues.
  td::string long_domain(300, 'a');
  long_domain += ".example.com";

  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile(long_domain, "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                                 EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsBuilderLengthAgreementSecurity, MinimalDomainMustNotCorruptWire) {
  // Single-character domain
  MockRng rng(42);
  auto wire = build_tls_client_hello_for_profile("a", "0123456789secret", 1712345678, BrowserProfile::Chrome133,
                                                 EchMode::Disabled, rng);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsBuilderLengthAgreementSecurity, EchEnabledWithAllPayloadLengthsMustProduceValidWire) {
  // ECH payload lengths are {144, 176, 208, 240}. Verify each directly.
  NetworkRouteHints non_ru;
  non_ru.is_known = true;
  non_ru.is_ru = false;

  for (int ech_len : {144, 176, 208, 240}) {
    td::mtproto::stealth::detail::TlsHelloBuildOptions options;
    options.ech_payload_length = ech_len;
    options.padding_extension_payload_length = 0;
    options.pq_group_id = 0x11EC;
    options.ech_enc_key_length = 32;
    options.alps_extension_type = 0x44CD;

    MockRng rng(42);
    auto wire = td::mtproto::stealth::detail::build_default_tls_client_hello_with_options(
        "www.google.com", "0123456789secret", 1712345678, non_ru, rng, options);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    auto hello = parsed.move_as_ok();
    ASSERT_EQ(static_cast<td::uint16>(ech_len), hello.ech_payload_length);
  }
}

}  // namespace
