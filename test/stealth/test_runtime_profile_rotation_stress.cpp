// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Concurrency and load stress for the in-memory quarantine map: concurrent
// selection and failure/success recording for the same destination must stay
// crash-free and self-consistent (the map is mutex-protected), and a sustained
// fail/connect/success loop must always resolve to an allowed profile.

#include "RuntimeProfileRotationTestSupport.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <atomic>
#include <thread>
#include <vector>

namespace {

using namespace td::mtproto::stealth;
using namespace td::mtproto::stealth::rotation_test;

constexpr td::int32 kUnixTime = 1712345678;

// Many threads hammer selection and failure/success recording for one
// destination; the run must finish without a crash, data race, or out-of-set
// selection, and counters stay internally consistent.
TEST(RuntimeProfileRotationStress, ConcurrentSelectionAndRecordingForOneDestination) {
  RotationTestGuard guard;
  auto params = rotation_params(linux_platform(), TransportConfidence::Strong, false, true, 2);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "stress-concurrent.example.com";

  constexpr int kThreads = 8;
  constexpr int kIterations = 2000;
  std::atomic<bool> out_of_set{false};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int t = 0; t < kThreads; t++) {
    workers.emplace_back([&, t] {
      for (int i = 0; i < kIterations; i++) {
        auto decision = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
        if (!platform_allows(linux_platform(), decision.profile)) {
          out_of_set.store(true);
        }
        RuntimeProfileWireVariant variant{decision.profile, decision.hello_uses_ech};
        if ((i + t) % 3 == 0) {
          note_runtime_profile_failure(dest, variant, RuntimeProfileFailureSignal::MalformedHelloResponse);
        } else if ((i + t) % 3 == 1) {
          note_runtime_profile_failure(dest, variant, RuntimeProfileFailureSignal::TransportRejectionAfterHello);
        } else {
          note_runtime_profile_success(dest, variant);
        }
      }
    });
  }
  for (auto &worker : workers) {
    worker.join();
  }

  ASSERT_FALSE(out_of_set.load());
  // The final selection is still a valid allowed profile after the storm.
  auto final_decision = pick_runtime_profile_adaptive(dest, kUnixTime, linux_platform(), EchMode::Disabled);
  ASSERT_TRUE(platform_allows(linux_platform(), final_decision.profile));
}

// A sustained fail -> rotate -> success -> restore loop always resolves to an
// allowed profile and returns to baseline after each success clears the variant.
TEST(RuntimeProfileRotationStress, RepeatedFailConnectSuccessLoopStaysConsistent) {
  RotationTestGuard guard;
  auto params = rotation_params(windows_platform(), TransportConfidence::Strong, false, true, 2);
  ASSERT_TRUE(set_runtime_stealth_params_for_tests(params).is_ok());
  const td::string dest = "stress-loop.example.com";

  for (int round = 0; round < 5000; round++) {
    auto baseline = pick_runtime_profile_adaptive(dest, kUnixTime, windows_platform(), EchMode::Disabled);
    ASSERT_TRUE(platform_allows(windows_platform(), baseline.profile));

    quarantine_variant(dest, baseline.profile, baseline.hello_uses_ech, 2);
    auto rotated = pick_runtime_profile_adaptive(dest, kUnixTime, windows_platform(), EchMode::Disabled);
    ASSERT_TRUE(platform_allows(windows_platform(), rotated.profile));

    note_runtime_profile_success(dest, RuntimeProfileWireVariant{baseline.profile, baseline.hello_uses_ech});
    note_runtime_profile_success(dest, RuntimeProfileWireVariant{rotated.profile, rotated.hello_uses_ech});

    auto restored = pick_runtime_profile_adaptive(dest, kUnixTime, windows_platform(), EchMode::Disabled);
    ASSERT_TRUE(restored.profile == baseline.profile);
    ASSERT_FALSE(restored.avoided_quarantined_profile);
  }
}

}  // namespace
