// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/SessionEventBounds.h"

#include "td/telegram/net/NetReliabilityMonitor.h"

#include "td/utils/tests.h"

// Contract tests for the session event sequencer subsystem (Phase 17 / §20 hardening).
// Covers: new-session event dedup, first_msg_id clamping, salt-update rate gate,
//         route-correction cross-reference, rate gate, and consecutive chain limit.
//
// Obfuscated suite name: SessionEventBoundsContract
// (true purpose: MTProto service message injection hardening — anti-T26, T27)

namespace {

using td::mtproto::RouteCorrectionSequencer;
using td::mtproto::SessionInitSequencer;
using td::uint64;

// ---------------------------------------------------------------------------
// SessionInitSequencer — new-session event tests
// ---------------------------------------------------------------------------

// Contract: first occurrence of a unique_id with no prior salt update
// → AcceptWithSaltUpdate; counter NOT fired; salt update performed.
TEST(SessionEventBoundsContract, NewSessionFirstOccurrenceAcceptedWithSaltUpdate) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;
  constexpr double kNow = 100.0;

  auto decision = seq.on_event(0xDEADBEEF12345678ULL, kNow);

  ASSERT_TRUE(decision == SessionInitSequencer::Decision::AcceptWithSaltUpdate);
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(0u, snap.counters.session_init_replay_total);
  ASSERT_EQ(0u, snap.counters.session_init_rate_gate_total);
}

// Contract: second occurrence of the same unique_id is a replay signal, not a
// rate-gated new session.
TEST(SessionEventBoundsContract, DuplicateUniqueIdReturnedWithoutSaltUpdate) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;
  constexpr double kNow = 100.0;

  seq.on_event(0xAAAAAAAABBBBBBBBULL, kNow);
  // Same unique_id again (typical replay scenario)
  auto decision = seq.on_event(0xAAAAAAAABBBBBBBBULL, kNow + 60.0);

  ASSERT_TRUE(decision == SessionInitSequencer::Decision::ReplayWithoutSaltUpdate);
  // Caller must fire the counter; verify we can call it
  td::net_health::note_session_init_replay();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.session_init_replay_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Contract: two distinct new-session events within 30s
// → second returns AcceptWithoutSaltUpdate (rate gate);
//   session_init_rate_gate_total counter fires.
TEST(SessionEventBoundsContract, RateGateSuppressesSecondSaltUpdateWithin30s) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;
  constexpr double kNow = 200.0;

  auto d1 = seq.on_event(0x1111111111111111ULL, kNow);
  auto d2 = seq.on_event(0x2222222222222222ULL, kNow + 5.0);  // only 5s later

  ASSERT_TRUE(d1 == SessionInitSequencer::Decision::AcceptWithSaltUpdate);
  ASSERT_TRUE(d2 == SessionInitSequencer::Decision::AcceptWithoutSaltUpdate);

  td::net_health::note_session_init_rate_gate();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.session_init_rate_gate_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Contract: two distinct new-session events separated by >30s
// → both return AcceptWithSaltUpdate (no rate gate).
TEST(SessionEventBoundsContract, TwoNewSessionsAfter30sIntervalBothAccepted) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;
  constexpr double kNow = 300.0;

  auto d1 = seq.on_event(0x3333333333333333ULL, kNow);
  auto d2 = seq.on_event(0x4444444444444444ULL, kNow + 30.0);  // exactly at boundary

  ASSERT_TRUE(d1 == SessionInitSequencer::Decision::AcceptWithSaltUpdate);
  ASSERT_TRUE(d2 == SessionInitSequencer::Decision::AcceptWithSaltUpdate);
}

// Contract: unique_id == 0 is structurally invalid → Reject.
TEST(SessionEventBoundsContract, ZeroUniqueIdIsRejected) {
  td::net_health::reset_net_monitor_for_tests();
  SessionInitSequencer seq;

  auto decision = seq.on_event(0, 100.0);

  ASSERT_TRUE(decision == SessionInitSequencer::Decision::Reject);
}

// Contract: first_msg_id within max_sent + margin → not clamped.
TEST(SessionEventBoundsContract, FirstMsgIdWithinBoundsNotClamped) {
  constexpr uint64 kMaxSent = 1000ULL << 32;
  constexpr uint64 kFirstMsgId = kMaxSent + 10ULL;  // within margin

  auto [clamped, was_clamped] = SessionInitSequencer::clamp_first_msg_id(kFirstMsgId, kMaxSent);

  ASSERT_FALSE(was_clamped);
  ASSERT_EQ(kFirstMsgId, clamped);
}

