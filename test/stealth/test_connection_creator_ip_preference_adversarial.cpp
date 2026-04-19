// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

// ADVERSARIAL TESTS: Black-hat attacks targeting the IPv6/IPv4 DC selection policy.
// RC-B root cause: prefer_ipv6 for DC selection must stay independent of proxy IP family.
// Every test here is derived from a concrete failure or attack scenario.

#include "td/telegram/net/ConnectionCreator.h"

#include "td/utils/tests.h"

namespace {

td::IPAddress ipv4_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv4_port(ip, port).ensure();
  return result;
}

td::IPAddress ipv6_address(td::CSlice ip, td::int32 port) {
  td::IPAddress result;
  result.init_ipv6_port(ip, port).ensure();
  return result;
}

// ── Attack P1: IPv6 proxy must not force IPv6 DC when user_prefer_ipv6=false ─────
// Hypothesis: an IPv6-resolved proxy tricks the policy into preferring IPv6 DCs,
// causing "Network is unreachable" on IPv4-only paths.
TEST(ConnectionCreatorIpPreferenceAdversarial, Ipv6ProxyCannnotForceIpv6DcOnIpv4OnlyPath) {
  //
  // MTProto proxy with IPv6 address, user preference = false.
  // DC selection must NOT prefer IPv6 regardless of proxy family.
  //
  bool dc_prefers_ipv6 = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::mtproto("proxy.example.com", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")),
      /*user_prefer_ipv6=*/false, ipv6_address("2001:db8::1", 443));
  ASSERT_FALSE(dc_prefers_ipv6);
}

// ── Attack P2: SOCKS5 over IPv6 proxy with user=false still yields IPv4 DC ────
TEST(ConnectionCreatorIpPreferenceAdversarial, Socks5Ipv6ProxyWithUserFalseYieldsIpv4Dc) {
  bool dc_prefers_ipv6 =
      td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy::socks5("proxy.example.com", 1080, "u", "p"),
                                                               /*user_prefer_ipv6=*/false, ipv6_address("::1", 1080));
  ASSERT_FALSE(dc_prefers_ipv6);
}

// ── Attack P3: HTTP proxy over IPv6 with user=false still yields IPv4 DC ──────
TEST(ConnectionCreatorIpPreferenceAdversarial, HttpIpv6ProxyWithUserFalseYieldsIpv4Dc) {
  bool dc_prefers_ipv6 = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::http_tcp("proxy.example.com", 8080, "u", "p"),
      /*user_prefer_ipv6=*/false, ipv6_address("2001:db8::cafe", 8080));
  ASSERT_FALSE(dc_prefers_ipv6);
}

// ── Attack P4: Alternating proxy family across retries must not flip DC preference ─
// Hypothesis: if a retry causes the resolved proxy address to switch from IPv4→IPv6,
// the DC preference must remain stable (tied only to user setting).
TEST(ConnectionCreatorIpPreferenceAdversarial, AlternatingProxyFamilyDoesNotFlipDcPreference) {
  // Iteration 1: proxy resolved to IPv4
  bool pref_v4_proxy = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::socks5("proxy.example.com", 1080, "u", "p"),
      /*user_prefer_ipv6=*/false, ipv4_address("1.2.3.4", 1080));

  // Iteration 2: same proxy, re-resolved to IPv6 (retry scenario)
  bool pref_v6_proxy = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::socks5("proxy.example.com", 1080, "u", "p"),
      /*user_prefer_ipv6=*/false, ipv6_address("2001:db8::1", 1080));

  // Both must give the same result — the user preference (false) is the only input
  // that matters.
  ASSERT_EQ(pref_v4_proxy, pref_v6_proxy);
  ASSERT_FALSE(pref_v4_proxy);
}

// ── Attack P5: Invalid resolved proxy address must not produce unexpected IPv6 preference ─
// Hypothesis: an uninitialized/invalid IPAddress used as proxy address causes
// is_ipv6() to return true by accident, forcing IPv6 DC selection.
TEST(ConnectionCreatorIpPreferenceAdversarial, InvalidProxyAddressDoesNotForceIpv6Preference) {
  bool dc_prefers_ipv6 =
      td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy::socks5("proxy.example.com", 1080, "u", "p"),
                                                               /*user_prefer_ipv6=*/false,
                                                               td::IPAddress());  // default-constructed = invalid
  ASSERT_FALSE(dc_prefers_ipv6);
}

// ── Attack P6: User=true with direct (no proxy) must still be IPv6 ────────────
// Positive complement: when the user explicitly wants IPv6, it must be respected
// even over a direct connection.
TEST(ConnectionCreatorIpPreferenceAdversarial, DirectConnectionRespectsUserIpv6True) {
  bool dc_prefers_ipv6 =
      td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(),
                                                               /*user_prefer_ipv6=*/true, td::IPAddress());
  ASSERT_TRUE(dc_prefers_ipv6);
}

// ── Attack P7: Fuzz over proxy kinds with user=false — all should return false ─
// Hypothesis: some proxy kind enum value causes a path where the proxy IP family
// bleeds into the DC preference decision.
TEST(ConnectionCreatorIpPreferenceAdversarial, AllProxyKindsWithUserFalseMustReturnFalse) {
  const td::IPAddress ipv6 = ipv6_address("2001:db8::1", 443);
  const td::IPAddress ipv4 = ipv4_address("203.0.113.1", 443);

  // All known proxy types over an IPv6 resolved address with user=false.
  ASSERT_FALSE(
      td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy::socks5("h", 1080, "u", "p"), false, ipv6));
  ASSERT_FALSE(
      td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy::http_tcp("h", 8080, "u", "p"), false, ipv6));
  ASSERT_FALSE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::mtproto("h", 443, td::mtproto::ProxySecret::from_raw("0123456789abcdef")), false, ipv6));
  // Direct (no proxy) with IPv6-looking address (shouldn't matter anyway)
  ASSERT_FALSE(td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy(), false, ipv6));
  // IPv4 resolved address with user=false is trivially false
  ASSERT_FALSE(
      td::ConnectionCreator::should_prefer_ipv6_for_dc_options(td::Proxy::socks5("h", 1080, "u", "p"), false, ipv4));
}

// ── Attack P8: Both user=true and IPv6 proxy still returns true ────────────────
// The result must be determined solely by user_prefer_ipv6, so true+IPv6 = true.
TEST(ConnectionCreatorIpPreferenceAdversarial, UserTrueWithIpv6ProxyReturnsTrueNotDoubled) {
  bool dc_prefers_ipv6 = td::ConnectionCreator::should_prefer_ipv6_for_dc_options(
      td::Proxy::socks5("proxy.example.com", 1080, "u", "p"),
      /*user_prefer_ipv6=*/true, ipv6_address("2001:db8::1", 1080));
  // Still true — not "amplified" or otherwise modified by proxy family.
  ASSERT_TRUE(dc_prefers_ipv6);
}

}  // namespace
