// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/mtproto/SessionEventBounds.h"

namespace td {
namespace mtproto {

// ---------------------------------------------------------------------------
// SessionInitSequencer
// ---------------------------------------------------------------------------

bool SessionInitSequencer::is_known_unique_id(uint64 unique_id) const noexcept {
  for (size_t i = 0; i < unique_id_count_; i++) {
    const size_t idx = (unique_id_head_ + kUniqueIdCacheSize - unique_id_count_ + i) % kUniqueIdCacheSize;
    if (unique_id_ring_[idx] == unique_id) {
      return true;
    }
  }
  return false;
}

void SessionInitSequencer::record_unique_id(uint64 unique_id) noexcept {
  unique_id_ring_[unique_id_head_] = unique_id;
  unique_id_head_ = (unique_id_head_ + 1) % kUniqueIdCacheSize;
  if (unique_id_count_ < kUniqueIdCacheSize) {
    unique_id_count_++;
  }
}

SessionInitSequencer::Decision SessionInitSequencer::on_event(uint64 unique_id, double now) noexcept {
  if (unique_id == 0) {
    return Decision::Reject;
  }

  if (is_known_unique_id(unique_id)) {
    // Replay: session recognises this unique_id already — suppress both salt
    // mutation and resend recovery.
    return Decision::ReplayWithoutSaltUpdate;
  }

  record_unique_id(unique_id);

  // Rate-gate the salt update
  if (now - last_salt_update_at_ < kSaltUpdateMinIntervalSec) {
    return Decision::AcceptWithoutSaltUpdate;
  }

  last_salt_update_at_ = now;
  return Decision::AcceptWithSaltUpdate;
}

std::pair<uint64, bool> SessionInitSequencer::clamp_first_msg_id(uint64 first_msg_id, uint64 max_sent_msg_id) noexcept {
  if (max_sent_msg_id == 0) {
    // No messages sent yet — nothing to clamp against
    return {first_msg_id, false};
  }
  const uint64 ceiling = max_sent_msg_id + kFirstMsgIdClampMargin;
  if (first_msg_id > ceiling) {
    return {ceiling, true};
  }
  return {first_msg_id, false};
}

// ---------------------------------------------------------------------------
// RouteCorrectionSequencer
// ---------------------------------------------------------------------------

bool RouteCorrectionSequencer::is_known_sent_msg_id(uint64 msg_id) const noexcept {
  for (size_t i = 0; i < sent_ids_count_; i++) {
    const size_t idx = (sent_ids_head_ + kSentIdCacheSize - sent_ids_count_ + i) % kSentIdCacheSize;
    if (sent_ids_ring_[idx] == msg_id) {
      return true;
    }
  }
  return false;
}

void RouteCorrectionSequencer::track_sent(uint64 msg_id) noexcept {
  if (msg_id == 0) {
    return;
  }
  sent_ids_ring_[sent_ids_head_] = msg_id;
  sent_ids_head_ = (sent_ids_head_ + 1) % kSentIdCacheSize;
  if (sent_ids_count_ < kSentIdCacheSize) {
    sent_ids_count_++;
  }
}

void RouteCorrectionSequencer::on_delivery_confirmed() noexcept {
  consecutive_count_ = 0;
}

RouteCorrectionSequencer::Decision RouteCorrectionSequencer::on_event(uint64 bad_msg_id, double now) noexcept {
  // Always increment consecutive counter regardless of outcome
  consecutive_count_++;

  // Check teardown threshold first (check AFTER incrementing)
  if (consecutive_count_ >= kConsecutiveTeardownThreshold) {
    return Decision::TearDown;
  }

  // Validate the referenced message ID
  if (!is_known_sent_msg_id(bad_msg_id)) {
    return Decision::Reject;
  }

  // Rate-gate: count events in the last kRateWindowSec seconds
  // Evict stale timestamps
  while (rate_count_ > 0) {
    const size_t oldest_idx = (rate_head_ + kMaxPerWindow + 1 - rate_count_) % (kMaxPerWindow + 1);
    if (rate_ts_[oldest_idx] < now - kRateWindowSec) {
      rate_count_--;
    } else {
      break;
    }
  }

  if (static_cast<int>(rate_count_) >= kMaxPerWindow) {
    return Decision::RateLimit;
  }

  // Record this event's timestamp
  rate_ts_[rate_head_] = now;
  rate_head_ = (rate_head_ + 1) % (kMaxPerWindow + 1);
  rate_count_++;

  return Decision::Accept;
}

}  // namespace mtproto
}  // namespace td