// Contract: first_msg_id > max_sent + margin → clamped; counter fires.
TEST(SessionEventBoundsContract, FirstMsgIdExceedsBoundIsClamped) {
  td::net_health::reset_net_monitor_for_tests();
  constexpr uint64 kMaxSent = 1000ULL << 32;
  constexpr uint64 kCeiling = kMaxSent + SessionInitSequencer::kFirstMsgIdClampMargin;
  constexpr uint64 kFirstMsgId = kCeiling + 1'000'000'000ULL;  // way beyond ceiling

  auto [clamped, was_clamped] = SessionInitSequencer::clamp_first_msg_id(kFirstMsgId, kMaxSent);

  ASSERT_TRUE(was_clamped);
  ASSERT_EQ(kCeiling, clamped);
  ASSERT_TRUE(clamped < kFirstMsgId);

  td::net_health::note_session_init_scope_clamp();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.session_init_scope_clamp_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Contract: first_msg_id == UINT64_MAX is clamped (overflow-attack variant).
TEST(SessionEventBoundsContract, MaxUint64FirstMsgIdIsClamped) {
  constexpr uint64 kMaxSent = 1000ULL << 32;
  auto [clamped, was_clamped] = SessionInitSequencer::clamp_first_msg_id(UINT64_MAX, kMaxSent);

  ASSERT_TRUE(was_clamped);
  ASSERT_TRUE(clamped < UINT64_MAX);
}

// Contract: no messages sent yet (max_sent == 0) → clamp is skipped.
TEST(SessionEventBoundsContract, NoSentMessagesClampIsSkipped) {
  constexpr uint64 kFirstMsgId = 999999ULL << 32;
  auto [clamped, was_clamped] = SessionInitSequencer::clamp_first_msg_id(kFirstMsgId, 0);

  ASSERT_FALSE(was_clamped);
  ASSERT_EQ(kFirstMsgId, clamped);
}

// ---------------------------------------------------------------------------
// RouteCorrectionSequencer — bad_server_salt tests
// ---------------------------------------------------------------------------

// Contract: bad_msg_id matches a tracked sent message → Accept; no counter.
TEST(SessionEventBoundsContract, RouteCorrectionWithKnownMsgIdAccepted) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;
  constexpr uint64 kMsgId = 0xABC0000000000004ULL;
  seq.track_sent(kMsgId);

  auto decision = seq.on_event(kMsgId, 100.0);

  ASSERT_TRUE(decision == RouteCorrectionSequencer::Decision::Accept);
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(0u, snap.counters.route_correction_unref_total);
  ASSERT_EQ(0u, snap.counters.route_correction_rate_gate_total);
}

// Contract: bad_msg_id not in sent set → Reject; caller fires counter.
TEST(SessionEventBoundsContract, RouteCorrectionWithUnknownMsgIdRejected) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;
  // No messages tracked

  auto decision = seq.on_event(0xDEADC0DE00000004ULL, 100.0);

  ASSERT_TRUE(decision == RouteCorrectionSequencer::Decision::Reject);
  td::net_health::note_route_correction_unref();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.route_correction_unref_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Contract: 3 corrections within 10s window → all 3 accepted; 4th → RateLimit.
TEST(SessionEventBoundsContract, FourthRouteCorrectionWithin10sIsRateLimited) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  for (uint64 i = 1; i <= 4; i++) {
    seq.track_sent(i << 32);
  }

  auto d1 = seq.on_event(1ULL << 32, 100.0);
  auto d2 = seq.on_event(2ULL << 32, 100.1);
  auto d3 = seq.on_event(3ULL << 32, 100.2);
  auto d4 = seq.on_event(4ULL << 32, 100.3);  // 4th within window

  ASSERT_TRUE(d1 == RouteCorrectionSequencer::Decision::Accept);
  ASSERT_TRUE(d2 == RouteCorrectionSequencer::Decision::Accept);
  ASSERT_TRUE(d3 == RouteCorrectionSequencer::Decision::Accept);
  ASSERT_TRUE(d4 == RouteCorrectionSequencer::Decision::RateLimit);

  td::net_health::note_route_correction_rate_gate();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.route_correction_rate_gate_total);
}

