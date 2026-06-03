// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/tests.h"

#include <limits>

namespace {

using td::int32;
using td::mtproto::stealth::default_runtime_stealth_params;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::set_runtime_stealth_params_for_tests;

constexpr int32 kBucketSeconds = 86400;
constexpr int32 kDestinationCount = 5000;
constexpr int32 kMaxSafeBucket = std::numeric_limits<int32>::max() / kBucketSeconds;
constexpr int32 kStartBucket = kMaxSafeBucket - kDestinationCount - 1;

static_assert(kStartBucket > 0, "stress test bucket range must stay within positive int32 unix time");

class RuntimeGuard final {
 public:
  RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

NetworkRouteHints known_non_ru() {
  NetworkRouteHints route;
  route.is_known = true;
  route.is_ru = false;
  return route;
}

TEST(EchRouteFailureBucketBoundaryStress, ThousandsOfDestinationsStayDisabledAcrossImmediateDayBoundaryCrossings) {
  RuntimeGuard guard;

  auto params = default_runtime_stealth_params();
  params.route_failure.ech_failure_threshold = 1;
  params.route_failure.ech_disable_ttl_seconds = 300.0;
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  for (int index = 0; index < kDestinationCount; index++) {
    const int32 bucket = kStartBucket + index;
    const int32 before_midnight = bucket * kBucketSeconds + (kBucketSeconds - 1);
    const int32 after_midnight = (bucket + 1) * kBucketSeconds;
    const td::string dest = "boundary-stress-" + td::to_string(index) + ".example.com";

    note_runtime_ech_failure(dest, before_midnight);
    auto decision = get_runtime_ech_decision(dest, after_midnight, known_non_ru());
    ASSERT_TRUE(decision.ech_mode == EchMode::Disabled);
    ASSERT_TRUE(decision.disabled_by_circuit_breaker);
  }
}

}  // namespace