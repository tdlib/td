//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsHelloProfiles, ALPSCodepointMustMatchKnownProfilePolicy) {
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  std::unordered_set<td::uint16> expected = {td::mtproto::test::fixtures::kAlpsChrome131,
                                             td::mtproto::test::fixtures::kAlpsChrome133Plus};
  td::uint16 matched = 0;
  for (auto ext_type : expected) {
    if (find_extension(parsed.ok(), ext_type) != nullptr) {
      matched = ext_type;
      break;
    }
  }

  ASSERT_NE(matched, 0);
}

TEST(TlsHelloProfiles, EchTypeAndDeclaredEncLengthInvariant) {
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  const auto *ech = find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType);
  ASSERT_TRUE(ech != nullptr);
  ASSERT_EQ(32, parsed.ok().ech_declared_enc_length);
  ASSERT_EQ(parsed.ok().ech_declared_enc_length, parsed.ok().ech_actual_enc_length);
}

TEST(TlsHelloProfiles, MustSupportHybridAndClassicalKeyShareGroups) {
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  std::unordered_set<td::uint16> groups(parsed.ok().key_share_groups.begin(), parsed.ok().key_share_groups.end());
  ASSERT_TRUE(groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(groups.count(td::mtproto::test::fixtures::kX25519Group) != 0);
}

}  // namespace
