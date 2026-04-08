// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_map>
#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

td::string build_ech_enabled_client_hello(td::int32 unix_time) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return build_default_tls_client_hello("www.google.com", "0123456789secret", unix_time, hints);
}

TEST(TlsHelloProfiles, ALPSCodepointMustMatchKnownProfilePolicy) {
  auto wire = build_ech_enabled_client_hello(1712345678);
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
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  const auto *ech = find_extension(parsed.ok(), td::mtproto::test::fixtures::kEchExtensionType);
  ASSERT_TRUE(ech != nullptr);
  ASSERT_EQ(32, parsed.ok().ech_declared_enc_length);
  ASSERT_EQ(parsed.ok().ech_declared_enc_length, parsed.ok().ech_actual_enc_length);
}

TEST(TlsHelloProfiles, MustSupportHybridAndClassicalKeyShareGroups) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  std::unordered_set<td::uint16> groups(parsed.ok().key_share_groups.begin(), parsed.ok().key_share_groups.end());
  ASSERT_TRUE(groups.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(groups.count(td::mtproto::test::fixtures::kX25519Group) != 0);
}

TEST(TlsHelloProfiles, KeyShareEntryLengthsMustMatchPolicy) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  std::unordered_map<td::uint16, td::uint16> key_lengths;
  for (const auto &entry : parsed.ok().key_share_entries) {
    key_lengths[entry.group] = entry.key_length;
  }

  ASSERT_TRUE(key_lengths.count(td::mtproto::test::fixtures::kPqHybridGroup) != 0);
  ASSERT_TRUE(key_lengths.count(td::mtproto::test::fixtures::kX25519Group) != 0);

  ASSERT_EQ(0x04C0, key_lengths[td::mtproto::test::fixtures::kPqHybridGroup]);
  ASSERT_EQ(32, key_lengths[td::mtproto::test::fixtures::kX25519Group]);
}

}  // namespace
