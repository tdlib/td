// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::int32;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::DesktopOs;
using td::mtproto::stealth::DeviceClass;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::make_profile_selection_key;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimePlatformHints;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

RuntimePlatformHints make_linux_platform() {
  RuntimePlatformHints platform;
  platform.device_class = DeviceClass::Desktop;
  platform.desktop_os = DesktopOs::Linux;
  return platform;
}

NetworkRouteHints known_non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

td::string long_destination_with_suffix(char suffix) {
  td::string base(td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH + 40, 'a');
  base.push_back(suffix);
  return base;
}

TEST(TlsRuntimeDestinationKeySafety, SelectionKeyNormalizesDestinationToProxyDomainLimit) {
  const auto destination = long_destination_with_suffix('x');
  const int32 unix_time = 1712345678;

  const auto key = make_profile_selection_key(destination, unix_time);
  ASSERT_EQ(td::mtproto::ProxySecret::MAX_DOMAIN_LENGTH, key.destination.size());
}

TEST(TlsRuntimeDestinationKeySafety, FailureCacheUsesNormalizedDestinationKey) {
  RuntimeParamsGuard guard;

  auto params = default_runtime_stealth_params();
  params.platform_hints = make_linux_platform();
  params.route_failure.ech_failure_threshold = 1;
  params.route_failure.ech_disable_ttl_seconds = 300.0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  const auto destination_a = long_destination_with_suffix('x');
  const auto destination_b = long_destination_with_suffix('y');

  note_runtime_ech_failure(destination_a, 1712345678);

  const auto decision = get_runtime_ech_decision(destination_b, 1712345678, known_non_ru_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_circuit_breaker);
}

}  // namespace
