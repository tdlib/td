// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/FingerprintFixtures.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/TlsHelloParsers.h"

#include "td/mtproto/stealth/TlsHelloBuilder.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#if !TD_DARWIN

namespace {

using td::mtproto::stealth::build_runtime_tls_client_hello;
using td::mtproto::stealth::build_tls_client_hello_for_profile;
using td::mtproto::stealth::default_runtime_platform_hints;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::pick_runtime_profile;
using td::mtproto::test::find_extension;
using td::mtproto::test::MockRng;
using td::mtproto::test::parse_tls_client_hello;

TEST(TlsRuntimeBuilderEquivalence, RuntimeBuilderMatchesExplicitProfileAndDecisionPath) {
  auto platform = default_runtime_platform_hints();
  NetworkRouteHints route_hints;
  route_hints.is_known = true;
  route_hints.is_ru = false;

  for (td::uint32 bucket = 20000; bucket < 20032; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 7200);
    td::string domain = "runtime-equivalence-" + td::to_string(bucket) + ".example.com";

    auto profile = pick_runtime_profile(domain, unix_time, platform);
    auto decision = get_runtime_ech_decision(domain, unix_time, route_hints);

    MockRng runtime_rng(bucket + 11);
    MockRng explicit_rng(bucket + 11);
    auto runtime_wire = build_runtime_tls_client_hello(domain, "0123456789secret", unix_time, route_hints, runtime_rng);
    auto explicit_wire = build_tls_client_hello_for_profile(domain, "0123456789secret", unix_time, profile,
                                                            decision.ech_mode, explicit_rng);

    ASSERT_EQ(runtime_wire, explicit_wire);
  }
}

TEST(TlsRuntimeBuilderEquivalence, RuntimeBuilderSuppressesEchOnFailClosedRoutesWithoutChangingProfileFamily) {
  auto platform = default_runtime_platform_hints();
  NetworkRouteHints enabled_route;
  enabled_route.is_known = true;
  enabled_route.is_ru = false;

  NetworkRouteHints disabled_route;
  disabled_route.is_known = false;
  disabled_route.is_ru = false;

  for (td::uint32 bucket = 20000; bucket < 20032; bucket++) {
    auto unix_time = static_cast<td::int32>(bucket * 86400 + 5400);
    td::string domain = "runtime-ech-toggle-" + td::to_string(bucket) + ".example.com";
    auto profile = pick_runtime_profile(domain, unix_time, platform);

    MockRng enabled_rng(bucket + 101);
    MockRng disabled_rng(bucket + 101);
    auto enabled_wire =
        build_runtime_tls_client_hello(domain, "0123456789secret", unix_time, enabled_route, enabled_rng);
    auto disabled_wire =
        build_runtime_tls_client_hello(domain, "0123456789secret", unix_time, disabled_route, disabled_rng);

    auto enabled_hello = parse_tls_client_hello(enabled_wire);
    auto disabled_hello = parse_tls_client_hello(disabled_wire);
    ASSERT_TRUE(enabled_hello.is_ok());
    ASSERT_TRUE(disabled_hello.is_ok());

    auto enabled_ech = find_extension(enabled_hello.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
    auto disabled_ech = find_extension(disabled_hello.ok(), td::mtproto::test::fixtures::kEchExtensionType) != nullptr;
    ASSERT_TRUE(!disabled_ech);
    if (td::mtproto::stealth::profile_spec(profile).allows_ech) {
      ASSERT_TRUE(enabled_ech);
    } else {
      ASSERT_TRUE(!enabled_ech);
    }
  }
}

}  // namespace

#endif