// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::int32 kUnixTime = 1712345678;

td::string build_with_seed(td::uint64 seed, const NetworkRouteHints &route_hints) {
  MockRng rng(seed);
  return build_default_tls_client_hello("www.google.com", "0123456789secret", kUnixTime, route_hints, rng);
}

TEST(TlsHelloBuilderSeam, SameSeedMustProduceStableWireImage) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  auto first = build_with_seed(0x12345678ULL, hints);
  auto second = build_with_seed(0x12345678ULL, hints);

  ASSERT_EQ(first, second);

  auto parsed = parse_tls_client_hello(first);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsHelloBuilderSeam, DifferentSeedsMustChangeWireWithoutBreakingParserInvariants) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  auto first = build_with_seed(7, hints);
  auto second = build_with_seed(8, hints);

  ASSERT_NE(first, second);

  auto first_parsed = parse_tls_client_hello(first);
  auto second_parsed = parse_tls_client_hello(second);
  ASSERT_TRUE(first_parsed.is_ok());
  ASSERT_TRUE(second_parsed.is_ok());
}

TEST(TlsHelloBuilderSeam, SequentialBuildsOnSameInjectedRngMustNotCollapseToStaticOutput) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  MockRng rng(99);
  auto first = build_default_tls_client_hello("www.google.com", "0123456789secret", kUnixTime, hints, rng);
  auto second = build_default_tls_client_hello("www.google.com", "0123456789secret", kUnixTime, hints, rng);

  ASSERT_NE(first, second);

  auto first_parsed = parse_tls_client_hello(first);
  auto second_parsed = parse_tls_client_hello(second);
  ASSERT_TRUE(first_parsed.is_ok());
  ASSERT_TRUE(second_parsed.is_ok());
}

}  // namespace