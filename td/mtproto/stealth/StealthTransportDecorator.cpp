// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace td {
namespace mtproto {
namespace stealth {

namespace stealth_transport_decorator_internal {

constexpr int32 kMinTlsRecordSize = 256;
constexpr int32 kMaxTlsRecordSize = 16384;
constexpr int32 kMaxGreetingRecordSize = 1500;

struct TransportPayloadOverhead final {
  int32 bytes{0};
};

struct BatchLayout final {
  int32 payload_cap_bytes{0};
  size_t prepend_bytes{0};
  size_t append_bytes{0};
};

int32 clamp_tls_record_size(int32 size) {
  return std::max(kMinTlsRecordSize, std::min(size, kMaxTlsRecordSize));
}

uint64 milliseconds_to_microseconds_saturating(double delay_ms) {
  if (!std::isfinite(delay_ms) || delay_ms <= 0.0) {
    return 0;
  }

  auto micros = static_cast<long double>(delay_ms) * static_cast<long double>(1000.0);
  auto max_micros = static_cast<long double>(std::numeric_limits<uint64>::max());
  if (!(micros < max_micros)) {
    return std::numeric_limits<uint64>::max();
  }
  return static_cast<uint64>(micros);
}

uint64 sample_delay_us(double min_delay_ms, double max_delay_ms, IRng &rng) {
  if (!(max_delay_ms > 0.0)) {
    return 0;
  }
  auto min_delay_us = milliseconds_to_microseconds_saturating(min_delay_ms);
  auto max_delay_us = milliseconds_to_microseconds_saturating(max_delay_ms);
  if (max_delay_ms <= min_delay_ms) {
    return min_delay_us;
  }
  if (max_delay_us <= min_delay_us) {
    return min_delay_us;
  }

  auto span = max_delay_us - min_delay_us;
  if (span >= static_cast<uint64>(std::numeric_limits<uint32>::max())) {
    return min_delay_us + static_cast<uint64>(rng.secure_uint32());
  }
  auto width = static_cast<uint32>(span + 1);
  return min_delay_us + static_cast<uint64>(rng.bounded(width));
}

uint64 saturating_add(uint64 left, uint64 right) {
  auto max_value = std::numeric_limits<uint64>::max();
  if (left > max_value - right) {
    return max_value;
  }
  return left + right;
}

bool is_finite_time(double value) {
  return std::isfinite(value);
}

double sanitize_time_or_zero(double value) {
  return is_finite_time(value) ? value : 0.0;
}

int32 adjust_tls_record_size_for_payload_overhead(int32 payload_cap_bytes, TransportPayloadOverhead payload_overhead) {
  auto safe_overhead = std::max<int64>(0, payload_overhead.bytes);
  auto adjusted = static_cast<int64>(payload_cap_bytes) + safe_overhead;
  return static_cast<int32>(std::max<int64>(kMinTlsRecordSize, std::min<int64>(adjusted, kMaxTlsRecordSize)));
}

size_t required_record_padding_append_reserve(int32 target_bytes) {
  auto clamped_target = clamp_tls_record_size(target_bytes);
  // clamp_tls_record_size guarantees result >= kMinTlsRecordSize (256), so always > 4.
  return static_cast<size_t>(clamped_target - 4);
}

BufferWriter ensure_write_capacity(BufferWriter &&message, size_t prepend_bytes, size_t append_bytes) {
  if (message.prepare_prepend().size() >= prepend_bytes && message.prepare_append().size() >= append_bytes) {
    return std::move(message);
  }
  return BufferWriter(message.as_slice(), prepend_bytes, append_bytes);
}

size_t max_small_records_in_window(const StealthConfig &config) {
  auto raw_budget = std::floor(config.record_padding_policy.small_record_max_fraction *
                               static_cast<double>(config.record_padding_policy.small_record_window_size));
  return static_cast<size_t>(std::max<double>(0.0, raw_budget));
}

TrafficHint normalize_drs_hint(TrafficHint hint) {
  return hint == TrafficHint::Unknown ? TrafficHint::Interactive : hint;
}

size_t account_transport_payload_overhead(size_t written_bytes, TransportPayloadOverhead payload_overhead) {
  if (payload_overhead.bytes <= 0) {
    return written_bytes;
  }
  auto safe_overhead = static_cast<size_t>(payload_overhead.bytes);
  auto max_size = std::numeric_limits<size_t>::max();
  if (written_bytes > max_size - safe_overhead) {
    return max_size;
  }
  return written_bytes + safe_overhead;
}

class BatchBuilder final {
 public:
  BatchBuilder(BatchLayout layout, TrafficHint hint)
      : cap_(layout.payload_cap_bytes), prepend_(layout.prepend_bytes), append_(layout.append_bytes), hint_(hint) {
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

}  // namespace stealth_transport_decorator_internal

Result<unique_ptr<StealthTransportDecorator>> StealthTransportDecorator::create(unique_ptr<IStreamTransport> inner,
                                                                                StealthConfig config,
                                                                                unique_ptr<IRng> rng,
                                                                                unique_ptr<IClock> clock) {
  if (inner == nullptr) {
    return Status::Error("StealthTransportDecorator::create requires non-null inner transport");
  }
  if (rng == nullptr) {
    return Status::Error("StealthTransportDecorator::create requires non-null rng");
  }
  if (clock == nullptr) {
    return Status::Error("StealthTransportDecorator::create requires non-null clock");
  }
  auto config_status = config.validate();
  if (config_status.is_error()) {
    return Status::Error(config_status.code(), config_status.public_message());
  }
  if (config.greeting_camouflage_policy.greeting_record_count != 0 && !inner->supports_tls_record_sizing()) {
    std::string message =
        "StealthTransportDecorator::create: greeting camouflage requires TLS record sizing support "
        "[greeting_record_count=";
    message += std::to_string(config.greeting_camouflage_policy.greeting_record_count);
    message += "] [supports_tls_record_sizing=false]";
    return Status::Error(message);
  }

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
    , chaff_scheduler_(config_, ipt_controller_, *rng_, clock_->now())
    , bypass_ring_(config_.ring_capacity)
    , ring_(config_.ring_capacity)
    , high_watermark_(config_.high_watermark)
    , low_watermark_(config_.low_watermark)
    , drs_(config_.drs_policy, *rng_)
    , small_record_window_flags_(static_cast<size_t>(config_.record_padding_policy.small_record_window_size), 0)
    , small_record_window_size_(small_record_window_flags_.size()) {
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

int32 StealthTransportDecorator::apply_small_record_budget(int32 target_bytes) const {
  auto threshold = config_.record_padding_policy.small_record_threshold;
  if (target_bytes >= threshold || small_record_window_size_ == 0) {
    return target_bytes;
  }
  if (small_record_count_ < stealth_transport_decorator_internal::max_small_records_in_window(config_)) {
    return target_bytes;
  }
  return threshold;
}

int32 StealthTransportDecorator::apply_bidirectional_response_floor(TrafficHint hint, int32 target_bytes) {
  if (hint != TrafficHint::Interactive || pending_response_floor_bytes_ <= 0) {
    return target_bytes;
  }
  auto adjusted = std::max(target_bytes, pending_response_floor_bytes_);
  pending_response_floor_bytes_ = 0;
  return adjusted;
}

void consume_bidirectional_response_floor_on_greeting(TrafficHint hint, int32 &pending_response_floor_bytes) {
  if (stealth_transport_decorator_internal::normalize_drs_hint(hint) == TrafficHint::Interactive) {
    pending_response_floor_bytes = 0;
  }
}

void StealthTransportDecorator::note_record_target(int32 target_bytes) {
  if (small_record_window_size_ == 0) {
    return;
  }
  auto threshold = config_.record_padding_policy.small_record_threshold;
  uint8 is_small_record = target_bytes < threshold ? 1 : 0;
  if (small_record_window_samples_ < small_record_window_size_) {
    small_record_window_flags_[small_record_window_samples_] = is_small_record;
    small_record_window_samples_++;
  } else {
    small_record_count_ -= small_record_window_flags_[small_record_window_index_];
    small_record_window_flags_[small_record_window_index_] = is_small_record;
    small_record_window_index_ = (small_record_window_index_ + 1) % small_record_window_size_;
  }
  small_record_count_ += is_small_record;
}

bool StealthTransportDecorator::is_greeting_phase_active() const {
  return !has_manual_record_size_override_ &&
         greeting_records_sent_ < config_.greeting_camouflage_policy.greeting_record_count;
}

void StealthTransportDecorator::note_greeting_record_emitted() {
  last_greeting_record_size_ = current_record_size_;
  greeting_records_sent_++;
  if (greeting_records_sent_ == config_.greeting_camouflage_policy.greeting_record_count) {
    drs_.prime_with_payload_cap(last_greeting_record_size_);
  }
}

void StealthTransportDecorator::note_inbound_response(size_t bytes) {
  if (!config_.bidirectional_correlation_policy.enabled) {
    pending_response_floor_bytes_ = 0;
    pending_post_response_jitter_us_ = 0;
    clear_stale_queued_response_jitter();
    return;
  }

  if (bytes <= static_cast<size_t>(config_.bidirectional_correlation_policy.small_response_threshold_bytes)) {
    pending_response_floor_bytes_ = config_.bidirectional_correlation_policy.next_request_min_payload_cap;
    pending_post_response_jitter_us_ = stealth_transport_decorator_internal::sample_delay_us(
        config_.bidirectional_correlation_policy.post_response_delay_jitter_ms_min,
        config_.bidirectional_correlation_policy.post_response_delay_jitter_ms_max, *rng_);
    return;
  }

  pending_response_floor_bytes_ = 0;
  pending_post_response_jitter_us_ = 0;
  clear_stale_queued_response_jitter();
}

void StealthTransportDecorator::clear_stale_queued_response_jitter() {
  auto now = stealth_transport_decorator_internal::sanitize_time_or_zero(clock_->now());
  auto clear_pending_jitter = [now](ShaperPendingWrite &pending_write) {
    if (pending_write.response_jitter_delay_us == 0) {
      return;
    }
    auto jitter_seconds = static_cast<double>(pending_write.response_jitter_delay_us) / 1e6;
    auto sanitized_send_at = stealth_transport_decorator_internal::sanitize_time_or_zero(pending_write.send_at);
    auto adjusted_send_at = sanitized_send_at - jitter_seconds;
    if (!stealth_transport_decorator_internal::is_finite_time(adjusted_send_at)) {
      adjusted_send_at = now;
    }
    pending_write.send_at = adjusted_send_at < now ? now : adjusted_send_at;
    pending_write.response_jitter_delay_us = 0;
  };
  bypass_ring_.for_each(clear_pending_jitter);
  ring_.for_each(clear_pending_jitter);
}

Result<size_t> StealthTransportDecorator::read_next(BufferSlice *message, uint32 *quick_ack) {
  return read_next(message, quick_ack, nullptr);
}

Result<size_t> StealthTransportDecorator::read_next(BufferSlice *message, uint32 *quick_ack, int32 *error_code) {
  if (overflow_failed_) {
    if (error_code != nullptr) {
      *error_code = 0;
    }
    return Status::Error("stealth ring overflow fail-closed");
  }
  auto result = inner_->read_next(message, quick_ack, error_code);
  if (result.is_ok()) {
    if (message != nullptr && !message->empty()) {
      note_inbound_response(message->size());
    }
    chaff_scheduler_.note_activity(clock_->now());
  }
  return result;
}

bool StealthTransportDecorator::support_quick_ack() const {
  return inner_->support_quick_ack();
}

void StealthTransportDecorator::write(BufferWriter &&message, bool quick_ack) {
  if (overflow_failed_) {
    return;
  }
  auto hint = pending_hint_;
  pending_hint_ = TrafficHint::Unknown;
  auto effective_hint = stealth_transport_decorator_internal::normalize_drs_hint(hint);

  auto has_pending_data = queued_write_count() != 0;
  auto delay_us = ipt_controller_.next_delay_us(has_pending_data, hint);
  uint64 response_jitter_delay_us = 0;
  if (effective_hint == TrafficHint::Interactive && pending_post_response_jitter_us_ != 0) {
    response_jitter_delay_us = pending_post_response_jitter_us_;
    delay_us = stealth_transport_decorator_internal::saturating_add(delay_us, response_jitter_delay_us);
    pending_post_response_jitter_us_ = 0;
  }
  auto now = stealth_transport_decorator_internal::sanitize_time_or_zero(clock_->now());
  auto send_at = now + static_cast<double>(delay_us) / 1e6;
  if (!stealth_transport_decorator_internal::is_finite_time(send_at)) {
    send_at = now;
  }
  ShaperPendingWrite pending_write{std::move(message), quick_ack, send_at, hint, response_jitter_delay_us};
  if (queued_write_count() >= config_.ring_capacity) {
    overflow_invariant_hits_++;
    fail_closed_on_ring_overflow();
    return;
  }

  auto &target_ring = delay_us == 0 ? bypass_ring_ : ring_;
  if (!target_ring.try_enqueue(std::move(pending_write))) {
    overflow_invariant_hits_++;
    fail_closed_on_ring_overflow();
    return;
  }

  if (queued_write_count() >= high_watermark_) {
    backpressure_latched_ = true;
  }
}

bool StealthTransportDecorator::can_read() const {
  return overflow_failed_ || inner_->can_read();
}

bool StealthTransportDecorator::can_write() const {
  return !overflow_failed_ && inner_->can_write() && !backpressure_latched_;
}

void StealthTransportDecorator::init(ChainBufferReader *input, ChainBufferWriter *output) {
  inner_->init(input, output);
}

size_t StealthTransportDecorator::max_prepend_size() const {
  return inner_->max_prepend_size();
}

size_t StealthTransportDecorator::max_append_size() const {
  auto append_size = inner_->max_append_size();
  if (!inner_->supports_tls_record_sizing() || !has_manual_record_size_override_) {
    return append_size;
  }
  return std::max(append_size,
                  stealth_transport_decorator_internal::required_record_padding_append_reserve(current_record_size_));
}

TransportType StealthTransportDecorator::get_type() const {
  return inner_->get_type();
}

bool StealthTransportDecorator::use_random_padding() const {
  return config_.crypto_padding_policy.enabled || inner_->use_random_padding();
}

void StealthTransportDecorator::configure_packet_info(PacketInfo *packet_info) const {
  CHECK(packet_info != nullptr);
  inner_->configure_packet_info(packet_info);
  if (!config_.crypto_padding_policy.enabled) {
    return;
  }
  packet_info->use_random_padding = true;
  packet_info->use_stealth_padding = true;
  packet_info->stealth_padding_min_bytes = config_.crypto_padding_policy.min_padding_bytes;
  packet_info->stealth_padding_max_bytes = config_.crypto_padding_policy.max_padding_bytes;
}

void StealthTransportDecorator::pre_flush_write(double now) {
  if (overflow_failed_) {
    return;
  }
  if (!stealth_transport_decorator_internal::is_finite_time(now)) {
    inner_->pre_flush_write(0.0);
    return;
  }

  if (!has_manual_record_size_override_ && has_drs_activity_ && queued_write_count() != 0 &&
      drs_.should_reset_after_idle(now - last_drs_activity_at_)) {
    drs_.notify_idle();
  }

  auto write_batch = [this, now](ShaperRingBuffer &ring) {
    if (!inner_->can_write() || ring.empty() || ring.earliest_deadline() > now) {
      return false;
    }

    stealth_transport_decorator_internal::BatchBuilder *batch_ptr = nullptr;
    std::optional<stealth_transport_decorator_internal::BatchBuilder> batch;
    stealth_transport_decorator_internal::TransportPayloadOverhead batch_payload_overhead;
    bool batch_uses_greeting = false;
    size_t required_append_reserve = inner_->max_append_size();
    size_t required_prepend_reserve = inner_->max_prepend_size();
    ring.drain_ready(now, [&](ShaperPendingWrite &pending_write) {
      auto hint = stealth_transport_decorator_internal::normalize_drs_hint(pending_write.hint);
      if (!batch.has_value()) {
        int32 payload_cap = current_record_size_;
        int32 effective_record_size = current_record_size_;
        stealth_transport_decorator_internal::TransportPayloadOverhead payload_overhead;
        if (is_greeting_phase_active()) {
          payload_cap = config_.sample_greeting_record_size(greeting_records_sent_, *rng_);
          // Greeting camouflage is a first-flight template and must remain within
          // the sampled browser-like bin instead of being normalized by the
          // generic small-record budget used for later DRS/manual traffic.
          current_record_size_ =
              std::min(payload_cap, stealth_transport_decorator_internal::kMaxGreetingRecordSize);
          payload_overhead.bytes = inner_->tls_record_sizing_payload_overhead();
          effective_record_size = stealth_transport_decorator_internal::adjust_tls_record_size_for_payload_overhead(
              current_record_size_, payload_overhead);
          batch_uses_greeting = true;
        } else if (!has_manual_record_size_override_) {
          payload_cap = drs_.next_payload_cap(hint);
          current_record_size_ = apply_small_record_budget(stealth_transport_decorator_internal::clamp_tls_record_size(
              apply_bidirectional_response_floor(hint, payload_cap)));
          payload_overhead.bytes = inner_->tls_record_sizing_payload_overhead();
          effective_record_size = stealth_transport_decorator_internal::adjust_tls_record_size_for_payload_overhead(
              current_record_size_, payload_overhead);
        } else {
          current_record_size_ = apply_small_record_budget(current_record_size_);
          effective_record_size = current_record_size_;
        }
        if (inner_->supports_tls_record_sizing()) {
          inner_->set_max_tls_record_size(effective_record_size);
          inner_->set_stealth_record_padding_target(current_record_size_);
          required_append_reserve = std::max(
              required_append_reserve,
              stealth_transport_decorator_internal::required_record_padding_append_reserve(current_record_size_));
        }
        batch.emplace(stealth_transport_decorator_internal::BatchLayout{current_record_size_, required_prepend_reserve,
                                                                        required_append_reserve},
                      hint);
        batch_ptr = &batch.value();
        batch_ptr->push(std::move(pending_write));
        if (!has_manual_record_size_override_ && !batch_uses_greeting) {
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

    // After drain_ready with a non-empty ring whose earliest deadline <= now,
    // the callback always runs at least once, so batch is always populated.
    CHECK(batch.has_value());

    auto resolve_coalesced_batch_hint = [&](const vector<ShaperPendingWrite> &items) {
      CHECK(!items.empty());
      auto resolved_hint = items.front().hint;
      if (resolved_hint != TrafficHint::Unknown) {
        return resolved_hint;
      }
      for (const auto &item : items) {
        if (item.hint != TrafficHint::Unknown) {
          return item.hint;
        }
      }
      return resolved_hint;
    };

    const auto normalized_batch_hint =
        stealth_transport_decorator_internal::normalize_drs_hint(batch->items().front().hint);
    const bool should_update_drs =
        normalized_batch_hint != TrafficHint::Keepalive && normalized_batch_hint != TrafficHint::AuthHandshake;

    size_t written_bytes = 0;
    if (batch->can_coalesce()) {
      inner_->set_traffic_hint(resolve_coalesced_batch_hint(batch->items()));
      auto merged = batch->take_coalesced_message();
      written_bytes = merged.size();
      inner_->write(std::move(merged), false);
      note_record_target(current_record_size_);
      if (batch_uses_greeting) {
        consume_bidirectional_response_floor_on_greeting(batch->items().front().hint, pending_response_floor_bytes_);
        note_greeting_record_emitted();
      }
    } else {
      for (auto &item : batch->items()) {
        inner_->set_traffic_hint(item.hint);
        if (inner_->supports_tls_record_sizing()) {
          inner_->set_stealth_record_padding_target(current_record_size_);
        }
        item.message = stealth_transport_decorator_internal::ensure_write_capacity(
            std::move(item.message), required_prepend_reserve, required_append_reserve);
        written_bytes += item.message.size();
        inner_->write(std::move(item.message), item.quick_ack);
        note_record_target(current_record_size_);
        if (batch_uses_greeting) {
          consume_bidirectional_response_floor_on_greeting(item.hint, pending_response_floor_bytes_);
          note_greeting_record_emitted();
        }
      }
    }

    if (!has_manual_record_size_override_ && !batch_uses_greeting && should_update_drs) {
      drs_.notify_bytes_written(stealth_transport_decorator_internal::account_transport_payload_overhead(
          written_bytes, batch_payload_overhead));
      has_drs_activity_ = true;
      last_drs_activity_at_ = now;
    }
    chaff_scheduler_.note_activity(now);
    return true;
  };

  auto write_idle_chaff = [this, now]() {
    auto sampled_target_bytes = chaff_scheduler_.current_target_bytes();
    if (sampled_target_bytes <= 0) {
      return false;
    }

    auto target_bytes =
        apply_small_record_budget(stealth_transport_decorator_internal::clamp_tls_record_size(sampled_target_bytes));
    auto budget_target_bytes = static_cast<size_t>(target_bytes);
    stealth_transport_decorator_internal::TransportPayloadOverhead payload_overhead;
    if (inner_->supports_tls_record_sizing()) {
      payload_overhead.bytes = inner_->tls_record_sizing_payload_overhead();
      budget_target_bytes = stealth_transport_decorator_internal::account_transport_payload_overhead(
          budget_target_bytes, payload_overhead);
    }

    if (!chaff_scheduler_.should_emit_for_target(now, queued_write_count() != 0, inner_->can_write(),
                                                 budget_target_bytes)) {
      return false;
    }

    auto effective_record_size = target_bytes;
    if (inner_->supports_tls_record_sizing()) {
      effective_record_size = stealth_transport_decorator_internal::adjust_tls_record_size_for_payload_overhead(
          target_bytes, payload_overhead);
      inner_->set_max_tls_record_size(effective_record_size);
      inner_->set_stealth_record_padding_target(target_bytes);
    }

    auto append_reserve = inner_->max_append_size();
    if (inner_->supports_tls_record_sizing()) {
      append_reserve = std::max(
          append_reserve, stealth_transport_decorator_internal::required_record_padding_append_reserve(target_bytes));
    }
    inner_->set_traffic_hint(TrafficHint::Keepalive);
    BufferWriter chaff(Slice(), inner_->max_prepend_size(), append_reserve);
    inner_->write(std::move(chaff), false);
    note_record_target(target_bytes);
    chaff_scheduler_.note_chaff_emitted(now, budget_target_bytes);
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

  static_cast<void>(write_idle_chaff());

  inner_->pre_flush_write(now);

  if (backpressure_latched_ && queued_write_count() <= low_watermark_) {
    backpressure_latched_ = false;
  }
}

double StealthTransportDecorator::get_shaping_wakeup() const {
  if (overflow_failed_) {
    return 0.0;
  }
  auto inner_wakeup = inner_->get_shaping_wakeup();

  bool has_wakeup = false;
  double wakeup = 0.0;
  if (!bypass_ring_.empty()) {
    auto bypass_wakeup = bypass_ring_.earliest_deadline();
    if (stealth_transport_decorator_internal::is_finite_time(bypass_wakeup) || bypass_wakeup == 0.0) {
      wakeup = bypass_wakeup;
      has_wakeup = true;
    }
  }
  if (!ring_.empty()) {
    auto ring_wakeup = ring_.earliest_deadline();
    if (stealth_transport_decorator_internal::is_finite_time(ring_wakeup) || ring_wakeup == 0.0) {
      if (!has_wakeup || ring_wakeup < wakeup) {
        wakeup = ring_wakeup;
        has_wakeup = true;
      }
    }
  }
  if (stealth_transport_decorator_internal::is_finite_time(inner_wakeup) && inner_wakeup > 0.0) {
    if (!has_wakeup || inner_wakeup < wakeup) {
      wakeup = inner_wakeup;
      has_wakeup = true;
    }
  }
  double chaff_wakeup = 0.0;
  auto chaff_target_bytes = chaff_scheduler_.current_target_bytes();
  if (chaff_target_bytes > 0) {
    auto effective_chaff_target =
        apply_small_record_budget(stealth_transport_decorator_internal::clamp_tls_record_size(chaff_target_bytes));
    auto budget_target_bytes = static_cast<size_t>(effective_chaff_target);
    if (inner_->supports_tls_record_sizing()) {
      stealth_transport_decorator_internal::TransportPayloadOverhead payload_overhead;
      payload_overhead.bytes = inner_->tls_record_sizing_payload_overhead();
      budget_target_bytes = stealth_transport_decorator_internal::account_transport_payload_overhead(
          budget_target_bytes, payload_overhead);
    }
    chaff_wakeup = chaff_scheduler_.get_wakeup_for_target(clock_->now(), queued_write_count() != 0, inner_->can_write(),
                                                          budget_target_bytes);
  }
  if (stealth_transport_decorator_internal::is_finite_time(chaff_wakeup) && chaff_wakeup > 0.0) {
    if (!has_wakeup || chaff_wakeup < wakeup) {
      wakeup = chaff_wakeup;
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
  current_record_size_ = apply_small_record_budget(stealth_transport_decorator_internal::clamp_tls_record_size(size));
  if (inner_->supports_tls_record_sizing()) {
    stealth_transport_decorator_internal::TransportPayloadOverhead payload_overhead;
    payload_overhead.bytes = inner_->tls_record_sizing_payload_overhead();
    auto effective_record_size = stealth_transport_decorator_internal::adjust_tls_record_size_for_payload_overhead(
        current_record_size_, payload_overhead);
    inner_->set_max_tls_record_size(effective_record_size);
    inner_->set_stealth_record_padding_target(current_record_size_);
  }
}

void StealthTransportDecorator::set_stealth_record_padding_target(int32 target_bytes) {
  has_manual_record_size_override_ = true;
  current_record_size_ =
      apply_small_record_budget(stealth_transport_decorator_internal::clamp_tls_record_size(target_bytes));
  if (inner_->supports_tls_record_sizing()) {
    stealth_transport_decorator_internal::TransportPayloadOverhead payload_overhead;
    payload_overhead.bytes = inner_->tls_record_sizing_payload_overhead();
    auto effective_record_size = stealth_transport_decorator_internal::adjust_tls_record_size_for_payload_overhead(
        current_record_size_, payload_overhead);
    inner_->set_max_tls_record_size(effective_record_size);
    inner_->set_stealth_record_padding_target(current_record_size_);
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

void StealthTransportDecorator::fail_closed_on_ring_overflow() {
  if (overflow_failed_) {
    return;
  }
  overflow_failed_ = true;
  backpressure_latched_ = true;
  bypass_ring_ = ShaperRingBuffer(config_.ring_capacity);
  ring_ = ShaperRingBuffer(config_.ring_capacity);
  pending_post_response_jitter_us_ = 0;
  pending_response_floor_bytes_ = 0;

  LOG(WARNING) << "Stealth ring overflow detected; fail-closing transport"
               << tag("ring_capacity", config_.ring_capacity)
               << tag("overflow_invariant_hits", overflow_invariant_hits_);
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td
