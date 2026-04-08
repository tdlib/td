// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::uint16 kStatusRequest = 0x0005;
constexpr td::uint16 kSupportedGroups = 0x000A;
constexpr td::uint16 kEcPointFormats = 0x000B;
constexpr td::uint16 kSignatureAlgorithms = 0x000D;
constexpr td::uint16 kAlpn = 0x0010;
constexpr td::uint16 kSct = 0x0012;
constexpr td::uint16 kPadding = 0x0015;
constexpr td::uint16 kExtendedMasterSecret = 0x0017;
constexpr td::uint16 kCompressCertificate = 0x001B;
constexpr td::uint16 kSessionTicket = 0x0023;
constexpr td::uint16 kSupportedVersions = 0x002B;
constexpr td::uint16 kPskModes = 0x002D;
constexpr td::uint16 kKeyShare = 0x0033;
constexpr td::uint16 kAlps = 0x44CD;
constexpr td::uint16 kEch = 0xFE0D;
constexpr td::uint16 kRenegotiationInfo = 0xFF01;

bool is_grease_extension(td::uint16 type) {
  auto hi = static_cast<td::uint8>((type >> 8) & 0xFF);
  auto lo = static_cast<td::uint8>(type & 0xFF);
  return hi == lo && (hi & 0x0F) == 0x0A;
}

std::vector<td::uint16> normalize_extensions(const td::mtproto::test::ParsedClientHello &hello) {
  std::vector<td::uint16> result;
  result.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    if (is_grease_extension(ext.type) || ext.type == kPadding) {
      continue;
    }
    result.push_back(ext.type);
  }
  return result;
}

std::vector<td::uint16> raw_extension_types(const td::mtproto::test::ParsedClientHello &hello) {
  std::vector<td::uint16> result;
  result.reserve(hello.extensions.size());
  for (const auto &ext : hello.extensions) {
    result.push_back(ext.type);
  }
  return result;
}

td::string order_signature(const std::vector<td::uint16> &ext) {
  td::string signature;
  for (auto type : ext) {
    signature += td::to_string(type);
    signature += ",";
  }
  return signature;
}

std::multiset<td::uint16> to_multiset(const std::vector<td::uint16> &ext) {
  return std::multiset<td::uint16>(ext.begin(), ext.end());
}

void assert_chrome_anchor_layout(const td::mtproto::test::ParsedClientHello &hello, bool padding_allowed) {
  auto raw = raw_extension_types(hello);
  ASSERT_TRUE(raw.size() >= 3u);
  ASSERT_TRUE(is_grease_extension(raw.front()));

  if (raw.back() == kPadding) {
    ASSERT_TRUE(padding_allowed);
    ASSERT_TRUE(is_grease_extension(raw[raw.size() - 2]));
  } else {
    ASSERT_TRUE(is_grease_extension(raw.back()));
  }
}

TEST(TlsExtensionOrderPolicy, EchEnabledExtensionsMatchChromeShuffleModel) {
  const std::multiset<td::uint16> expected = {0x0000,
                                              kStatusRequest,
                                              kSupportedGroups,
                                              kEcPointFormats,
                                              kSignatureAlgorithms,
                                              kAlpn,
                                              kSct,
                                              kExtendedMasterSecret,
                                              kCompressCertificate,
                                              kSessionTicket,
                                              kSupportedVersions,
                                              kPskModes,
                                              kKeyShare,
                                              kAlps,
                                              kEch,
                                              kRenegotiationInfo};

  std::unordered_set<td::string> observed_orders;
  std::unordered_set<size_t> renegotiation_positions;

  td::Slice secret("0123456789secret");

  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    assert_chrome_anchor_layout(parsed.ok(), false);

    auto ext = normalize_extensions(parsed.ok());
    ASSERT_EQ(16u, ext.size());
    ASSERT_TRUE(expected == to_multiset(ext));

    observed_orders.insert(order_signature(ext));
    auto it = std::find(ext.begin(), ext.end(), kRenegotiationInfo);
    ASSERT_TRUE(it != ext.end());
    renegotiation_positions.insert(static_cast<size_t>(it - ext.begin()));
  }

  ASSERT_TRUE(observed_orders.size() > 1u);
  ASSERT_TRUE(renegotiation_positions.size() > 2u);
}

TEST(TlsExtensionOrderPolicy, EchDisabledExtensionsMatchChromeShuffleModel) {
  const std::multiset<td::uint16> expected = {0x0000,
                                              kStatusRequest,
                                              kSupportedGroups,
                                              kEcPointFormats,
                                              kSignatureAlgorithms,
                                              kAlpn,
                                              kSct,
                                              kExtendedMasterSecret,
                                              kCompressCertificate,
                                              kSessionTicket,
                                              kSupportedVersions,
                                              kPskModes,
                                              kKeyShare,
                                              kAlps,
                                              kRenegotiationInfo};

  std::unordered_set<td::string> observed_orders;
  std::unordered_set<size_t> renegotiation_positions;

  td::Slice secret("0123456789secret");

  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    assert_chrome_anchor_layout(parsed.ok(), true);

    auto ext = normalize_extensions(parsed.ok());
    ASSERT_EQ(15u, ext.size());
    ASSERT_TRUE(expected == to_multiset(ext));

    observed_orders.insert(order_signature(ext));
    auto it = std::find(ext.begin(), ext.end(), kRenegotiationInfo);
    ASSERT_TRUE(it != ext.end());
    renegotiation_positions.insert(static_cast<size_t>(it - ext.begin()));
  }

  ASSERT_TRUE(observed_orders.size() > 1u);
  ASSERT_TRUE(renegotiation_positions.size() > 2u);
}

TEST(TlsExtensionOrderPolicy, ChromeShuffleEntropyMustRemainNonDeterministic) {
  std::unordered_set<td::string> observed_enabled_orders;
  std::unordered_set<td::string> observed_disabled_orders;
  td::Slice secret("0123456789secret");

  {
    NetworkRouteHints hints;
    hints.is_known = true;
    hints.is_ru = false;

    for (int i = 0; i < 256; i++) {
      auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      auto ext = normalize_extensions(parsed.ok());
      ASSERT_EQ(16u, ext.size());
      observed_enabled_orders.insert(order_signature(ext));
    }
  }

  {
    NetworkRouteHints hints;
    hints.is_known = false;
    hints.is_ru = false;

    for (int i = 0; i < 256; i++) {
      auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      auto ext = normalize_extensions(parsed.ok());
      ASSERT_EQ(15u, ext.size());
      observed_disabled_orders.insert(order_signature(ext));
    }
  }

  ASSERT_TRUE(observed_enabled_orders.size() > 1u);
  ASSERT_TRUE(observed_disabled_orders.size() > 1u);
}

}  // namespace
