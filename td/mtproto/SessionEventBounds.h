// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

// Lightweight session event sequencer.
// Tracks ordering constraints for inbound session-lifecycle signals
// received from the remote endpoint and enforces window limits to
// keep the session within reviewed operational bounds.

#include "td/utils/common.h"

#include <array>

namespace td {
namespace mtproto {

// Sequencer for new-session-created events.
// Applies: unique_id replay guard, first_msg_id scope clamping,
// and a rate gate on salt-update operations.
class SessionInitSequencer final {
 public:
  // Decision for each new-session event.
  enum class Decision : uint8 {
    AcceptWithSaltUpdate,        // first occurrence outside the rate gate
    AcceptWithoutSaltUpdate,     // fresh unique_id, but salt update is rate-gated
    ReplayWithoutSaltUpdate,     // duplicate unique_id replay; suppress resend as well
    Reject,                      // structurally invalid (e.g. unique_id = 0)
  };

  // Process a new-session event.
  // unique_id: the opaque session identifier from the remote
  // now: current monotonic time (seconds)
  // Returns Decision and updates internal state accordingly.
  Decision on_event(uint64 unique_id, double now) noexcept;

  // Clamp first_msg_id to the highest known sent message ID plus a margin.
  // Returns (possibly clamped id, was_clamped).
  // If max_sent_msg_id == 0 (no messages sent yet), clamp is skipped.
  static std::pair<uint64, bool> clamp_first_msg_id(uint64 first_msg_id, uint64 max_sent_msg_id) noexcept;

  // Maximum number of unique_ids retained for replay detection.
  static constexpr size_t kUniqueIdCacheSize = 64;

  // Minimum interval between salt updates (seconds).
  static constexpr double kSaltUpdateMinIntervalSec = 30.0;

  // Margin added to max_sent_msg_id when clamping first_msg_id.
  // Allows for a small number of legitimately in-flight messages.
  static constexpr uint64 kFirstMsgIdClampMargin = 64ULL << 32;

 private:
  bool is_known_unique_id(uint64 unique_id) const noexcept;
  void record_unique_id(uint64 unique_id) noexcept;

  std::array<uint64, kUniqueIdCacheSize> unique_id_ring_{};
  size_t unique_id_head_{0};
  size_t unique_id_count_{0};
  double last_salt_update_at_{-1000.0};
};

// Sequencer for route-correction (bad_server_salt) events.
// Applies: sent-message-ID cross-reference, rate gate (max 3/10s),
// and a consecutive failure chain limit (5 → teardown signal).
class RouteCorrectionSequencer final {
 public:
  // Outcome of processing a route-correction event.
  enum class Decision : uint8 {
    Accept,     // passes all checks; update salt and resend
    Reject,     // bad_msg_id unknown — do not update salt
    RateLimit,  // rate gate active — do not update salt
    TearDown,   // consecutive chain limit exceeded — close session
  };

  // Process a bad_server_salt event.
  // bad_msg_id: the msg_id referenced in the correction
  // now: current monotonic time (seconds)
  // Returns Decision.  Internally tracks rate window and consecutive count.
  Decision on_event(uint64 bad_msg_id, double now) noexcept;

  // Record a sent message ID so future corrections can be validated.
  void track_sent(uint64 msg_id) noexcept;

  // Signal that a message was delivered successfully.
  // Resets the consecutive failure counter.
  void on_delivery_confirmed() noexcept;

  // Maximum number of sent message IDs retained for cross-reference.
  static constexpr size_t kSentIdCacheSize = 1024;

  // Sliding window for rate gate (seconds).
  static constexpr double kRateWindowSec = 10.0;

  // Maximum events accepted per rate window.
  static constexpr int kMaxPerWindow = 3;

  // Consecutive events (accepted or rejected) without a successful delivery
  // after which a TearDown decision is returned.
  static constexpr int kConsecutiveTeardownThreshold = 5;

 private:
  bool is_known_sent_msg_id(uint64 msg_id) const noexcept;

  std::array<uint64, kSentIdCacheSize> sent_ids_ring_{};
  size_t sent_ids_head_{0};
  size_t sent_ids_count_{0};

  // Timestamps of recent accepted events for rate-gate enforcement.
  std::array<double, kMaxPerWindow + 1> rate_ts_{};
  size_t rate_head_{0};
  size_t rate_count_{0};

  int consecutive_count_{0};
};

}  // namespace mtproto
}  // namespace td
