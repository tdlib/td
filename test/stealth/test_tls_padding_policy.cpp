// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/Interfaces.h"
#include "td/mtproto/stealth/TlsHelloBuilder.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::build_default_tls_client_hello;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

constexpr td::uint16 kPaddingExtensionType = 0x0015;

td::string build_with_seed(td::string domain, const NetworkRouteHints &route_hints, td::uint64 seed) {
  MockRng rng(seed);
  return build_default_tls_client_hello(std::move(domain), "0123456789secret", 1712345678, route_hints, rng);
}

TEST(TlsPaddingPolicy, KnownNonRuRouteNeverEmitsPaddingExtensionOnCurrentLane) {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;

  for (int i = 0; i < 128; i++) {
    auto wire = build_default_tls_client_hello("www.google.com", "0123456789secret", 1712345678 + i, hints);
    auto parsed = parse_tls_client_hello(wire);
    ASSERT_TRUE(parsed.is_ok());
    ASSERT_TRUE(find_extension(parsed.ok(), kPaddingExtensionType) == nullptr);
  }
}

TEST(TlsPaddingPolicy, ProfileDrivenPolicyMatchesRfc7685Window) {
  td::mtproto::stealth::PaddingPolicy policy;

  ASSERT_EQ(0u, policy.compute_padding_content_len(0xFF));
  ASSERT_EQ(252u, policy.compute_padding_content_len(0x100));
  ASSERT_EQ(208u, policy.compute_padding_content_len(0x12C));
  ASSERT_EQ(1u, policy.compute_padding_content_len(0x1FF));
  ASSERT_EQ(0u, policy.compute_padding_content_len(0x200));
}

TEST(TlsPaddingPolicy, DisabledPolicySuppressesPaddingAcrossWindow) {
  auto policy = td::mtproto::stealth::no_padding_policy();

  for (size_t len : {0x100u, 0x12Cu, 0x1FFu}) {
    ASSERT_EQ(0u, policy.compute_padding_content_len(len));
  }
}

TEST(TlsPaddingPolicy, ResolvedPaddingLengthUsesExactRfc7685Window) {
  td::mtproto::stealth::PaddingPolicy policy;

  ASSERT_EQ(252u, td::mtproto::stealth::resolve_padding_extension_payload_len(policy, 0x100u, 56u));
  ASSERT_EQ(208u, td::mtproto::stealth::resolve_padding_extension_payload_len(policy, 0x12Cu, 24u));
  ASSERT_EQ(1u, td::mtproto::stealth::resolve_padding_extension_payload_len(policy, 0x1FFu, 32u));
}

TEST(TlsPaddingPolicy, EntropyOnlyFallbackAppliesOnlyOutsideWindow) {
  td::mtproto::stealth::PaddingPolicy policy;

  ASSERT_EQ(17u, td::mtproto::stealth::resolve_padding_extension_payload_len(policy, 0x200u, 16u));
  ASSERT_EQ(25u, td::mtproto::stealth::resolve_padding_extension_payload_len(policy, 0x90u, 24u));
  ASSERT_EQ(0u, td::mtproto::stealth::resolve_padding_extension_payload_len(policy, 0x90u, 0u));
}

TEST(TlsPaddingPolicy, OverlongDomainMustTruncateBeforeWireShapingOnUnknownRoute) {
  NetworkRouteHints hints;
  hints.is_known = false;
  hints.is_ru = false;

  td::string truncated_domain(ProxySecret::MAX_DOMAIN_LENGTH, 'a');
  td::string overlong_domain = truncated_domain + td::string("overflow.example");

  auto truncated_wire = build_with_seed(truncated_domain, hints, 17);
  auto overlong_wire = build_with_seed(overlong_domain, hints, 17);

  ASSERT_EQ(truncated_wire, overlong_wire);

  auto parsed = parse_tls_client_hello(overlong_wire);
  ASSERT_TRUE(parsed.is_ok());
}

TEST(TlsPaddingPolicy, DomainLengthBoundaryRemainsParseableAcrossRoutes) {
  for (bool enable_ech : {false, true}) {
    NetworkRouteHints hints;
    hints.is_known = enable_ech;
    hints.is_ru = false;

    for (size_t len : {ProxySecret::MAX_DOMAIN_LENGTH - 1, ProxySecret::MAX_DOMAIN_LENGTH,
                       ProxySecret::MAX_DOMAIN_LENGTH + 1, ProxySecret::MAX_DOMAIN_LENGTH + 16}) {
      auto domain = td::string(len, 'b');
      auto wire = build_with_seed(domain, hints, static_cast<td::uint64>(len * 13 + enable_ech));
      auto parsed = parse_tls_client_hello(wire);
      ASSERT_TRUE(parsed.is_ok());
    }
  }
}

}  // namespace