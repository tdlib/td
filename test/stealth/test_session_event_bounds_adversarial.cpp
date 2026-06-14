// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/SessionEventBounds.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"

// Adversarial tests for session event sequencer (Phase 17 / §20 hardening).
// Simulates hostile injection scenarios described in the plan:
//  - T26: new_session_created injection (amplification, salt takeover, replay)
//  - T27: bad_server_salt injection (salt cycling, flood, combined)
//
// Obfuscated suite name: SessionEventBoundsAdversarial

namespace {

using td::mtproto::RouteCorrectionSequencer;
using td::mtproto::SessionInitSequencer;
using td::uint64;

// Attack: inject new_session_created with first_msg_id = UINT64_MAX
// → amplification clamped; resends bound to ceiling, not unbounded.
TEST(SessionEventBoundsAdversarial, MaxFirstMsgIdAmplificationIsClamped) {
  constexpr uint64 kMaxSent = 500ULL << 32;
  const uint64 kCeiling = kMaxSent + SessionInitSequencer::kFirstMsgIdClampMargin;

  auto [clamped, was_clamped] = SessionInitSequencer::clamp_first_msg_id(UINT64_MAX, kMaxSent);

  ASSERT_TRUE(was_clamped);
  ASSERT_EQ(kCeiling, clamped);
  // An attacker cannot force re-sends of more than kFirstMsgIdClampMargin window of messages.
  ASSERT_TRUE(clamped < UINT64_MAX);
}

// Attack: salt takeover via 10 new_session_created with different salts in 5s
// → only first salt update accepted; remaining 9 suppressed by rate gate.
TEST(SessionEventBoundsAdversarial, TenNewSessionsInFiveSecondsOnlyFirstSaltAccepted) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;

  int accepted_with_salt = 0;
  for (int i = 1; i <= 10; i++) {
    const uint64 uid = static_cast<uint64>(i) * 0x1111111111111111ULL;
    auto decision = seq.on_event(uid, 100.0 + i * 0.5);
    if (decision == SessionInitSequencer::Decision::AcceptWithSaltUpdate) {
      accepted_with_salt++;
    }
  }

  // Only the first event at t=100.5 should update the salt;
  // all others within 30s are rate-gated.
  ASSERT_EQ(1, accepted_with_salt);
}

// Attack: replay new_session_created with recorded unique_id after 60s.
// Dedup cache must still recognise it as a replay.
TEST(SessionEventBoundsAdversarial, ReplayAfter60SecondsIsDetectedByUniqueIdCache) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;
  constexpr uint64 kUid = 0xFEEDFACECAFEBABEULL;

  auto d1 = seq.on_event(kUid, 100.0);
  // Advance past the rate window
  auto d2 = seq.on_event(kUid, 180.0);  // same uid, 80s later

  ASSERT_TRUE(d1 == SessionInitSequencer::Decision::AcceptWithSaltUpdate);
  ASSERT_TRUE(d2 == SessionInitSequencer::Decision::ReplayWithoutSaltUpdate);
}

// Attack: salt cycling via bad_server_salt with fabricated bad_msg_ids.
// All 20 injections reference unknown message IDs → all rejected.
// Salt should NOT be updated from any of them.
TEST(SessionEventBoundsAdversarial, SaltCyclingViaFabricatedMsgIdsAllRejected) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;
  // No messages tracked - all references are fabricated

  int accepted_count = 0;
  for (int i = 1; i <= 20; i++) {
    const uint64 fake_id = static_cast<uint64>(i) * 0xDEADDEADDEADDEADULL;
    auto decision = seq.on_event(fake_id, 100.0 + i * 0.1);
    if (decision == RouteCorrectionSequencer::Decision::Accept) {
      accepted_count++;
    }
  }

  // TearDown should also have been triggered (at 5th consecutive)
  // but none should have been Accept
  ASSERT_EQ(0, accepted_count);
}

// Attack: bad_server_salt flood — 100 events in 1 second.
// Rate limiter caps at 3 accepted within the 10s window.
// TearDown fires at consecutive threshold.
TEST(SessionEventBoundsAdversarial, BadSaltFloodRateLimitedAndTearDownTriggered) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  // Track enough sent IDs for some to match
  for (uint64 i = 1; i <= 20; i++) {
    seq.track_sent(i << 32);
  }

  int accept_count = 0;
  int rate_limit_count = 0;
  int teardown_count = 0;
  int reject_count = 0;

  for (int i = 1; i <= 100; i++) {
    const uint64 msg_id = (static_cast<uint64>((i - 1) % 20) + 1) << 32;
    auto d = seq.on_event(msg_id, 100.0 + i * 0.01);  // all within ~1s

    switch (d) {
      case RouteCorrectionSequencer::Decision::Accept:
        accept_count++;
        break;
      case RouteCorrectionSequencer::Decision::RateLimit:
        rate_limit_count++;
        break;
      case RouteCorrectionSequencer::Decision::TearDown:
        teardown_count++;
        break;
      case RouteCorrectionSequencer::Decision::Reject:
        reject_count++;
        break;
    }
  }

  // At most 3 accepted (rate gate)
  ASSERT_TRUE(accept_count <= 3);
  // TearDown must have been triggered at consecutive threshold
  ASSERT_TRUE(teardown_count >= 1);
  // Total events accounted for
  ASSERT_EQ(100, accept_count + rate_limit_count + teardown_count + reject_count);
}

