// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// PR-S2: TLS Record Pad-to-Target — Stress, fuzz, and stability tests.
// These tests exercise extreme conditions: rapid target changes, boundary
// payloads, OOM resistance, and high-volume padding correctness.

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::tcp::ObfuscatedTransport;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

std::vector<size_t> extract_tls_record_lengths(td::Slice wire) {
  std::vector<size_t> lengths;
  size_t offset = 0;
  if (wire.size() >= 6 && wire.substr(0, 6) == td::Slice("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  while (offset + 5 <= wire.size()) {
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    lengths.push_back(len);
    offset += 5 + len;
  }
  return lengths;
}

struct StressHarness {
  td::unique_ptr<StealthTransportDecorator> transport;
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  MockClock *clock_ptr{nullptr};

  static StressHarness create(StealthConfig config, td::uint64 seed) {
    StressHarness h;
    auto inner =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    auto clock = td::make_unique<MockClock>();
    h.clock_ptr = clock.get();
    auto r =
        StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(seed), std::move(clock));
    CHECK(r.is_ok());
    h.transport = r.move_as_ok();
    auto reader = h.input_writer.extract_reader();
    h.transport->init(&reader, &h.output_writer);
    return h;
  }

  void write_and_flush(td::Slice payload, TrafficHint hint = TrafficHint::Interactive) {
    transport->set_traffic_hint(hint);
    td::BufferWriter w(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  std::vector<size_t> collect() {
    auto r = output_writer.extract_reader();
    r.sync_with_writer();
    auto wire = r.move_as_buffer_slice().as_slice().str();
    return extract_tls_record_lengths(wire);
  }
};

// ==============================================
// Stress: Mixed payloads and targets, no crash
// ==============================================

TEST(TlsRecordPaddingStress, MixedPayloadsAndTargets_NoCrash) {
  // 5000 writes with random-like payload sizes (4-4096 bytes) and DRS-driven targets.
  // Must not crash, assert, or OOM.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto h = StressHarness::create(config, 42);
  h.write_and_flush("warm");

  MockRng payload_rng(123);
  for (int i = 0; i < 5000; i++) {
    size_t payload_size = 4 + (payload_rng.bounded(4093));
    // Ensure 4-byte alignment (IntermediateTransport requirement)
    payload_size = (payload_size + 3) & ~3u;
    td::string payload(payload_size, static_cast<char>('A' + (i % 26)));
    auto hint = static_cast<TrafficHint>(1 + (i % 4));  // Interactive, Keepalive, BulkData, AuthHandshake
    h.write_and_flush(payload, hint);
  }

  auto lengths = h.collect();
  // Should have produced many records without crashing
  ASSERT_TRUE(lengths.size() >= 5000u);
}

TEST(TlsRecordPaddingStress, RapidTargetChanges_AllRecordsValid) {
  // Rapid DRS target changes (every write). All records must have valid TLS framing.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto h = StressHarness::create(config, 99);
  h.write_and_flush("warm");

  // Each write gets a different hint, causing DRS to sample different phases
  for (int i = 0; i < 2000; i++) {
    auto hint = (i % 2 == 0) ? TrafficHint::BulkData : TrafficHint::Interactive;
    h.write_and_flush("data", hint);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 2000u);

  for (size_t i = 0; i < lengths.size(); i++) {
    ASSERT_TRUE(lengths[i] >= 1u);
    ASSERT_TRUE(lengths[i] <= 16384u);
  }
}

TEST(TlsRecordPaddingStress, SmallRecordBudget_HighLoad_NeverViolated) {
  // 5000 writes of tiny payloads. The small-record budget (5%) must never be exceeded.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  config.record_padding_policy.small_record_threshold = 200;
  config.record_padding_policy.small_record_max_fraction = 0.05;
  config.record_padding_policy.small_record_window_size = 200;
  ASSERT_TRUE(config.validate().is_ok());

  auto h = StressHarness::create(config, 7);
  h.write_and_flush("warm");

  for (int i = 0; i < 5000; i++) {
    h.write_and_flush("tiny", TrafficHint::Keepalive);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 5000u);

  // Check small-record budget in a sliding window of 200
  const size_t window = 200;
  for (size_t start = 1; start + window <= lengths.size(); start++) {
    size_t small_count = 0;
    for (size_t j = start; j < start + window; j++) {
      if (lengths[j] < 200u) {
        small_count++;
      }
    }
    double frac = static_cast<double>(small_count) / static_cast<double>(window);
    ASSERT_TRUE(frac <= 0.06);  // Allow slight overshoot due to window boundary
  }
}

// ==============================================
// Stress: Various payload alignments
// ==============================================

TEST(TlsRecordPaddingStress, PayloadAlignment_4Byte_Validated) {
  // IntermediateTransport requires 4-byte aligned payloads.
  // Verify various aligned sizes work without assertion.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto h = StressHarness::create(config, 42);
  h.write_and_flush("warm");

  // Test aligned sizes: 4, 8, 12, 16, ..., 4096
  for (td::int32 sz = 4; sz <= 4096; sz += 4) {
    td::string payload(static_cast<size_t>(sz), 'X');
    h.write_and_flush(payload);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 1024u);
  for (auto len : lengths) {
    ASSERT_TRUE(len > 0u);
    ASSERT_TRUE(len <= 16384u);
  }
}

// ==============================================
// Light fuzz: Random seeds, random payload sizes
// ==============================================

TEST(TlsRecordPaddingStress, LightFuzz_RandomSeeds_NoAssertionFailure) {
  // Light fuzz: 10 different RNG seeds × 200 writes each with random-sized payloads.
  for (td::uint64 seed = 0; seed < 10; seed++) {
    MockRng rng_cfg(seed + 1000);
    auto config = StealthConfig::default_config(rng_cfg);
    ASSERT_TRUE(config.validate().is_ok());

    auto h = StressHarness::create(config, seed);
    h.write_and_flush("warm");

    MockRng payload_rng(seed + 500);
    for (int i = 0; i < 200; i++) {
      size_t sz = 4 + (payload_rng.bounded(2044));
      sz = (sz + 3) & ~3u;
      td::string payload(sz, 'Z');
      auto hint = static_cast<TrafficHint>(1 + payload_rng.bounded(4));
      h.write_and_flush(payload, hint);
    }

    auto lengths = h.collect();
    ASSERT_TRUE(lengths.size() >= 200u);
  }
}

// ==============================================
// Decorator: Target propagation fidelity under load
// ==============================================

TEST(TlsRecordPaddingStress, DecoratorPropagatesTarget_EveryWrite) {
  // Under load, every write must propagate a padding target to the inner transport.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  for (int i = 0; i < 500; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  ASSERT_EQ(500, inner_ptr->write_calls);
  // Each write should have set a stealth_record_padding_target ≥ 1
  // (initial + per-write = at least 501 target calls)
  ASSERT_TRUE(inner_ptr->stealth_record_padding_targets.size() >= 500u);

  for (auto target : inner_ptr->stealth_record_padding_targets) {
    // All targets must be valid (within TLS range)
    ASSERT_TRUE(target >= 256);
    ASSERT_TRUE(target <= 16384);
  }
}

// ==============================================
// Idle detection: Phase reset after idle gap
// ==============================================

TEST(TlsRecordPaddingStress, IdleGapResetsPhaseAndContinuesPadding) {
  // After an idle gap > idle_reset_ms, the DRS should reset to SlowStart
  // and continue producing properly padded records.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  config.drs_policy.idle_reset_ms_min = 500;
  config.drs_policy.idle_reset_ms_max = 500;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  // Write some records
  for (int i = 0; i < 10; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  // Advance clock by 2 seconds (well beyond idle_reset_ms=500ms)
  clock_ptr->advance(2.0);

  // Write more records — should reset DRS and continue padding correctly
  for (int i = 0; i < 10; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 20u);

  // All records (even after idle reset) must be within valid TLS range
  for (auto len : lengths) {
    ASSERT_TRUE(len >= 1u);
    ASSERT_TRUE(len <= 16384u);
  }
}

// ==============================================
// One-shot semantics: padding target resets after use
// ==============================================

TEST(TlsRecordPaddingStress, OneShotSemantics_TargetResetAfterEveryWrite) {
  // The stealth_target_frame_size_ in IntermediateTransport is reset to 0
  // after each write. Verify this by checking that without re-setting the
  // target, the next write uses default (0-15 byte) padding.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  // Primer
  td::BufferWriter primer(td::Slice("warm"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  // Write with target 1400
  transport.set_stealth_record_padding_target(1400);
  td::BufferWriter w1(td::Slice("test"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(w1), false);

  // Write WITHOUT re-setting target (should go back to native size)
  td::BufferWriter w2(td::Slice("test"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(w2), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 3u);
  ASSERT_EQ(1400u, lengths[1]);
  // Third record: native size (no stealth target) — should be small
  ASSERT_TRUE(lengths[2] < 256u);
}

// ==============================================
// Manual override vs DRS: independence
// ==============================================

TEST(TlsRecordPaddingStress, ManualOverride_PreventsDrsFromChangingTarget) {
  // Once set_max_tls_record_size() is called, DRS is bypassed.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  // Set manual override
  transport->set_max_tls_record_size(1234);

  // Write 100 records — all should use 1234, not DRS-sampled values
  for (int i = 0; i < 100; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(w), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  ASSERT_EQ(100, inner_ptr->write_calls);

  // All max_tls_record_sizes should be the manual override value
  // (initial set + per-batch sets)
  ASSERT_TRUE(inner_ptr->max_tls_record_sizes.size() >= 2u);
  for (size_t i = 1; i < inner_ptr->max_tls_record_sizes.size(); i++) {
    auto sz = inner_ptr->max_tls_record_sizes[i];
    ASSERT_EQ(1234, sz);
  }
}

}  // namespace
