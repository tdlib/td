//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"
#include "test/stealth/TlsHelloWireMutator.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::corrupt_ech_enc_coordinate;
using td::mtproto::test::is_valid_curve25519_public_coordinate;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

td::string build_with_seed(td::uint64 seed) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  MockRng rng(seed);
  return build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, hints, rng);
}

TEST(TlsEchEncKeySecurity, EchEncapsulatedKeyMustBeValidCurve25519Coordinate) {
  for (td::uint64 seed = 1; seed <= 32; seed++) {
    auto wire = build_with_seed(seed);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_EQ(32u, parsed.ok().ech_enc.size());
    ASSERT_TRUE(is_valid_curve25519_public_coordinate(parsed.ok().ech_enc));
  }
}

TEST(TlsEchEncKeySecurity, RejectsInvalidEchEncapsulatedKeyCoordinate) {
  auto wire = build_with_seed(1);
  ASSERT_TRUE(corrupt_ech_enc_coordinate(wire));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

}  // namespace