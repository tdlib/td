// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

td::string build_with_seed(const NetworkRouteHints &route_hints, td::uint64 seed) {
  MockRng rng(seed);
  return build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, route_hints, rng);
}

TEST(TlsRouteFailClosedEquivalence, UnknownAndRuRoutesProduceIdenticalWireForSameSeed) {
  NetworkRouteHints unknown_route;
  unknown_route.is_known = false;
  unknown_route.is_ru = false;

  NetworkRouteHints ru_route;
  ru_route.is_known = true;
  ru_route.is_ru = true;

  for (td::uint64 seed = 1; seed <= 128; seed++) {
    auto unknown_wire = build_with_seed(unknown_route, seed);
    auto ru_wire = build_with_seed(ru_route, seed);

    ASSERT_EQ(unknown_wire, ru_wire);

    auto parsed = parse_tls_client_hello(unknown_wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType) == nullptr);
    ASSERT_TRUE(find_extension(parsed.ok(), 0xFE02) == nullptr);
  }
}

TEST(TlsRouteFailClosedEquivalence, UnknownAndRuRoutesKeepSameNormalizedExtensionSetAcrossSeeds) {
  NetworkRouteHints unknown_route;
  unknown_route.is_known = false;
  unknown_route.is_ru = false;

  NetworkRouteHints ru_route;
  ru_route.is_known = true;
  ru_route.is_ru = true;

  for (td::uint64 seed = 1; seed <= 128; seed++) {
    auto unknown_wire = build_with_seed(unknown_route, seed);
    auto ru_wire = build_with_seed(ru_route, seed);

    auto unknown = parse_tls_client_hello(unknown_wire);
    auto ru = parse_tls_client_hello(ru_wire);
    ASSERT_TRUE(unknown.is_ok());
    ASSERT_TRUE(ru.is_ok());

    ASSERT_EQ(unknown.ok().extensions.size(), ru.ok().extensions.size());
    for (size_t i = 0; i < unknown.ok().extensions.size(); i++) {
      ASSERT_EQ(unknown.ok().extensions[i].type, ru.ok().extensions[i].type);
      ASSERT_EQ(unknown.ok().extensions[i].value.size(), ru.ok().extensions[i].value.size());
    }
  }
}

}  // namespace