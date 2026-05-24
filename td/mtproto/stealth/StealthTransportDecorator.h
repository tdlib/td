// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#pragma once

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/stealth/ChaffScheduler.h"
#include "td/mtproto/stealth/DrsEngine.h"
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

  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack) override;
  Result<size_t> read_next(BufferSlice *message, uint32 *quick_ack, int32 *error_code) override;
  bool support_quick_ack() const override;
  void write(BufferWriter &&message, bool quick_ack) override;
  bool can_read() const override;
  bool can_write() const override;
  void init(ChainBufferReader *input, ChainBufferWriter *output) override;
  size_t max_prepend_size() const override;
  size_t max_append_size() const override;
  TransportType get_type() const override;
  bool use_random_padding() const override;
  void configure_packet_info(PacketInfo *packet_info) const override;
  void pre_flush_write(double now) override;
  double get_shaping_wakeup() const override;
  void set_traffic_hint(TrafficHint hint) override;
  void set_max_tls_record_size(int32 size) override;
  void set_stealth_record_padding_target(int32 target_bytes) override;
  bool supports_tls_record_sizing() const override;
  size_t traffic_bulk_threshold_bytes() const override;

 private:
  StealthTransportDecorator(unique_ptr<IStreamTransport> inner, StealthConfig config, unique_ptr<IRng> rng,
                            unique_ptr<IClock> clock);

  unique_ptr<IStreamTransport> inner_;
  StealthConfig config_;
  unique_ptr<IRng> rng_;
  unique_ptr<IClock> clock_;
  IptController ipt_controller_;
  ChaffScheduler chaff_scheduler_;
  ShaperRingBuffer bypass_ring_;
  ShaperRingBuffer ring_;
  size_t high_watermark_{0};
  size_t low_watermark_{0};
  bool backpressure_latched_{false};
  uint64 overflow_invariant_hits_{0};
  DrsEngine drs_;
  int32 initial_record_size_{0};
  int32 current_record_size_{0};
  TrafficHint pending_hint_{TrafficHint::Unknown};
  bool favor_shaped_first_on_contention_{false};
  bool has_manual_record_size_override_{false};
  bool has_drs_activity_{false};
  double last_drs_activity_at_{0.0};
  uint8 greeting_records_sent_{0};
  int32 last_greeting_record_size_{0};
  int32 pending_response_floor_bytes_{0};
  uint64 pending_post_response_jitter_us_{0};
  vector<uint8> small_record_window_flags_;
  size_t small_record_window_size_{0};
  size_t small_record_window_samples_{0};
  size_t small_record_window_index_{0};
  size_t small_record_count_{0};

  int32 apply_small_record_budget(int32 target_bytes) const;
  int32 apply_bidirectional_response_floor(TrafficHint hint, int32 target_bytes);
  bool is_greeting_phase_active() const;
  void note_greeting_record_emitted();
  void note_inbound_response(size_t bytes);
  void note_record_target(int32 target_bytes);
  void clear_stale_queued_response_jitter();
  size_t queued_write_count() const;
};

}  // namespace stealth
}  // namespace mtproto
}  // namespace td