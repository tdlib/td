// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::detail::build_default_tls_client_hello_with_options;
using td::mtproto::stealth::detail::kCorrectEchEncKeyLen;
using td::mtproto::stealth::detail::TlsHelloBuildOptions;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

td::string build_with_options(const NetworkRouteHints &route_hints, td::uint64 seed) {
  MockRng rng(seed);
  TlsHelloBuildOptions options;
  options.padding_extension_payload_length = 19;
  options.ech_payload_length = 240;
  options.ech_enc_key_length = kCorrectEchEncKeyLen;
  return build_default_tls_client_hello_with_options("www.google.com", "0123456789secret", 1712345678, route_hints, rng,
                                                     options);
}

TEST(TlsBuilderRouteSeam, ExplicitOptionsCannotForceEchOnUnknownRoute) {
  NetworkRouteHints route_hints;
  route_hints.is_known = false;
  route_hints.is_ru = false;

  auto wire = build_with_options(route_hints, 17);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
}

TEST(TlsBuilderRouteSeam, ExplicitOptionsCannotForceEchOnRuRoute) {
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = true;

  auto wire = build_with_options(route_hints, 18);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
  ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
}

TEST(TlsBuilderRouteSeam, ExplicitOptionsStillDriveLengthsOnKnownNonRuRoute) {
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  auto wire = build_with_options(route_hints, 19);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  auto *ech = find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType);
  ASSERT_TRUE(ech != nullptr);
  ASSERT_EQ(240u, parsed.ok().ech_payload_length);
  ASSERT_EQ(kCorrectEchEncKeyLen, parsed.ok().ech_declared_enc_length);
  ASSERT_EQ(kCorrectEchEncKeyLen, parsed.ok().ech_actual_enc_length);
}

}  // namespace