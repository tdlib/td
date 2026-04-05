//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <array>
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

bool is_in_window(td::uint16 type, const std::unordered_set<td::uint16> &window) {
  return window.count(type) != 0;
}

td::string window_signature(const std::vector<td::uint16> &ext, size_t begin, size_t end) {
  td::string signature;
  for (size_t idx = begin; idx < end; idx++) {
    signature += td::to_string(ext[idx]);
    signature += ",";
  }
  return signature;
}

TEST(TlsExtensionOrderPolicy, EchEnabledOrderMustStayWithinProfileWindows) {
  const std::unordered_set<td::uint16> window1 = {0x0000, kStatusRequest, kSupportedGroups, kEcPointFormats};
  const std::unordered_set<td::uint16> window2 = {kSignatureAlgorithms, kAlpn, kSct, kExtendedMasterSecret};
  const std::unordered_set<td::uint16> window3 = {kCompressCertificate, kSessionTicket, kSupportedVersions, kPskModes};
  const std::unordered_set<td::uint16> window4 = {kKeyShare, kAlps, kEch};

  std::unordered_set<td::string> observed_tail_orders;
  td::Slice secret("0123456789secret");

  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  for (int i = 0; i < 128; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto ext = normalize_extensions(parsed.ok());
    ASSERT_EQ(16u, ext.size());

    for (size_t idx = 0; idx < 4; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window1));
    }
    for (size_t idx = 4; idx < 8; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window2));
    }
    for (size_t idx = 8; idx < 12; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window3));
    }
    for (size_t idx = 12; idx < 15; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window4));
    }

    ASSERT_EQ(kRenegotiationInfo, ext[15]);

    td::string tail_order;
    for (size_t idx = 12; idx < 15; idx++) {
      tail_order += td::to_string(ext[idx]) + ",";
    }
    observed_tail_orders.insert(std::move(tail_order));
  }

  ASSERT_TRUE(observed_tail_orders.size() > 1u);
}

TEST(TlsExtensionOrderPolicy, EchDisabledOrderMustStayWithinProfileWindows) {
  const std::unordered_set<td::uint16> window1 = {0x0000, kStatusRequest, kSupportedGroups, kEcPointFormats};
  const std::unordered_set<td::uint16> window2 = {kSignatureAlgorithms, kAlpn, kSct, kExtendedMasterSecret};
  const std::unordered_set<td::uint16> window3 = {kCompressCertificate, kSessionTicket, kSupportedVersions, kPskModes};
  const std::unordered_set<td::uint16> window4 = {kKeyShare, kAlps};

  td::Slice secret("0123456789secret");

  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  for (int i = 0; i < 128; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());

    auto ext = normalize_extensions(parsed.ok());
    ASSERT_EQ(15u, ext.size());

    for (size_t idx = 0; idx < 4; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window1));
    }
    for (size_t idx = 4; idx < 8; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window2));
    }
    for (size_t idx = 8; idx < 12; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window3));
    }
    for (size_t idx = 12; idx < 14; idx++) {
      ASSERT_TRUE(is_in_window(ext[idx], window4));
    }

    ASSERT_EQ(kRenegotiationInfo, ext[14]);
  }
}

TEST(TlsExtensionOrderPolicy, WindowPermutationEntropyMustRemainNonDeterministic) {
  td::Slice secret("0123456789secret");

  {
    std::array<std::unordered_set<td::string>, 4> observed_windows;
    NetworkRouteHints hints;
    hints.is_known = true;
    hints.is_ru = false;

    for (int i = 0; i < 256; i++) {
      auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      auto ext = normalize_extensions(parsed.ok());
      ASSERT_EQ(16u, ext.size());
      observed_windows[0].insert(window_signature(ext, 0, 4));
      observed_windows[1].insert(window_signature(ext, 4, 8));
      observed_windows[2].insert(window_signature(ext, 8, 12));
      observed_windows[3].insert(window_signature(ext, 12, 15));
    }

    for (const auto &window_orders : observed_windows) {
      ASSERT_TRUE(window_orders.size() > 1u);
    }
  }

  {
    std::array<std::unordered_set<td::string>, 4> observed_windows;
    NetworkRouteHints hints;
    hints.is_known = false;
    hints.is_ru = false;

    for (int i = 0; i < 256; i++) {
      auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());

      auto ext = normalize_extensions(parsed.ok());
      ASSERT_EQ(15u, ext.size());
      observed_windows[0].insert(window_signature(ext, 0, 4));
      observed_windows[1].insert(window_signature(ext, 4, 8));
      observed_windows[2].insert(window_signature(ext, 8, 12));
      observed_windows[3].insert(window_signature(ext, 12, 14));
    }

    for (const auto &window_orders : observed_windows) {
      ASSERT_TRUE(window_orders.size() > 1u);
    }
  }
}

}  // namespace
