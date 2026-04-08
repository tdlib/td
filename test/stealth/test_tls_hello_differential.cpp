// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TestHelpers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::compute_ja3;
using td::mtproto::test::extract_cipher_suites;

td::string build_ech_enabled_client_hello(td::int32 unix_time) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return build_default_tls_client_hello("www.google.com", "0123456789secret", unix_time, hints);
}

TEST(TlsHelloDifferential, Ja3MustNotMatchKnownTelegramFingerprint) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  ASSERT_NE(td::string(td::mtproto::test::fixtures::kKnownTelegramJa3), compute_ja3(wire));
}

TEST(TlsHelloDifferential, RandomizedChromeLaneJa3MustNotCollapseAcrossConnections) {
  std::unordered_set<td::string> ja3_hashes;

  for (int i = 0; i < 64; i++) {
    auto wire = build_ech_enabled_client_hello(1712345678 + i);
    ja3_hashes.insert(compute_ja3(wire));
  }

  ASSERT_TRUE(ja3_hashes.size() > 1u);
}

TEST(TlsHelloDifferential, ModernLaneMustNotOfferDeprecated3DesSuites) {
  auto wire = build_ech_enabled_client_hello(1712345678);
  auto cipher_suites = extract_cipher_suites(wire);
  std::unordered_set<td::uint16> suite_set(cipher_suites.begin(), cipher_suites.end());

  ASSERT_TRUE(suite_set.count(td::mtproto::test::fixtures::kTlsRsaWith3DesEdeCbcSha) == 0);
  ASSERT_TRUE(suite_set.count(td::mtproto::test::fixtures::kTlsEcdheEcdsaWith3DesEdeCbcSha) == 0);
  ASSERT_TRUE(suite_set.count(td::mtproto::test::fixtures::kTlsEcdheRsaWith3DesEdeCbcSha) == 0);
}

}  // namespace