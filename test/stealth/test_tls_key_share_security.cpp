//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/FingerprintFixtures.h"
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
using td::mtproto::test::corrupt_key_share_coordinate;
using td::mtproto::test::is_valid_curve25519_public_coordinate;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
bool set_key_share_entry_length(td::string &wire, td::uint16 group, td::uint16 key_length) {
  return td::mtproto::test::set_key_share_entry_length(wire, group, key_length);
}

td::string build_with_seed(td::uint64 seed) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  MockRng rng(seed);
  return build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678, hints, rng);
}

TEST(TlsKeyShareSecurity, RejectsWrongX25519KeyShareLength) {
  auto wire = build_with_seed(1);
  ASSERT_TRUE(set_key_share_entry_length(wire, td::mtproto::test::fixtures::kX25519Group,
                                         td::mtproto::test::fixtures::kX25519KeyShareLength - 1));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(TlsKeyShareSecurity, RejectsWrongPqHybridKeyShareLength) {
  auto wire = build_with_seed(1);
  ASSERT_TRUE(set_key_share_entry_length(wire, td::mtproto::test::fixtures::kPqHybridGroup,
                                         td::mtproto::test::fixtures::kPqHybridKeyShareLength - 1));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(TlsKeyShareSecurity, X25519KeyShareMustBeValidCurve25519Coordinate) {
  for (td::uint64 seed = 1; seed <= 32; seed++) {
    auto wire = build_with_seed(seed);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    bool found = false;
    for (const auto &entry : parsed.ok().key_share_entries) {
      if (entry.group != td::mtproto::test::fixtures::kX25519Group) {
        continue;
      }
      found = true;
      ASSERT_EQ(td::mtproto::test::fixtures::kX25519KeyShareLength, entry.key_length);
      ASSERT_TRUE(is_valid_curve25519_public_coordinate(entry.key_data));
    }
    ASSERT_TRUE(found);
  }
}

TEST(TlsKeyShareSecurity, PqHybridKeyShareTailMustBeValidCurve25519Coordinate) {
  for (td::uint64 seed = 1; seed <= 32; seed++) {
    auto wire = build_with_seed(seed);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    bool found = false;
    for (const auto &entry : parsed.ok().key_share_entries) {
      if (entry.group != td::mtproto::test::fixtures::kPqHybridGroup) {
        continue;
      }
      found = true;
      ASSERT_EQ(td::mtproto::test::fixtures::kPqHybridKeyShareLength, entry.key_length);
      ASSERT_TRUE(entry.key_data.size() >= td::mtproto::test::fixtures::kX25519KeyShareLength);
      auto x25519_tail =
          entry.key_data.substr(entry.key_data.size() - td::mtproto::test::fixtures::kX25519KeyShareLength,
                                td::mtproto::test::fixtures::kX25519KeyShareLength);
      ASSERT_TRUE(is_valid_curve25519_public_coordinate(x25519_tail));
    }
    ASSERT_TRUE(found);
  }
}

TEST(TlsKeyShareSecurity, RejectsInvalidX25519Coordinate) {
  auto wire = build_with_seed(1);
  ASSERT_TRUE(corrupt_key_share_coordinate(wire, td::mtproto::test::fixtures::kX25519Group,
                                           /*mutate_tail_only=*/false));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

TEST(TlsKeyShareSecurity, RejectsInvalidPqHybridTailCoordinate) {
  auto wire = build_with_seed(1);
  ASSERT_TRUE(corrupt_key_share_coordinate(wire, td::mtproto::test::fixtures::kPqHybridGroup,
                                           /*mutate_tail_only=*/true));
  ASSERT_TRUE(parse_tls_client_hello(wire).is_error());
}

}  // namespace