// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"

#include <cstdio>
#include <limits>

namespace td {
namespace mtproto {
namespace stealth {

namespace {

constexpr int32 kMinTlsRecordSize = 256;
constexpr int32 kMaxTlsRecordSize = 16384;

int32 clamp_tls_record_size(int32 size) {
  return std::max(kMinTlsRecordSize, std::min(size, kMaxTlsRecordSize));
}

int32 adjust_tls_record_size_for_payload_overhead(int32 payload_cap, int32 payload_overhead) {
  auto safe_overhead = std::max<int64>(0, payload_overhead);
  auto adjusted = static_cast<int64>(payload_cap) + safe_overhead;
  return static_cast<int32>(std::max<int64>(kMinTlsRecordSize, std::min<int64>(adjusted, kMaxTlsRecordSize)));
}

TrafficHint normalize_drs_hint(TrafficHint hint) {
  return hint == TrafficHint::Unknown ? TrafficHint::Interactive : hint;
}

void fail_closed_on_ring_overflow() {
  // Emit a direct stderr marker before aborting so the death test remains
  // deterministic even when the logging backend output is truncated.
  std::fputs("Stealth ring overflow invariant broken\n", stderr);
  std::fflush(stderr);
  LOG(FATAL) << "Stealth ring overflow invariant broken";
}

size_t account_transport_payload_overhead(size_t written_bytes, int32 payload_overhead) {
  if (payload_overhead <= 0) {
    return written_bytes;
  }
  auto safe_overhead = static_cast<size_t>(payload_overhead);
  auto max_size = std::numeric_limits<size_t>::max();
  if (written_bytes > max_size - safe_overhead) {
    return max_size;
  }
  return written_bytes + safe_overhead;
}

class BatchBuilder final {
 public:
  BatchBuilder(int32 cap, size_t prepend, size_t append, TrafficHint hint)
      : cap_(cap), prepend_(prepend), append_(append), hint_(hint) {
  }

  void push(ShaperPendingWrite &&pending_write) {
    total_payload_bytes_ += pending_write.message.size();
    items_.push_back(std::move(pending_write));
  }

  bool can_append(const ShaperPendingWrite &pending_write) const {
    if (items_.empty()) {
      return true;
    }
    if (items_.front().quick_ack || pending_write.quick_ack) {
      return false;
    }
    if (normalize_drs_hint(pending_write.hint) != hint_) {
      return false;
    }
    return total_payload_bytes_ + pending_write.message.size() <= static_cast<size_t>(cap_);
  }

  void append(ShaperPendingWrite &&pending_write) {
    total_payload_bytes_ += pending_write.message.size();
    items_.push_back(std::move(pending_write));
  }

  bool can_coalesce() const {
    if (items_.size() <= 1) {
      return false;
    }
    for (const auto &item : items_) {
      if (item.quick_ack) {
        return false;
      }
    }
    return true;
  }

  BufferWriter take_coalesced_message() {
    CHECK(can_coalesce());
    BufferWriter writer(Slice(), prepend_, append_ + total_payload_bytes_);
    for (const auto &item : items_) {
      auto slice = item.message.as_slice();
      writer.prepare_append().truncate(slice.size()).copy_from(slice);
      writer.confirm_append(slice.size());
    }
    return writer;
  }

  vector<ShaperPendingWrite> &items() {
    return items_;
  }

  size_t total_payload_bytes() const {
    return total_payload_bytes_;
  }

