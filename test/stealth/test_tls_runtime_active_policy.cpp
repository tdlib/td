//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::RuntimeActivePolicy;
using td::mtproto::stealth::RuntimeEchDecision;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthRuntimeParams;

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

NetworkRouteHints unknown_route() {
  NetworkRouteHints route;
  route.is_known = false;
  route.is_ru = false;
  return route;
}

NetworkRouteHints known_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = true;
  return route;
}

NetworkRouteHints known_non_ru_route() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

TEST(TlsRuntimeActivePolicy, UnknownRouteFallsBackToConfiguredNonRuActivePolicy) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params = default_runtime_stealth_params();
  params.active_policy = RuntimeActivePolicy::NonRuEgress;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  RuntimeEchDecision decision = get_runtime_ech_decision("runtime-active-policy.example", 1712345678, unknown_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);
  ASSERT_FALSE(decision.disabled_by_route);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);
}

TEST(TlsRuntimeActivePolicy, UnknownRouteFallsBackToConfiguredRuActivePolicy) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params = default_runtime_stealth_params();
  params.active_policy = RuntimeActivePolicy::RuEgress;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  RuntimeEchDecision decision = get_runtime_ech_decision("runtime-active-policy.example", 1712345678, unknown_route());
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_route);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);
}

TEST(TlsRuntimeActivePolicy, ExplicitRouteHintsOverrideConfiguredActivePolicy) {
  RuntimeParamsGuard guard;

  StealthRuntimeParams params = default_runtime_stealth_params();
  params.active_policy = RuntimeActivePolicy::RuEgress;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  auto non_ru_decision = get_runtime_ech_decision("runtime-active-policy.example", 1712345678, known_non_ru_route());
  ASSERT_TRUE(non_ru_decision.ech_mode == EchMode::Rfc9180Outer);
  ASSERT_FALSE(non_ru_decision.disabled_by_route);

  auto ru_decision = get_runtime_ech_decision("runtime-active-policy.example", 1712345678, known_ru_route());
  ASSERT_TRUE(ru_decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(ru_decision.disabled_by_route);
}

}  // namespace