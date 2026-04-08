// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial tests: ECH circuit breaker and route failure cache
// must resist abuse by censors who replay partial failures to
// permanently disable ECH, or by MitM proxies that selectively
// drop ECH connections.

#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::note_runtime_ech_success;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;

NetworkRouteHints non_ru() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return hints;
}

NetworkRouteHints ru() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = true;
  return hints;
}

TEST(TlsEchCircuitBreakerAdversarial, RuRouteMustAlwaysDisableEch) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  for (int i = 0; i < 100; i++) {
    auto decision = get_runtime_ech_decision("www.google.com", 1712345678 + i, ru());
    ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  }
}

TEST(TlsEchCircuitBreakerAdversarial, RepeatedFailuresMustTriggersCircuitBreaker) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  // Simulate 10 consecutive ECH failures.
  for (int i = 0; i < 10; i++) {
    note_runtime_ech_failure("www.google.com", 1712345678 + i);
  }

  auto decision = get_runtime_ech_decision("www.google.com", 1712345690, non_ru());
  // After enough failures, circuit breaker should disable ECH.
  ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(decision.disabled_by_circuit_breaker);
}

TEST(TlsEchCircuitBreakerAdversarial, SuccessAfterFailureMustResetCircuitBreaker) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  // Generate failures.
  for (int i = 0; i < 10; i++) {
    note_runtime_ech_failure("www.google.com", 1712345678 + i);
  }

  // One success should help recover.
  note_runtime_ech_success("www.google.com", 1712345690);

  // Give enough time for TTL to expire (check after a time jump).
  auto decision = get_runtime_ech_decision("www.google.com", 1712345690 + 3600, non_ru());
  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);
}

TEST(TlsEchCircuitBreakerAdversarial, DifferentDestinationsMustHaveIndependentState) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  // Fail only domain A.
  for (int i = 0; i < 10; i++) {
    note_runtime_ech_failure("blocked-server.com", 1712345678 + i);
  }

  // Domain B must still have ECH enabled.
  auto decision = get_runtime_ech_decision("unblocked-server.com", 1712345690, non_ru());
  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);

  // Domain A should be circuit-broken.
  auto decision_a = get_runtime_ech_decision("blocked-server.com", 1712345690, non_ru());
  ASSERT_TRUE(decision_a.ech_mode == EchMode::Disabled);
}

TEST(TlsEchCircuitBreakerAdversarial, ZeroFailuresMustNotTriggerCircuitBreaker) {
  reset_runtime_ech_failure_state_for_tests();
  reset_runtime_ech_counters_for_tests();

  auto decision = get_runtime_ech_decision("fresh-server.com", 1712345678, non_ru());
  ASSERT_TRUE(decision.ech_mode == EchMode::Rfc9180Outer);
  ASSERT_FALSE(decision.disabled_by_circuit_breaker);
  ASSERT_FALSE(decision.disabled_by_route);
}

}  // namespace
