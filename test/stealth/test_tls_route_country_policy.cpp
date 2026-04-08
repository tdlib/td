// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::route_hints_from_country_code;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

bool has_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

TEST(TlsRouteCountryPolicy, RuCountryCodeDisablesEch) {
  auto hints = route_hints_from_country_code("RU");
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_FALSE(has_ech_extension(parsed.ok()));
}

TEST(TlsRouteCountryPolicy, NonRuCountryCodeEnablesEch) {
  auto hints = route_hints_from_country_code("US");
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_TRUE(has_ech_extension(parsed.ok()));
}

TEST(TlsRouteCountryPolicy, InvalidCountryCodeKeepsEchDisabled) {
  auto hints = route_hints_from_country_code("R1");
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_FALSE(has_ech_extension(parsed.ok()));
}

}  // namespace