// Contract: 5 consecutive events without delivery confirmation → TearDown.
TEST(SessionEventBoundsContract, FiveConsecutiveRouteCorrectionsTriggerTearDown) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  // Pre-load 5 sent IDs spaced far enough apart in time to avoid rate gate
  for (uint64 i = 1; i <= 5; i++) {
    seq.track_sent(i << 32);
  }

  RouteCorrectionSequencer::Decision last_decision = RouteCorrectionSequencer::Decision::Accept;
  for (int i = 1; i <= 5; i++) {
    last_decision = seq.on_event(static_cast<uint64>(i) << 32, 100.0 + i * 4.0);  // every 4s = within 10s only 3
    // Do NOT call on_delivery_confirmed
  }

  ASSERT_TRUE(last_decision == RouteCorrectionSequencer::Decision::TearDown);
  td::net_health::note_route_correction_chain_reset();
  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(1u, snap.counters.route_correction_chain_reset_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Contract: successful delivery resets consecutive counter.
TEST(SessionEventBoundsContract, DeliveryConfirmedResetsConsecutiveCounter) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  for (uint64 i = 1; i <= 10; i++) {
    seq.track_sent(i << 32);
  }

  // Advance 4 events
  for (int i = 1; i <= 4; i++) {
    seq.on_event(static_cast<uint64>(i) << 32, 100.0 + i * 4.0);
  }

  // Confirm delivery → resets counter
  seq.on_delivery_confirmed();

  // Now the 5th event should NOT trigger TearDown (counter was reset)
  for (int i = 5; i <= 9; i++) {
    auto d = seq.on_event(static_cast<uint64>(i) << 32, 200.0 + (i - 5) * 4.0);
    // Expect not TearDown for first 4 after reset
    if (i < 9) {
      ASSERT_TRUE(d != RouteCorrectionSequencer::Decision::TearDown);
    }
  }
}

// Contract: rate window expires after kRateWindowSec seconds.
// After expiry, the slot is freed and new events can be accepted.
TEST(SessionEventBoundsContract, RateWindowExpiresAllowsFurtherAcceptance) {
  td::net_health::reset_net_monitor_for_tests();
  RouteCorrectionSequencer seq;

  for (uint64 i = 1; i <= 6; i++) {
    seq.track_sent(i << 32);
  }

  // Fill the window
  seq.on_event(1ULL << 32, 0.0);
  seq.on_event(2ULL << 32, 1.0);
  seq.on_event(3ULL << 32, 2.0);

  // 4th within window is rate-limited
  auto d4 = seq.on_event(4ULL << 32, 3.0);
  ASSERT_TRUE(d4 == RouteCorrectionSequencer::Decision::RateLimit);

  // Fast-forward past the rate window
  seq.on_delivery_confirmed();               // reset consecutive
  auto d5 = seq.on_event(5ULL << 32, 15.0);  // 15s > kRateWindowSec=10s

  ASSERT_TRUE(d5 == RouteCorrectionSequencer::Decision::Accept);
}

// Contract: note_route_correction_chain_reset raises health state to Suspicious.
TEST(SessionEventBoundsContract, ChainResetCounterEscalatesMonitorState) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_route_correction_chain_reset();

  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
  ASSERT_EQ(1u, snap.counters.route_correction_chain_reset_total);
}

// Contract: note_session_init_scope_clamp raises health state to Suspicious.
TEST(SessionEventBoundsContract, ScopeClampCounterEscalatesMonitorState) {
  td::net_health::reset_net_monitor_for_tests();

  td::net_health::note_session_init_scope_clamp();

  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Suspicious);
}

// Contract: all six new counters start at zero on reset.
TEST(SessionEventBoundsContract, AllNewCountersStartAtZeroAfterReset) {
  td::net_health::reset_net_monitor_for_tests();

  auto snap = td::net_health::get_net_monitor_snapshot();
  ASSERT_EQ(0u, snap.counters.session_init_replay_total);
  ASSERT_EQ(0u, snap.counters.session_init_scope_clamp_total);
  ASSERT_EQ(0u, snap.counters.session_init_rate_gate_total);
  ASSERT_EQ(0u, snap.counters.route_correction_unref_total);
  ASSERT_EQ(0u, snap.counters.route_correction_rate_gate_total);
  ASSERT_EQ(0u, snap.counters.route_correction_chain_reset_total);
  ASSERT_TRUE(snap.state == td::net_health::NetMonitorState::Healthy);
}

}  // namespace
