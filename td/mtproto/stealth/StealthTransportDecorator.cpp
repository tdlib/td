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

  auto delay_us = next_delay_us_stub(hint);
  auto send_at = clock_->now() + static_cast<double>(delay_us) / 1e6;
  ShaperPendingWrite pending_write{std::move(message), quick_ack, send_at, hint};
  if (!ring_.try_enqueue(std::move(pending_write))) {
    overflow_invariant_hits_++;
    LOG(FATAL) << "Stealth ring overflow invariant broken";
  }

  if (ring_.size() >= high_watermark_) {
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
  ring_.drain_ready(now, [this](ShaperPendingWrite &pending_write) {
    if (!inner_->can_write()) {
      return false;
    }
    inner_->set_traffic_hint(pending_write.hint);
    if (inner_->supports_tls_record_sizing()) {
      inner_->set_max_tls_record_size(current_record_size_);
    }
    inner_->write(std::move(pending_write.message), pending_write.quick_ack);
    return true;
  });

  if (backpressure_latched_ && ring_.size() <= low_watermark_) {
    backpressure_latched_ = false;
  }
}

double StealthTransportDecorator::get_shaping_wakeup() const {
  if (ring_.empty()) {
    return 0.0;
  }
  return ring_.earliest_deadline();
}

void StealthTransportDecorator::set_traffic_hint(TrafficHint hint) {
  pending_hint_ = hint;
}

void StealthTransportDecorator::set_max_tls_record_size(int32 size) {
  current_record_size_ = size;
  if (inner_->supports_tls_record_sizing()) {
    inner_->set_max_tls_record_size(size);
  }
}

bool StealthTransportDecorator::supports_tls_record_sizing() const {
  return inner_->supports_tls_record_sizing();
}

uint64 StealthTransportDecorator::next_delay_us_stub(TrafficHint hint) const {
  switch (hint) {
    case TrafficHint::Keepalive:
    case TrafficHint::BulkData:
    case TrafficHint::AuthHandshake:
    case TrafficHint::Interactive:
    case TrafficHint::Unknown:
    default:
      return 0;
  }
}

}  // namespace stealth
}  // namespace mtproto
}  // namespace td