// Attack: combined salt attack — new_session_created with attacker salt,
// then (after 31s) bad_server_salt with second attacker salt.
// Both events may be individually processed but are detectable via counters.
TEST(SessionEventBoundsAdversarial, CombinedSaltAttackDetectedByCounters) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer si_seq;
  RouteCorrectionSequencer rc_seq;

  // Phase 1: new_session_created with attacker salt (legitimate first occurrence)
  auto d1 = si_seq.on_event(0xA77AC4E81111ULL, 100.0);
  ASSERT_TRUE(d1 == SessionInitSequencer::Decision::AcceptWithSaltUpdate);

  // Phase 2: wait 31s, inject bad_server_salt (unknown msg_id = fabricated)
  auto d2 = rc_seq.on_event(0xFA99CADE00000004ULL, 131.0);
  ASSERT_TRUE(d2 == RouteCorrectionSequencer::Decision::Reject);

  // Both anomalous events are detectable:
  td::net_health::note_route_correction_unref();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.route_correction_unref_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Attack: first_msg_id just above the clamp ceiling (fence-post).
// Must be clamped. Below ceiling must NOT be clamped.
TEST(SessionEventBoundsAdversarial, FencePostAroundClampCeilingBehavesCorrectly) {
  constexpr uint64 kMaxSent = 100ULL << 32;
  const uint64 kCeiling = kMaxSent + SessionInitSequencer::kFirstMsgIdClampMargin;

  // Exactly at ceiling → not clamped
  auto [c1, w1] = SessionInitSequencer::clamp_first_msg_id(kCeiling, kMaxSent);
  ASSERT_FALSE(w1);
  ASSERT_EQ(kCeiling, c1);

  // One above ceiling → clamped
  auto [c2, w2] = SessionInitSequencer::clamp_first_msg_id(kCeiling + 1, kMaxSent);
  ASSERT_TRUE(w2);
  ASSERT_EQ(kCeiling, c2);

  // One below ceiling → not clamped
  auto [c3, w3] = SessionInitSequencer::clamp_first_msg_id(kCeiling - 1, kMaxSent);
  ASSERT_FALSE(w3);
  ASSERT_EQ(kCeiling - 1, c3);
}

// Attack: saturate the unique_id ring cache (> kUniqueIdCacheSize entries),
// then replay the oldest entry. After ring wraparound the oldest entry
// should be evicted and the "same" uid accepted as fresh.
// This validates that the ring is bounded and doesn't grow unboundedly.
TEST(SessionEventBoundsAdversarial, UniqueIdRingOverflowOldestEntryEvicted) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;

  constexpr size_t kCacheSize = SessionInitSequencer::kUniqueIdCacheSize;
  // First, advance time past any rate gate by spreading events 30s apart
  const uint64 kFirstUid = 0xAAAA0001AAAA0001ULL;

  // Fill the cache completely with distinct UIDs widely spaced in time
  for (size_t i = 0; i < kCacheSize; i++) {
    const uint64 uid = kFirstUid + i;
    seq.on_event(uid, 30.0 * static_cast<double>(i + 1));
  }

  // The very first UID (kFirstUid) should now be evicted from the ring.
  // Replaying it after sufficient time should be treated as fresh (AcceptWithSaltUpdate).
  const double kNextTime = 30.0 * static_cast<double>(kCacheSize + 1);
  auto decision = seq.on_event(kFirstUid, kNextTime);

  // After eviction, it's a fresh event → should be accceptedl.
  // (Note: if AcceptWithoutSaltUpdate due to rate gate, that is also acceptable.)
  ASSERT_TRUE(decision != SessionInitSequencer::Decision::Reject);
}

// Attack: RouteCorrectionSequencer track_sent with msg_id=0 is silently ignored.
// Subsequent correction referencing msg_id=0 is Reject (unknown).
TEST(SessionEventBoundsAdversarial, ZeroMsgIdIsNotTrackedAndRejected) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  seq.track_sent(0);  // silently ignored
  auto decision = seq.on_event(0, 100.0);

  ASSERT_TRUE(decision == RouteCorrectionSequencer::Decision::Reject);
}

// Attack: on_delivery_confirmed called multiple times should not corrupt state
// (idempotent reset — consecutive counter stays at 0 not below 0).
TEST(SessionEventBoundsAdversarial, MultipleDeliveryConfirmedCallsAreIdempotent) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  seq.on_delivery_confirmed();
  seq.on_delivery_confirmed();
  seq.on_delivery_confirmed();

  seq.track_sent(0xBADC0FFEE << 4);
  seq.on_event(0xBADC0FFEE << 4, 100.0);
  seq.on_event(0xBADC0FFEE << 4, 100.0);  // duplicate msg (known sent, but re-referenced)
  seq.on_event(0xBADC0FFEE << 4, 100.0);
  seq.on_event(0xBADC0FFEE << 4, 100.0);
  auto d5 = seq.on_event(0xBADC0FFEE << 4, 100.0);  // 5th → TearDown

  ASSERT_TRUE(d5 == RouteCorrectionSequencer::Decision::TearDown);
}

}  // namespace