 private:
  int32 cap_{0};
  size_t prepend_{0};
  size_t append_{0};
  TrafficHint hint_{TrafficHint::Interactive};
  size_t total_payload_bytes_{0};
  vector<ShaperPendingWrite> items_;
};

}  // namespace

Result<unique_ptr<StealthTransportDecorator>> StealthTransportDecorator::create(unique_ptr<IStreamTransport> inner,
                                                                                StealthConfig config,
                                                                                unique_ptr<IRng> rng,
                                                                                unique_ptr<IClock> clock) {
  if (inner == nullptr) {
    return Status::Error("inner transport must not be null");
  }
  if (rng == nullptr) {
    return Status::Error("rng must not be null");
  }
  if (clock == nullptr) {
    return Status::Error("clock must not be null");
  }
  TRY_STATUS(config.validate());

  return unique_ptr<StealthTransportDecorator>(
      new StealthTransportDecorator(std::move(inner), std::move(config), std::move(rng), std::move(clock)));
}

StealthTransportDecorator::StealthTransportDecorator(unique_ptr<IStreamTransport> inner, StealthConfig config,
                                                     unique_ptr<IRng> rng, unique_ptr<IClock> clock)
    : inner_(std::move(inner))
    , config_(std::move(config))
    , rng_(std::move(rng))
    , clock_(std::move(clock))
    , ipt_controller_(config_.ipt_params, *rng_)
    , bypass_ring_(config_.ring_capacity)
    , ring_(config_.ring_capacity)
    , high_watermark_(config_.high_watermark)
    , low_watermark_(config_.low_watermark)
    , drs_(config_.drs_policy, *rng_) {
  CHECK(inner_ != nullptr);
  CHECK(rng_ != nullptr);
  CHECK(clock_ != nullptr);
  CHECK(config_.validate().is_ok());
  initial_record_size_ = config_.sample_initial_record_size(*rng_);
  current_record_size_ = initial_record_size_;
  if (inner_->supports_tls_record_sizing()) {
    inner_->set_max_tls_record_size(current_record_size_);
  }
}

Result<size_t> StealthTransportDecorator::read_next(BufferSlice *message, uint32 *quick_ack) {
  return inner_->read_next(message, quick_ack);
}

bool StealthTransportDecorator::support_quick_ack() const {
  return inner_->support_quick_ack();
}

void StealthTransportDecorator::write(BufferWriter &&message, bool quick_ack) {
  auto hint = pending_hint_;
  pending_hint_ = TrafficHint::Unknown;

  auto has_pending_data = queued_write_count() != 0;
  auto delay_us = ipt_controller_.next_delay_us(has_pending_data, hint);
  auto send_at = clock_->now() + static_cast<double>(delay_us) / 1e6;
  ShaperPendingWrite pending_write{std::move(message), quick_ack, send_at, hint};
  if (queued_write_count() >= config_.ring_capacity) {
    overflow_invariant_hits_++;
    fail_closed_on_ring_overflow();
  }

  auto &target_ring = delay_us == 0 ? bypass_ring_ : ring_;
  if (!target_ring.try_enqueue(std::move(pending_write))) {
    overflow_invariant_hits_++;
    fail_closed_on_ring_overflow();
  }

  if (queued_write_count() >= high_watermark_) {
    backpressure_latched_ = true;
  }
}

bool StealthTransportDecorator::can_read() const {
  return inner_->can_read();
}

bool StealthTransportDecorator::can_write() const {
  return inner_->can_write() && !backpressure_latched_;
}

void StealthTransportDecorator::init(ChainBufferReader *input, ChainBufferWriter *output) {
  inner_->init(input, output);
}

size_t StealthTransportDecorator::max_prepend_size() const {
  return inner_->max_prepend_size();
}

size_t StealthTransportDecorator::max_append_size() const {
  return inner_->max_append_size();
}

TransportType StealthTransportDecorator::get_type() const {
  return inner_->get_type();
}

bool StealthTransportDecorator::use_random_padding() const {
  return inner_->use_random_padding();
}

void StealthTransportDecorator::pre_flush_write(double now) {
  if (!has_manual_record_size_override_ && has_drs_activity_ && queued_write_count() != 0 &&
      drs_.should_reset_after_idle(now - last_drs_activity_at_)) {
    drs_.notify_idle();
  }

  auto write_batch = [this, now](ShaperRingBuffer &ring) {
    if (!inner_->can_write() || ring.empty() || ring.earliest_deadline() > now) {
      return false;
    }

    BatchBuilder *batch_ptr = nullptr;
    std::optional<BatchBuilder> batch;
    int32 batch_payload_overhead = 0;
    ring.drain_ready(now, [&](ShaperPendingWrite &pending_write) {
      auto hint = normalize_drs_hint(pending_write.hint);
      if (!batch.has_value()) {
        int32 payload_cap = current_record_size_;
        int32 effective_record_size = current_record_size_;
        int32 payload_overhead = 0;
        if (!has_manual_record_size_override_) {
          payload_cap = drs_.next_payload_cap(hint);
          current_record_size_ = clamp_tls_record_size(payload_cap);
          payload_overhead = inner_->tls_record_sizing_payload_overhead();
          effective_record_size = adjust_tls_record_size_for_payload_overhead(current_record_size_, payload_overhead);
        }
        if (inner_->supports_tls_record_sizing()) {
          inner_->set_max_tls_record_size(effective_record_size);
        }
        batch.emplace(current_record_size_, inner_->max_prepend_size(), inner_->max_append_size(), hint);
        batch_ptr = &batch.value();
        batch_ptr->push(std::move(pending_write));
        if (!has_manual_record_size_override_) {
          batch_payload_overhead = payload_overhead;
        }
        return true;
      }
      if (!batch_ptr->can_append(pending_write)) {
        return false;
      }
      batch_ptr->append(std::move(pending_write));
      return true;
    });

    if (!batch.has_value()) {
      return false;
    }

    size_t written_bytes = 0;
    if (batch->can_coalesce()) {
      inner_->set_traffic_hint(batch->items().front().hint);
      auto merged = batch->take_coalesced_message();
      written_bytes = merged.size();
      inner_->write(std::move(merged), false);
    } else {
      for (auto &item : batch->items()) {
        inner_->set_traffic_hint(item.hint);
        written_bytes += item.message.size();
        inner_->write(std::move(item.message), item.quick_ack);
      }
    }

    if (!has_manual_record_size_override_) {
      drs_.notify_bytes_written(account_transport_payload_overhead(written_bytes, batch_payload_overhead));
      has_drs_activity_ = true;
      last_drs_activity_at_ = now;
    }
    return true;
  };

  while (inner_->can_write() && !bypass_ring_.empty() && !ring_.empty() && ring_.earliest_deadline() <= now) {
    auto *first_ring = favor_shaped_first_on_contention_ ? &ring_ : &bypass_ring_;
    auto *second_ring = favor_shaped_first_on_contention_ ? &bypass_ring_ : &ring_;
    bool drained_any = false;
    drained_any = write_batch(*first_ring) || drained_any;
    if (!inner_->can_write()) {
      if (drained_any) {
        favor_shaped_first_on_contention_ = !favor_shaped_first_on_contention_;
      }
      break;
    }
    drained_any = write_batch(*second_ring) || drained_any;
    if (drained_any) {
      favor_shaped_first_on_contention_ = !favor_shaped_first_on_contention_;
    }
    if (!drained_any) {
      break;
    }
  }

  while (write_batch(bypass_ring_)) {
  }
  while (write_batch(ring_)) {
  }

  inner_->pre_flush_write(now);

  if (backpressure_latched_ && queued_write_count() <= low_watermark_) {
    backpressure_latched_ = false;
  }
}

double StealthTransportDecorator::get_shaping_wakeup() const {
  auto inner_wakeup = inner_->get_shaping_wakeup();

  bool has_wakeup = false;
  double wakeup = 0.0;
  if (!bypass_ring_.empty()) {
    wakeup = bypass_ring_.earliest_deadline();
    has_wakeup = true;
  }
  if (!ring_.empty()) {
    auto ring_wakeup = ring_.earliest_deadline();
    if (!has_wakeup || ring_wakeup < wakeup) {
      wakeup = ring_wakeup;
      has_wakeup = true;
    }
  }
  if (inner_wakeup != 0.0) {
    if (!has_wakeup || inner_wakeup < wakeup) {
      wakeup = inner_wakeup;
      has_wakeup = true;
    }
  }
  return has_wakeup ? wakeup : 0.0;
}

void StealthTransportDecorator::set_traffic_hint(TrafficHint hint) {
  pending_hint_ = hint;
}

void StealthTransportDecorator::set_max_tls_record_size(int32 size) {
  has_manual_record_size_override_ = true;
  current_record_size_ = clamp_tls_record_size(size);
  if (inner_->supports_tls_record_sizing()) {
    inner_->set_max_tls_record_size(current_record_size_);
  }
}

bool StealthTransportDecorator::supports_tls_record_sizing() const {
  return inner_->supports_tls_record_sizing();
}

size_t StealthTransportDecorator::traffic_bulk_threshold_bytes() const {
  return config_.bulk_threshold_bytes;
}

size_t StealthTransportDecorator::queued_write_count() const {
  return bypass_ring_.size() + ring_.size();
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td