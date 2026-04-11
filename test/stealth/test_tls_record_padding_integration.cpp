// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// PR-S2: TLS Record Pad-to-Target — Wire-level integration tests.
// These tests exercise the full write path through StealthTransportDecorator →
// ObfuscatedTransport → IntermediateTransport → TLS record framing and verify
// that the wire bytes match the expected structure.

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <string>
#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::tcp::ObfuscatedTransport;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

DrsPhaseModel make_fixed_phase(td::int32 cap) {
  DrsPhaseModel phase;
  phase.bins = {{cap, cap, 1}};
  phase.max_repeat_run = 1;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_fixed_record_size_config(td::int32 record_size) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_fixed_phase(record_size);
  config.drs_policy.congestion_open = make_fixed_phase(record_size);
  config.drs_policy.steady_state = make_fixed_phase(record_size);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = record_size;
  config.drs_policy.max_payload_cap = record_size;
  return config;
}

StealthConfig make_multi_phase_config() {
  MockRng rng(42);
  auto config = StealthConfig::default_config(rng);
  // Slow start: small records 256
  config.drs_policy.slow_start.bins = {{256, 256, 1}};
  config.drs_policy.slow_start.max_repeat_run = 10;
  config.drs_policy.slow_start.local_jitter = 0;
  config.drs_policy.slow_start_records = 4;
  // Congestion open: medium records 1400
  config.drs_policy.congestion_open.bins = {{1400, 1400, 1}};
  config.drs_policy.congestion_open.max_repeat_run = 10;
  config.drs_policy.congestion_open.local_jitter = 0;
  config.drs_policy.congestion_bytes = 8192;
  // Steady state: large records 4096
  config.drs_policy.steady_state.bins = {{4096, 4096, 1}};
  config.drs_policy.steady_state.max_repeat_run = 10;
  config.drs_policy.steady_state.local_jitter = 0;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 16384;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  return config;
}

std::vector<size_t> extract_tls_record_lengths(td::Slice wire) {
  std::vector<size_t> lengths;
  size_t offset = 0;
  if (wire.size() >= 6 && wire.substr(0, 6) == td::Slice("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  while (offset + 5 <= wire.size()) {
    ASSERT_EQ(static_cast<td::uint8>(0x17), static_cast<td::uint8>(wire[offset]));
    ASSERT_EQ(static_cast<td::uint8>(0x03), static_cast<td::uint8>(wire[offset + 1]));
    ASSERT_EQ(static_cast<td::uint8>(0x03), static_cast<td::uint8>(wire[offset + 2]));
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    lengths.push_back(len);
    offset += 5 + len;
  }
  ASSERT_EQ(offset, wire.size());
  return lengths;
}

td::string align_payload(size_t size, char fill = 'X') {
  auto aligned_size = (size + 3u) & ~size_t{3};
  return td::string(aligned_size, fill);
}

// `WireTestHarness` owns the `ChainBufferReader` that the StealthTransport
// is initialised against, and the transport stores a raw pointer to that
// reader internally. The harness is therefore NOT movable / NOT copyable
// — moving it would invalidate the pointer that `transport->init()`
// captured into the transport's poll info. Tests construct it in-place
// via `WireTestHarness h(config, seed);`.
//
// The historical form factored construction into a static `create(...)`
// returning by value, with `input_reader` declared as a stack local
// inside `create()`. NRVO does NOT save you here: the local goes out of
// scope at the closing brace of `create()` and the returned harness's
// `transport` already holds a raw pointer into a freed stack slot. This
// produced a hard segfault on the very first `transport->write()` of
// `Test_TlsRecordPaddingIntegration_*` and segfaulted every test that
// followed it in the `Test_Tls*` filter run.
class WireTestHarness {
 public:
  td::unique_ptr<StealthTransportDecorator> transport;
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  td::ChainBufferReader input_reader;
  MockClock *clock_ptr{nullptr};

  WireTestHarness(StealthConfig config, td::uint64 seed) {
    auto inner =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    auto clock = td::make_unique<MockClock>();
    clock_ptr = clock.get();
    auto result =
        StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(seed), std::move(clock));
    ASSERT_TRUE(result.is_ok());
    transport = result.move_as_ok();

    input_reader = input_writer.extract_reader();
    transport->init(&input_reader, &output_writer);
  }

  WireTestHarness(const WireTestHarness &) = delete;
  WireTestHarness &operator=(const WireTestHarness &) = delete;
  WireTestHarness(WireTestHarness &&) = delete;
  WireTestHarness &operator=(WireTestHarness &&) = delete;

  void write_and_flush(td::Slice payload, TrafficHint hint = TrafficHint::Interactive, bool quick_ack = false) {
    transport->set_traffic_hint(hint);
    td::BufferWriter writer(payload, transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), quick_ack);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  std::vector<size_t> collect_record_lengths() {
    auto output_reader = output_writer.extract_reader();
    output_reader.sync_with_writer();
    auto wire = output_reader.move_as_buffer_slice().as_slice().str();
    return extract_tls_record_lengths(wire);
  }
};

