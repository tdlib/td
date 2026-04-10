// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

DrsPhaseModel make_fixed_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 16;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_bidirectional_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_fixed_phase(320);
  config.drs_policy.congestion_open = make_fixed_phase(320);
  config.drs_policy.steady_state = make_fixed_phase(320);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 320;
  config.drs_policy.max_payload_cap = 320;
  config.bidirectional_correlation_policy.enabled = true;
  config.bidirectional_correlation_policy.small_response_threshold_bytes = 192;
  config.bidirectional_correlation_policy.next_request_min_payload_cap = 1200;
  return config;
}

td::unique_ptr<StealthTransportDecorator> make_transport(RecordingTransport **inner_out, MockClock **clock_out) {
  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator = StealthTransportDecorator::create(std::move(inner), make_bidirectional_config(),
                                                     td::make_unique<MockRng>(17), std::move(clock));
  CHECK(decorator.is_ok());
  *inner_out = inner_ptr;
  *clock_out = clock_ptr;
  return decorator.move_as_ok();
}

void perform_read(StealthTransportDecorator &transport, RecordingTransport &inner, size_t bytes) {
  inner.next_read_message = td::BufferSlice(td::Slice(td::string(bytes, 'r')));
  td::BufferSlice message;
  td::uint32 quick_ack = 0;
  auto result = transport.read_next(&message, &quick_ack);
  ASSERT_TRUE(result.is_ok());
  ASSERT_EQ(bytes, result.ok());
}

void queue_and_flush_interactive_write(StealthTransportDecorator &transport, MockClock &clock) {
  transport.set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter writer(td::Slice("client-request"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);
  auto wakeup = transport.get_shaping_wakeup();
  if (wakeup > clock.now()) {
    clock.advance(wakeup - clock.now());
  }
  transport.pre_flush_write(clock.now());
}

TEST(BidirectionalSizeCorrelation, SmallInboundResponseRaisesNextInteractivePaddingFloor) {
  RecordingTransport *inner = nullptr;
  MockClock *clock = nullptr;
  auto transport = make_transport(&inner, &clock);

  perform_read(*transport, *inner, 88);
  queue_and_flush_interactive_write(*transport, *clock);

  ASSERT_FALSE(inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(1200, inner->stealth_record_padding_targets.back());
  ASSERT_FALSE(inner->max_tls_record_sizes.empty());
  ASSERT_EQ(1200, inner->max_tls_record_sizes.back());
}

TEST(BidirectionalSizeCorrelation, LargeInboundResponseDoesNotOverrideCaptureDrivenCap) {
  RecordingTransport *inner = nullptr;
  MockClock *clock = nullptr;
  auto transport = make_transport(&inner, &clock);

  perform_read(*transport, *inner, 2048);
  queue_and_flush_interactive_write(*transport, *clock);

  ASSERT_FALSE(inner->stealth_record_padding_targets.empty());
  ASSERT_EQ(320, inner->stealth_record_padding_targets.back());
  ASSERT_FALSE(inner->max_tls_record_sizes.empty());
  ASSERT_EQ(320, inner->max_tls_record_sizes.back());
}

}  // namespace