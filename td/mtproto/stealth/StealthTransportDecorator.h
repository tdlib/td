//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/IptController.h"
#include "td/mtproto/stealth/ShaperRingBuffer.h"
#include "td/mtproto/stealth/StealthConfig.h"

namespace td {
namespace mtproto {
namespace stealth {

class StealthTransportDecorator final : public IStreamTransport {
 public:
  static Result<unique_ptr<StealthTransportDecorator>> create(unique_ptr<IStreamTransport> inner, StealthConfig config,
                                                              unique_ptr<IRng> rng, unique_ptr<IClock> clock);
  ~StealthTransportDecorator() override = default;

  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) final;
  bool support_quick_ack() const final;
  void write(BufferWriter &&message, bool quick_ack) final;
  bool can_read() const final;
  bool can_write() const final;
  void init(ChainBufferReader *input, ChainBufferWriter *output) final;
  size_t max_prepend_size() const final;
  size_t max_append_size() const final;
  TransportType get_type() const final;
  bool use_random_padding() const final;
  void pre_flush_write(double now) final;
  double get_shaping_wakeup() const final;
  void set_traffic_hint(TrafficHint hint) final;
  void set_max_tls_record_size(int32 size) final;
  bool supports_tls_record_sizing() const final;

 private:
  StealthTransportDecorator(unique_ptr<IStreamTransport> inner, StealthConfig config, unique_ptr<IRng> rng,
                            unique_ptr<IClock> clock);

  unique_ptr<IStreamTransport> inner_;
  StealthConfig config_;
  unique_ptr<IRng> rng_;
  unique_ptr<IClock> clock_;
  IptController ipt_controller_;
  ShaperRingBuffer bypass_ring_;
  ShaperRingBuffer ring_;
  size_t high_watermark_{0};
  size_t low_watermark_{0};
  bool backpressure_latched_{false};
  uint64 overflow_invariant_hits_{0};
  int32 initial_record_size_{0};
  int32 current_record_size_{0};
  TrafficHint pending_hint_{TrafficHint::Unknown};
  bool favor_shaped_first_on_contention_{false};

  size_t queued_write_count() const;
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td