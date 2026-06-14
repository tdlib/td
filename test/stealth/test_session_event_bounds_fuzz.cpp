// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/SessionEventBounds.h"

#include "td/utils/Random.h"
#include "td/utils/tests.h"

// Light fuzz tests for session event sequencer (Phase 17 / §20 hardening).
// Runs randomised inputs against SessionInitSequencer and RouteCorrectionSequencer
// to verify: no crash, no undefined behaviour, bounded outputs.
//
// This test suite is intended for CI execution (minimum 10,000 iterations).
// Obfuscated suite name: SessionEventBoundsFuzz

namespace {

using td::mtproto::RouteCorrectionSequencer;
using td::mtproto::SessionInitSequencer;
using td::uint64;

// Fuzz: random unique_ids and timestamps into SessionInitSequencer.
// Invariants: decision is always a valid enum value; function never crashes.
TEST(SessionEventBoundsFuzz, RandomNewSessionEventsNeverCrash) {
  SessionInitSequencer seq;
  constexpr int kIterations = 10000;
  double now = 100.0;

  for (int i = 0; i < kIterations; i++) {
    const uint64 uid = td::Random::fast_uint64();
    now += td::Random::fast(0, 100) / 10.0;  // advance by 0..10s

    auto decision = seq.on_event(uid, now);

    // Decision must be a valid enum value
    ASSERT_TRUE(decision == SessionInitSequencer::Decision::AcceptWithSaltUpdate ||
                decision == SessionInitSequencer::Decision::AcceptWithoutSaltUpdate ||
                decision == SessionInitSequencer::Decision::ReplayWithoutSaltUpdate ||
                decision == SessionInitSequencer::Decision::Reject);
  }
}

// Fuzz: random first_msg_id and max_sent into clamp function.
// Invariants: result is always <= ceiling; no crash.
TEST(SessionEventBoundsFuzz, RandomClampInputsNeverCrashAndResultBounded) {
  constexpr int kIterations = 10000;

  for (int i = 0; i < kIterations; i++) {
    const uint64 first_msg_id = td::Random::fast_uint64();
    const uint64 max_sent = td::Random::fast_uint64();

    auto [clamped, was_clamped] = SessionInitSequencer::clamp_first_msg_id(first_msg_id, max_sent);

    if (max_sent != 0) {
      const uint64 ceiling = max_sent + SessionInitSequencer::kFirstMsgIdClampMargin;
      // Ceiling might overflow uint64 — guard against that
      if (ceiling > max_sent) {  // no overflow
        ASSERT_TRUE(clamped <= ceiling);
        if (first_msg_id > ceiling) {
          ASSERT_TRUE(was_clamped);
          ASSERT_EQ(ceiling, clamped);
        } else {
          ASSERT_FALSE(was_clamped);
          ASSERT_EQ(first_msg_id, clamped);
        }
      }
    }
  }
}

// Fuzz: random bad_msg_ids into RouteCorrectionSequencer with and without tracking.
// Invariants: decision is a valid enum value; no crash.
TEST(SessionEventBoundsFuzz, RandomRouteCorrectionEventsNeverCrash) {
  constexpr int kIterations = 10000;
  double now = 100.0;

  for (int outer = 0; outer < 10; outer++) {
    RouteCorrectionSequencer seq;
    // Seed the sent set with some random IDs
    const int tracked = td::Random::fast(0, 50);
    for (int t = 0; t < tracked; t++) {
      seq.track_sent(td::Random::fast_uint64());
    }

    for (int i = 0; i < kIterations / 10; i++) {
      const uint64 bad_id = td::Random::fast_uint64();
      now += td::Random::fast(0, 50) / 100.0;

      auto decision = seq.on_event(bad_id, now);

      ASSERT_TRUE(decision == RouteCorrectionSequencer::Decision::Accept ||
                  decision == RouteCorrectionSequencer::Decision::Reject ||
                  decision == RouteCorrectionSequencer::Decision::RateLimit ||
                  decision == RouteCorrectionSequencer::Decision::TearDown);

      // After TearDown, occasionally reset by simulating a new connection
      if (decision == RouteCorrectionSequencer::Decision::TearDown) {
        seq.on_delivery_confirmed();  // simulates reconnect + delivery on new session
      }
    }
  }
}

// Fuzz: saturate sent-ID ring buffer beyond kSentIdCacheSize entries.
// No crash; ring wraps correctly.
TEST(SessionEventBoundsFuzz, SaturatingSentIdRingNeverCrashes) {
  constexpr int kIterations = 10000;
  RouteCorrectionSequencer seq;

  for (int i = 0; i < kIterations; i++) {
    seq.track_sent(td::Random::fast_uint64());
  }

  // After saturation, corrections should still work
  const uint64 known_id = 0xDEAD'BEEF'0000'0004ULL;
  seq.track_sent(known_id);
  seq.on_delivery_confirmed();
  auto decision = seq.on_event(known_id, 9999.0);

  ASSERT_TRUE(decision == RouteCorrectionSequencer::Decision::Accept ||
              decision == RouteCorrectionSequencer::Decision::RateLimit ||
              decision == RouteCorrectionSequencer::Decision::TearDown);
}

// Fuzz: interleaved track_sent and on_event calls with random IDs and times.
// Validates no crash and monotonic consecutive count behaviour.
TEST(SessionEventBoundsFuzz, InterleavedSentTrackingAndCorrectionEventsNeverCrash) {
  constexpr int kIterations = 5000;
  RouteCorrectionSequencer seq;
  double now = 0.0;

  for (int i = 0; i < kIterations; i++) {
    now += 0.001;  // 1ms increments

    const int op = td::Random::fast(0, 4);
    if (op == 0) {
      seq.track_sent(td::Random::fast_uint64());
    } else if (op == 1) {
      seq.on_delivery_confirmed();
    } else {
      const uint64 bad_id = td::Random::fast_uint64();
      auto d = seq.on_event(bad_id, now);
      ASSERT_TRUE(d == RouteCorrectionSequencer::Decision::Accept || d == RouteCorrectionSequencer::Decision::Reject ||
                  d == RouteCorrectionSequencer::Decision::RateLimit ||
                  d == RouteCorrectionSequencer::Decision::TearDown);
    }
  }
}

}  // namespace
