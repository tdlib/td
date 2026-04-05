//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_map>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

constexpr int kStressSampleCount = 2048;

bool has_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

bool has_legacy_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, 0xFE02) != nullptr;
}

TEST(TlsRoutePolicyStress, UnknownRouteLengthDistributionMustNotCollapse) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  std::unordered_map<size_t, int> length_counts;
  for (int i = 0; i < kStressSampleCount; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
    ASSERT_FALSE(has_legacy_ech_extension(parsed.ok()));
    length_counts[wire.size()]++;
  }

  ASSERT_TRUE(length_counts.size() >= 6u);
  int max_bucket = 0;
  for (const auto &it : length_counts) {
    max_bucket = std::max(max_bucket, it.second);
  }
  ASSERT_TRUE(max_bucket * 100 < kStressSampleCount * 80);
}

TEST(TlsRoutePolicyStress, RuRouteLengthDistributionMustNotCollapse) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;

  std::unordered_map<size_t, int> length_counts;
  for (int i = 0; i < kStressSampleCount; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
    ASSERT_FALSE(has_legacy_ech_extension(parsed.ok()));
    length_counts[wire.size()]++;
  }

  ASSERT_TRUE(length_counts.size() >= 6u);
  int max_bucket = 0;
  for (const auto &it : length_counts) {
    max_bucket = std::max(max_bucket, it.second);
  }
  ASSERT_TRUE(max_bucket * 100 < kStressSampleCount * 80);
}

TEST(TlsRoutePolicyStress, KnownNonRuEchPayloadDistributionMustRemainMultiBucket) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  std::unordered_map<td::uint16, int> payload_counts;
  for (int i = 0; i < kStressSampleCount; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(has_ech_extension(parsed.ok()));

    auto ech_payload_len = parsed.ok().ech_payload_length;
    ASSERT_TRUE(ech_payload_len == 144 || ech_payload_len == 176 || ech_payload_len == 208 || ech_payload_len == 240);
    payload_counts[ech_payload_len]++;
  }

  ASSERT_TRUE(payload_counts.size() >= 3u);
  int max_bucket = 0;
  for (const auto &it : payload_counts) {
    max_bucket = std::max(max_bucket, it.second);
  }
  ASSERT_TRUE(max_bucket * 100 < kStressSampleCount * 80);
}

}  // namespace
