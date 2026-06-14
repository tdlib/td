// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Deterministic (reproducible) light fuzz of the rotation seam: mutated
// destination strings must never crash or fan out an ambiguous quarantine key,
// and a malformed/out-of-enum failure-class input must fail closed without
// touching the quarantine counters.

#include "RuntimeProfileRotationTestSupport.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;

constexpr td::int32 kUnixTime = 1712345678;

std::vector<td::string> mutated_destinations() {
  std::vector<td::string> out = {
      "",
      ".",
      "..",
      "...",
      ".leading.example.com",
      "trailing.example.com.",
      "..double..dots..example..com..",
      "MiXeD.CaSe.Example.COM",
      "mixed.case.example.com",
  };
  // A very long label sequence within the project's domain length bounds.
  td::string long_label;
  for (int i = 0; i < 40; i++) {
    long_label += "abcd.";
  }
  long_label += "example.com";
  out.push_back(long_label);
  return out;
}

// No mutated destination may crash selection or failure recording, regardless of
// rotation state.
TEST(RuntimeProfileRotationFuzz, MutatedDestinationsNeverCrash) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, 2);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());

  for (const auto &dest : mutated_destinations()) {
    auto decision = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
    ASSERT_TRUE(platform_allows(linux_platform(), decision.profile));
    note_runtime_profile_failure(dest, RuntimeProfileWireVariant{decision.profile, decision.hello_uses_ech},
                                 RuntimeProfileFailureSignal::MalformedHelloResponse);
    note_runtime_profile_success(dest, RuntimeProfileWireVariant{decision.profile, decision.hello_uses_ech});
    // Re-select after the churn: still inside the allowed set, never crashes.
    auto again = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
    ASSERT_TRUE(platform_allows(linux_platform(), again.profile));
  }
}

// An out-of-enum / malformed failure-class value must be treated as not-eligible:
// no quarantine, no counter increment, no crash (fail closed).
TEST(RuntimeProfileRotationFuzz, MalformedFailureClassFailsClosed) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, 2);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "fuzz-class.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);

  const td::uint8 garbage_values[] = {5, 17, 99, 200, 255};
  for (auto raw : garbage_values) {
    auto signal = static_cast<RuntimeProfileFailureSignal>(raw);
    ASSERT_FALSE(runtime_profile_failure_signal_is_quarantine_eligible(signal));
    for (int i = 0; i < 8; i++) {
      note_runtime_profile_failure(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech}, signal);
    }
  }

  ASSERT_EQ(0u, get_runtime_profile_rotation_counters().profile_failure_recorded_total);
  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == baseline.profile);
  ASSERT_FALSE(after.avoided_quarantined_profile);
}

// Route-level breakage after a completed hello is not evidence that a specific
// fingerprint got blocked. Rotating on this signal burns profile budget on a
// path outage and can hide the real failure mode from operators.
TEST(RuntimeProfileRotationFuzz, TransportRejectionAfterHelloDoesNotQuarantineProfiles) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, 2);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "transport-reject.example.com";

  auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_FALSE(runtime_profile_failure_signal_is_quarantine_eligible(
      RuntimeProfileFailureSignal::TransportRejectionAfterHello));

  for (int i = 0; i < 8; i++) {
    note_runtime_profile_failure(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech},
                                 RuntimeProfileFailureSignal::TransportRejectionAfterHello);
  }

  ASSERT_EQ(0u, get_runtime_profile_rotation_counters().profile_failure_recorded_total);
  auto after = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(after.profile == baseline.profile);
  ASSERT_FALSE(after.avoided_quarantined_profile);
}

}  // namespace
