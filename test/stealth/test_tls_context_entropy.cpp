// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TestHelpers.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::detail::build_default_tls_client_hello_with_options;
using td::mtproto::stealth::detail::kCorrectEchEncKeyLen;
using td::mtproto::stealth::detail::kCurrentSingleLanePqGroupId;
using td::mtproto::stealth::detail::TlsHelloBuildOptions;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;
using td::mtproto::test::sample_grease_value;

constexpr td::uint16 kPaddingExtensionType = 0x0015;

td::string build_with_options(td::uint64 seed, const NetworkRouteHints &route_hints,
                              const TlsHelloBuildOptions &options) {
  MockRng rng(seed);
  return build_default_tls_client_hello_with_options("www.google.com", "0123456789secret", 1712345678, route_hints, rng,
                                                     options);
}

TEST(TlsContextEntropy, ExplicitSerializerParametersDriveWireImage) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  TlsHelloBuildOptions options;
  options.ech_payload_length = 176;
  options.pq_group_id = td::mtproto::test::fixtures::kPqHybridDraftGroup;
  options.ech_enc_key_length = kCorrectEchEncKeyLen;

  auto wire = build_with_options(42, hints, options);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  std::unordered_set<td::uint16> supported_groups(parsed.ok().supported_groups.begin(),
                                                  parsed.ok().supported_groups.end());
  std::unordered_set<td::uint16> key_share_groups(parsed.ok().key_share_groups.begin(),
                                                  parsed.ok().key_share_groups.end());

  ASSERT_TRUE(supported_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) != 0);
  ASSERT_TRUE(key_share_groups.count(td::mtproto::test::fixtures::kPqHybridDraftGroup) != 0);
  ASSERT_TRUE(supported_groups.count(kCurrentSingleLanePqGroupId) == 0);
  ASSERT_TRUE(key_share_groups.count(kCurrentSingleLanePqGroupId) == 0);
  ASSERT_EQ(176u, parsed.ok().ech_payload_length);
  ASSERT_EQ(kCorrectEchEncKeyLen, parsed.ok().ech_declared_enc_length);
  ASSERT_EQ(kCorrectEchEncKeyLen, parsed.ok().ech_actual_enc_length);
}

TEST(TlsContextEntropy, ExplicitPaddingExtensionLengthIsHonored) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  TlsHelloBuildOptions options;
  options.padding_extension_payload_length = 23;

  auto wire = build_with_options(7, hints, options);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  auto *padding_extension = find_extension(parsed.ok(), kPaddingExtensionType);
  ASSERT_TRUE(padding_extension != nullptr);
  ASSERT_EQ(23u, padding_extension->value.size());
}

TEST(TlsContextEntropy, NonStandardEchEncLengthStillMatchesWrittenBytes) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  TlsHelloBuildOptions options;
  options.ech_payload_length = 208;
  options.ech_enc_key_length = 31;

  auto wire = build_with_options(11, hints, options);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());
  ASSERT_EQ(31u, parsed.ok().ech_declared_enc_length);
  ASSERT_EQ(31u, parsed.ok().ech_actual_enc_length);
  ASSERT_EQ(208u, parsed.ok().ech_payload_length);
}

TEST(TlsContextEntropy, GreaseSampleHelperProducesValidRfc8701Values) {
  MockRng rng(99);
  for (int i = 0; i < 1000; i++) {
    auto grease = sample_grease_value(rng);
    auto lo = static_cast<td::uint8>(grease & 0xFF);
    auto hi = static_cast<td::uint8>((grease >> 8) & 0xFF);
    ASSERT_EQ(lo, hi);
    ASSERT_EQ(0x0A, lo & 0x0F);
  }
}

}  // namespace