//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/StealthTransportDecorator.h"

namespace td {
namespace mtproto {
namespace stealth {

namespace {

constexpr int32 kMinTlsRecordSize = 256;
constexpr int32 kMaxTlsRecordSize = 16384;

int32 clamp_tls_record_size(int32 size) {
  return std::max(kMinTlsRecordSize, std::min(size, kMaxTlsRecordSize));
}

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
    , low_watermark_(config_.low_watermark) {
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
    LOG(FATAL) << "Stealth ring overflow invariant broken";
  }

  auto &target_ring = delay_us == 0 ? bypass_ring_ : ring_;
  if (!target_ring.try_enqueue(std::move(pending_write))) {
    overflow_invariant_hits_++;
    LOG(FATAL) << "Stealth ring overflow invariant broken";
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
  auto write_pending = [this](ShaperPendingWrite &pending_write) {
    if (!inner_->can_write()) {
      return false;
    }
    inner_->set_traffic_hint(pending_write.hint);
    if (inner_->supports_tls_record_sizing()) {
      inner_->set_max_tls_record_size(current_record_size_);
    }
    inner_->write(std::move(pending_write.message), pending_write.quick_ack);
    return true;
  };

  auto drain_one_ready = [&](ShaperRingBuffer &ring) {
    bool drained = false;
    ring.drain_ready(now, [&](ShaperPendingWrite &pending_write) {
      if (drained) {
        return false;
      }
      if (!write_pending(pending_write)) {
        return false;
      }
      drained = true;
      return true;
    });
    return drained;
  };

  while (inner_->can_write() && !bypass_ring_.empty() && !ring_.empty() && ring_.earliest_deadline() <= now) {
    auto *first_ring = favor_shaped_first_on_contention_ ? &ring_ : &bypass_ring_;
    auto *second_ring = favor_shaped_first_on_contention_ ? &bypass_ring_ : &ring_;
    bool drained_any = false;
    drained_any = drain_one_ready(*first_ring) || drained_any;
    if (!inner_->can_write()) {
      if (drained_any) {
        favor_shaped_first_on_contention_ = !favor_shaped_first_on_contention_;
      }
      break;
    }
    drained_any = drain_one_ready(*second_ring) || drained_any;
    if (drained_any) {
      favor_shaped_first_on_contention_ = !favor_shaped_first_on_contention_;
    }
    if (!drained_any) {
      break;
    }
  }

  bypass_ring_.drain_ready(now, write_pending);
  ring_.drain_ready(now, write_pending);

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
  current_record_size_ = clamp_tls_record_size(size);
  if (inner_->supports_tls_record_sizing()) {
    inner_->set_max_tls_record_size(current_record_size_);
  }
}

bool StealthTransportDecorator::supports_tls_record_sizing() const {
  return inner_->supports_tls_record_sizing();
}

size_t StealthTransportDecorator::queued_write_count() const {
  return bypass_ring_.size() + ring_.size();
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td