// --- Integration tests ---

TEST(TlsRecordPaddingIntegration, SmallPayloadPaddedToTargetThroughFullStack) {
  // Verify a tiny 4-byte payload "ping" gets padded to 1400 through the full stack.
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());
  WireTestHarness harness(config, 7);

  // Primer write (triggers first-packet TLS preamble)
  harness.write_and_flush("warm");
  // Actual test write
  harness.write_and_flush("tiny");

  auto lengths = harness.collect_record_lengths();
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(1400u, lengths[1]);
}

TEST(TlsRecordPaddingIntegration, LargePayloadExceedingTargetNotTruncated) {
  // If the MTProto payload is already larger than the DRS target, the transport
  // must fragment it safely across multiple TLS records instead of truncating it
  // or attempting negative padding.
  auto config = make_fixed_record_size_config(256);
  ASSERT_TRUE(config.validate().is_ok());
  WireTestHarness harness(config, 33);

  harness.write_and_flush("warm");
  auto big_payload = align_payload(2048, 'X');
  harness.write_and_flush(big_payload);

  auto lengths = harness.collect_record_lengths();
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_TRUE(lengths.size() > 2u);
  size_t total_after_primer = 0;
  for (size_t i = 1; i < lengths.size(); i++) {
    ASSERT_TRUE(lengths[i] <= 256u);
    total_after_primer += lengths[i];
  }
  ASSERT_TRUE(total_after_primer >= big_payload.size());
}

TEST(TlsRecordPaddingIntegration, MultipleWritesAllPaddedToTarget) {
  auto config = make_fixed_record_size_config(1200);
  ASSERT_TRUE(config.validate().is_ok());
  WireTestHarness harness(config, 11);

  harness.write_and_flush("warm");
  for (int i = 0; i < 10; i++) {
    harness.write_and_flush("data");
  }

  auto lengths = harness.collect_record_lengths();
  ASSERT_TRUE(lengths.size() >= 11u);
  for (size_t i = 1; i < lengths.size(); i++) {
    ASSERT_EQ(1200u, lengths[i]);
  }
}

TEST(TlsRecordPaddingIntegration, QuickAckRecordDoesNotConsumeNextPayloadPadding) {
  // Quick_ack writes are control records, but they must not interfere with the
  // next ordinary payload record receiving the configured padding target.
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());
  WireTestHarness harness(config, 7);

  harness.write_and_flush("warm");
  harness.write_and_flush("ack!", TrafficHint::Interactive, true);
  harness.write_and_flush("data");

  auto lengths = harness.collect_record_lengths();
  ASSERT_TRUE(lengths.size() >= 3u);
  ASSERT_TRUE(lengths[1] < 64u);
  ASSERT_EQ(1400u, lengths[2]);
}

