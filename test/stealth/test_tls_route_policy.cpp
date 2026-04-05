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

#include "td/utils/tests.h"

#include <unordered_set>

namespace {

using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::parse_tls_client_hello;

bool has_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
}

bool has_legacy_ech_extension(const td::mtproto::test::ParsedClientHello &hello) {
  return find_extension(hello, 0xFE02) != nullptr;
}

TEST(TlsRoutePolicy, UnknownRouteDisablesEchByDefault) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678, hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_FALSE(has_ech_extension(parsed.ok()));
}

TEST(TlsRoutePolicy, RuRouteDisablesEch) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;

  auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678, hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_FALSE(has_ech_extension(parsed.ok()));
}

TEST(TlsRoutePolicy, KnownNonRuRouteKeepsEchEnabled) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678, hints);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_TRUE(has_ech_extension(parsed.ok()));
}

TEST(TlsRoutePolicy, DefaultOverloadMustFailClosedAndKeepEchDisabled) {
  auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678);
  auto parsed = parse_tls_client_hello(wire);
  ASSERT_TRUE(parsed.is_ok());

  ASSERT_FALSE(has_ech_extension(parsed.ok()));
}

TEST(TlsRoutePolicy, DefaultOverloadNeverEnablesEchAcrossManyConnections) {
  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
  }
}

TEST(TlsRoutePolicy, UnknownRouteNeverEnablesEchAcrossManyConnections) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
  }
}

TEST(TlsRoutePolicy, KnownNonRuRouteAlwaysCarriesEchAcrossManyConnections) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(has_ech_extension(parsed.ok()));
  }
}

TEST(TlsRoutePolicy, UnknownRouteMustNeverEmitAnyEchTypeAcrossManyConnections) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
    ASSERT_FALSE(has_legacy_ech_extension(parsed.ok()));
  }
}

TEST(TlsRoutePolicy, RuRouteMustNeverEmitAnyEchTypeAcrossManyConnections) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;

  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
    ASSERT_FALSE(has_legacy_ech_extension(parsed.ok()));
  }
}

TEST(TlsRoutePolicy, UnknownRouteClientHelloLengthMustNotCollapseToSingleFingerprint) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  std::unordered_set<size_t> observed_lengths;
  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
    observed_lengths.insert(wire.size());
  }

  ASSERT_TRUE(observed_lengths.size() > 1u);
}

TEST(TlsRoutePolicy, RuRouteClientHelloLengthMustNotCollapseToSingleFingerprint) {
  td::Slice secret("0123456789secret");
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;

  std::unordered_set<size_t> observed_lengths;
  for (int i = 0; i < 256; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", secret, 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_FALSE(has_ech_extension(parsed.ok()));
    observed_lengths.insert(wire.size());
  }

  ASSERT_TRUE(observed_lengths.size() > 1u);
}

}  // namespace