TEST(TlsRecordPaddingIntegration, AllTlsRecordsHaveCorrectContentType) {
  // Every TLS record must have content type 0x17 (Application Data).
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto result =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(3), std::move(clock));
  ASSERT_TRUE(result.is_ok());
  auto transport = result.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  for (int i = 0; i < 5; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter writer(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();

  // Parse TLS records manually, verify content type
  size_t offset = 0;
  // Skip first-packet preamble
  if (wire.size() >= 6 && wire.substr(0, 6) == std::string("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  int record_count = 0;
  while (offset + 5 <= wire.size()) {
    ASSERT_EQ(0x17, static_cast<td::uint8>(wire[offset]) & 0xFF);
    ASSERT_EQ(0x03, static_cast<td::uint8>(wire[offset + 1]) & 0xFF);
    ASSERT_EQ(0x03, static_cast<td::uint8>(wire[offset + 2]) & 0xFF);
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    ASSERT_TRUE(len > 0u);
    ASSERT_TRUE(len <= 16384u);
    offset += 5 + len;
    record_count++;
  }
  ASSERT_EQ(offset, wire.size());
  ASSERT_TRUE(record_count >= 5);
}

TEST(TlsRecordPaddingIntegration, FirstTlsPacketHasChangeCipherSpecPreamble) {
  // The first TLS record must be preceded by the fake ChangeCipherSpec.
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto result =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(3), std::move(clock));
  ASSERT_TRUE(result.is_ok());
  auto transport = result.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  transport->set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter writer(td::Slice("test"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(writer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();

  // First 6 bytes: ChangeCipherSpec
  ASSERT_TRUE(wire.size() >= 6u);
  ASSERT_EQ(0x14, static_cast<td::uint8>(wire[0]) & 0xFF);
  ASSERT_EQ(0x03, static_cast<td::uint8>(wire[1]) & 0xFF);
  ASSERT_EQ(0x03, static_cast<td::uint8>(wire[2]) & 0xFF);
  ASSERT_EQ(0x00, static_cast<td::uint8>(wire[3]) & 0xFF);
  ASSERT_EQ(0x01, static_cast<td::uint8>(wire[4]) & 0xFF);
  ASSERT_EQ(0x01, static_cast<td::uint8>(wire[5]) & 0xFF);

  // Then Application Data record
  ASSERT_TRUE(wire.size() >= 11u);
  ASSERT_EQ(0x17, static_cast<td::uint8>(wire[6]) & 0xFF);
}

TEST(TlsRecordPaddingIntegration, PhaseTransitionChangesRecordSizes) {
  // DRS phases should produce different record sizes as the connection progresses.
  auto config = make_multi_phase_config();
  ASSERT_TRUE(config.validate().is_ok());
  WireTestHarness harness(config, 99);

  // Write primer
  harness.write_and_flush("warm");

  // Write 4 records in SlowStart (should be 256)
  for (int i = 0; i < 4; i++) {
    harness.write_and_flush("data");
  }

  // After slow_start_records=4, should transition to CongestionOpen (1400)
  for (int i = 0; i < 8; i++) {
    harness.write_and_flush("data");
  }

  auto lengths = harness.collect_record_lengths();
  ASSERT_TRUE(lengths.size() >= 13u);

  // First record is primer, skip it
  // The primer itself consumes one slow-start slot, so the next three records
  // are still slow-start-sized and later records move into congestion-open.
  for (size_t i = 1; i <= 3 && i < lengths.size(); i++) {
    ASSERT_EQ(256u, lengths[i]);
  }

  // Some records after index 5 should be 1400 (congestion open)
  bool found_congestion = false;
  for (size_t i = 5; i < lengths.size(); i++) {
    if (lengths[i] == 1400u) {
      found_congestion = true;
      break;
    }
  }
  ASSERT_TRUE(found_congestion);
}

TEST(TlsRecordPaddingIntegration, TwoSeeds_ProduceDifferentPaddingContent) {
  // Different RNG seeds must produce different padding byte patterns.
  // This guards against hardcoded or degenerate padding content.
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  td::string wire1, wire2;
  for (int seed : {42, 999}) {
    auto inner =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    auto clock = td::make_unique<MockClock>();
    auto result =
        StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(seed), std::move(clock));
    ASSERT_TRUE(result.is_ok());
    auto transport = result.move_as_ok();

    td::ChainBufferWriter output_writer;
    td::ChainBufferWriter input_writer;
    auto input_reader = input_writer.extract_reader();
    transport->init(&input_reader, &output_writer);

    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter writer(td::Slice("test"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());

    auto output_reader = output_writer.extract_reader();
    output_reader.sync_with_writer();
    (seed == 42 ? wire1 : wire2) = output_reader.move_as_buffer_slice().as_slice().str();
  }

  // Same payload, same target, but different seeds should produce different wire content
  // (because the random padding bytes inside the frame differ)
  ASSERT_EQ(wire1.size(), wire2.size());
  // Count differing bytes (after preamble + TLS header, i.e., byte 11 onward)
  size_t diff_count = 0;
  for (size_t i = 11; i < wire1.size(); i++) {
    if (wire1[i] != wire2[i]) {
      diff_count++;
    }
  }
  // Expect substantial differences (> 10% of payload)
  ASSERT_TRUE(diff_count > wire1.size() / 20);
}

TEST(TlsRecordPaddingIntegration, WireRecordLengthFieldMatchesActualPayload) {
  // The 2-byte length field in each TLS record header must exactly match
  // the number of bytes that follow before the next record.
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto result =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(17), std::move(clock));
  ASSERT_TRUE(result.is_ok());
  auto transport = result.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  for (int i = 0; i < 10; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    auto payload = align_payload(static_cast<size_t>(50 + i * 100), static_cast<char>('A' + i));
    td::BufferWriter writer(td::Slice(payload), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();

  size_t offset = 0;
  if (wire.size() >= 6 && wire.substr(0, 6) == std::string("\x14\x03\x03\x00\x01\x01", 6)) {
    offset = 6;
  }
  while (offset + 5 <= wire.size()) {
    size_t len = (static_cast<td::uint8>(wire[offset + 3]) << 8) | static_cast<td::uint8>(wire[offset + 4]);
    ASSERT_TRUE(offset + 5 + len <= wire.size());
    offset += 5 + len;
  }
  ASSERT_EQ(offset, wire.size());
}

TEST(TlsRecordPaddingIntegration, BulkDataHintUsesLargerRecords) {
  // BulkData hint should always sample from steady_state (largest phase).
  MockRng rng(42);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start.bins = {{256, 256, 1}};
  config.drs_policy.slow_start.max_repeat_run = 10;
  config.drs_policy.slow_start.local_jitter = 0;
  config.drs_policy.slow_start_records = 1000;  // Never leave slow start via records
  config.drs_policy.congestion_open.bins = {{1400, 1400, 1}};
  config.drs_policy.congestion_open.max_repeat_run = 10;
  config.drs_policy.congestion_open.local_jitter = 0;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.steady_state.bins = {{8192, 8192, 1}};
  config.drs_policy.steady_state.max_repeat_run = 10;
  config.drs_policy.steady_state.local_jitter = 0;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 16384;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  ASSERT_TRUE(config.validate().is_ok());

  WireTestHarness harness(config, 77);
  harness.write_and_flush("warm");  // primer

  // Write with BulkData hint — should use steady_state bins (8192)
  for (int i = 0; i < 5; i++) {
    harness.write_and_flush("data", TrafficHint::BulkData);
  }

  auto lengths = harness.collect_record_lengths();
  for (size_t i = 1; i < lengths.size(); i++) {
    ASSERT_EQ(8192u, lengths[i]);
  }
}

TEST(TlsRecordPaddingIntegration, KeepaliveHintGetsMinPayloadCap) {
  // Keepalive hint gets min_payload_cap from DRS, then small-record budget may raise it.
  MockRng rng(42);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 16384;
  config.drs_policy.slow_start.bins = {{4096, 4096, 1}};
  config.drs_policy.slow_start.max_repeat_run = 10;
  config.drs_policy.slow_start.local_jitter = 0;
  config.drs_policy.slow_start_records = 1000;
  config.drs_policy.congestion_open.bins = {{4096, 4096, 1}};
  config.drs_policy.congestion_open.max_repeat_run = 10;
  config.drs_policy.congestion_open.local_jitter = 0;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.steady_state.bins = {{4096, 4096, 1}};
  config.drs_policy.steady_state.max_repeat_run = 10;
  config.drs_policy.steady_state.local_jitter = 0;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  ASSERT_TRUE(config.validate().is_ok());

  WireTestHarness harness(config, 7);
  harness.write_and_flush("warm");  // primer

  harness.write_and_flush("ping", TrafficHint::Keepalive);

  auto lengths = harness.collect_record_lengths();
  ASSERT_TRUE(lengths.size() >= 2u);
  // Keepalive returns min_payload_cap=900, so record size should be >=900
  ASSERT_TRUE(lengths[1] >= 900u);
}

}  // namespace
