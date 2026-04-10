// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// PR-S2: TLS Record Pad-to-Target — Deep adversarial tests.
// These tests simulate what a sophisticated DPI (TSPU) operator with ₽84B
// budget would look for: bucket quantization, record size histograms,
// small-record frequency, statistical anomalies, and information leaks.

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
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
  phase.max_repeat_run = 10;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_config_with_min_cap(td::int32 min_cap) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_fixed_phase(min_cap);
  config.drs_policy.congestion_open = make_fixed_phase(min_cap);
  config.drs_policy.steady_state = make_fixed_phase(min_cap);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = min_cap;
  config.drs_policy.max_payload_cap = min_cap;
  return config;
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

td::string align_payload(size_t size, char fill = 'X') {
  auto aligned_size = (size + 3u) & ~size_t{3};
  return td::string(aligned_size, fill);
}

struct WireHarness {
  td::unique_ptr<StealthTransportDecorator> transport;
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  MockClock *clock_ptr{nullptr};

  static WireHarness create(StealthConfig config, td::uint64 seed) {
    WireHarness h;
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
// AV-1: Record Size Histogram Attack
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, RecordSizeHistogram_NoBucketQuantizationPeaks) {
  // DPI attack: Histogram record sizes, check for MTProto bucket peaks at
  // {64,128,192,256,384,512,768,1024,1280} ±9 byte TLS/framing overhead.
  // With padding active, no peaks should appear at these values.
  auto config = make_config_with_min_cap(1400);
  ASSERT_TRUE(config.validate().is_ok());
  auto h = WireHarness::create(config, 42);

  h.write_and_flush("warm");

  // Simulate variety of MTProto-like payloads: pings, acks, small RPCs, medium RPCs
  std::vector<size_t> payload_sizes = {4, 4, 8, 16, 32, 64, 88, 100, 128, 200, 256, 384, 512, 768, 1024};
  for (int round = 0; round < 10; round++) {
    for (auto sz : payload_sizes) {
      td::string payload(sz, static_cast<char>('A' + (round % 26)));
      h.write_and_flush(payload);
    }
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 100u);

  // Check: no MTProto bucket peaks in record sizes
  // MTProto buckets + 9 (4 IT header + 5 TLS header) overhead
  std::vector<size_t> mtproto_bucket_wire_sizes = {73, 137, 201, 265, 393, 521, 777, 1033, 1289};
  std::map<size_t, int> bin_counts;
  for (auto len : lengths) {
    for (auto bucket : mtproto_bucket_wire_sizes) {
      if (len >= bucket - 2 && len <= bucket + 2) {
        bin_counts[bucket]++;
      }
    }
  }

  // With proper padding, NO record should land at bucket sizes (they should all be padded to 1400)
  for (auto &[bucket, count] : bin_counts) {
    // Tolerate at most 1 (rounding edge case)
    ASSERT_TRUE(count <= 1);
  }
}

TEST(TlsRecordPaddingDeepAdversarial, AllRecordsAboveSmallRecordThreshold) {
  // AV-3: Small-record frequency attack.
  // With stealth padding active, less than 5% of records should be < 200 bytes.
  auto config = make_config_with_min_cap(1400);
  ASSERT_TRUE(config.validate().is_ok());
  auto h = WireHarness::create(config, 7);

  h.write_and_flush("warm");

  // 200 writes mixing hint types
  for (int i = 0; i < 200; i++) {
    auto hint = (i % 5 == 0) ? TrafficHint::Keepalive : TrafficHint::Interactive;
    auto payload = align_payload(static_cast<size_t>(4 + (i % 100)), 'X');
    h.write_and_flush(payload, hint);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 200u);

  int small_count = 0;
  for (size_t i = 1; i < lengths.size(); i++) {  // skip primer
    if (lengths[i] < 200u) {
      small_count++;
    }
  }

  double small_frac = static_cast<double>(small_count) / static_cast<double>(lengths.size() - 1);
  // Must be < 5% (browser baseline)
  ASSERT_TRUE(small_frac < 0.05);
}

TEST(TlsRecordPaddingDeepAdversarial, KeepalivePingSize_NeverDetectably88Bytes) {
  // AV-1+AV-3: 88-byte keepalive pings are the most distinctive MTProto fingerprint.
  // Every single keepalive must be padded above the threshold.
  auto config = make_config_with_min_cap(900);
  ASSERT_TRUE(config.validate().is_ok());
  auto h = WireHarness::create(config, 33);

  h.write_and_flush("warm");

  // Simulate 200 keepalive-like payloads (88 bytes = typical keepalive packet)
  for (int i = 0; i < 200; i++) {
    td::string payload(88, '\x00');
    h.write_and_flush(payload, TrafficHint::Keepalive);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 200u);

  for (size_t i = 1; i < lengths.size(); i++) {
    // All must be >= min_payload_cap (900)
    ASSERT_TRUE(lengths[i] >= 900u);
    // None should be 88 + 4 + 5 = 97 (unpadded keepalive TLS record)
    ASSERT_TRUE(lengths[i] != 97u);
  }
}

// ==============================================
// AV-2: First-Flight Sequence Attack
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, AuthHandshakeSize_PaddedAboveThreshold) {
  // Auth handshake records must also be padded above the small-record threshold.
  auto config = make_config_with_min_cap(900);
  ASSERT_TRUE(config.validate().is_ok());
  auto h = WireHarness::create(config, 55);

  h.write_and_flush("warm");

  for (int i = 0; i < 10; i++) {
    td::string payload(200 + i * 20, 'A');
    h.write_and_flush(payload, TrafficHint::AuthHandshake);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 11u);

  for (size_t i = 1; i < lengths.size(); i++) {
    ASSERT_TRUE(lengths[i] >= 900u);
  }
}

// ==============================================
// AV-5: Cross-hint distinguishability
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, MixedTrafficHints_NoSizeClusteringByHint) {
  // A DPI should not be able to classify records as Keepalive/Interactive
  // purely by size. Both hint types should produce the SAME target size.
  auto config = make_config_with_min_cap(1400);
  ASSERT_TRUE(config.validate().is_ok());
  auto h = WireHarness::create(config, 7);

  h.write_and_flush("warm");

  // With a fixed target, all records should be 1400 regardless of hint.
  for (int i = 0; i < 50; i++) {
    h.write_and_flush("ping", TrafficHint::Keepalive);
    h.write_and_flush("data", TrafficHint::Interactive);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 101u);

  for (size_t i = 1; i < lengths.size(); i++) {
    ASSERT_EQ(1400u, lengths[i]);
  }
}

// ==============================================
// Security: Integer overflow and boundary tests
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, FrameSizeOverflowPrevented_MaxInt32) {
  // Target = INT32_MAX must be clamped to 16384, not cause overflow.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter primer(td::Slice("warm"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  transport.set_stealth_record_padding_target(std::numeric_limits<td::int32>::max());
  td::BufferWriter writer(td::Slice("tiny"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  // Must be clamped to TLS max (16384)
  ASSERT_EQ(16384u, lengths.back());
}

TEST(TlsRecordPaddingDeepAdversarial, ZeroPayloadWithPadding_NoUnderflow) {
  // Edge case: empty (0-byte) application data with padding target.
  // This should not crash or underflow.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  // First write (primer)
  td::BufferWriter primer(td::Slice("warm"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  // 4-byte payload (minimum aligned size for IntermediateTransport)
  transport.set_stealth_record_padding_target(1400);
  td::BufferWriter writer(td::Slice("ABCD"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(1400u, lengths[1]);
}

TEST(TlsRecordPaddingDeepAdversarial, NegativeTargetFailsClosed) {
  // INT32_MIN target should be clamped to 0, producing native-size record.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter primer(td::Slice("warm"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  transport.set_stealth_record_padding_target(std::numeric_limits<td::int32>::min());
  td::BufferWriter writer(td::Slice("tiny"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  // Native size — should be small, well below 256
  ASSERT_TRUE(lengths[1] < 256u);
}

// ==============================================
// Replay fingerprinting resistance
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, TwoConnections_DifferentSeeds_DifferentSizeSequences) {
  // Different RNG seeds must produce different record size distributions when
  // using non-fixed DRS bins. This prevents replay fingerprinting.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  std::vector<std::vector<size_t>> all_lengths;
  for (td::uint64 seed : {42ULL, 999ULL, 7777ULL}) {
    auto h = WireHarness::create(config, seed);
    h.write_and_flush("warm");
    for (int i = 0; i < 50; i++) {
      h.write_and_flush("data", TrafficHint::Interactive);
    }
    all_lengths.push_back(h.collect());
  }

  // Compare pairwise: at least some records must differ
  for (size_t a = 0; a < all_lengths.size(); a++) {
    for (size_t b = a + 1; b < all_lengths.size(); b++) {
      auto &la = all_lengths[a];
      auto &lb = all_lengths[b];
      size_t min_len = std::min(la.size(), lb.size());
      ASSERT_TRUE(min_len >= 20u);
      size_t diff = 0;
      for (size_t i = 1; i < min_len; i++) {
        if (la[i] != lb[i]) {
          diff++;
        }
      }
      // At least 10% of records should differ
      ASSERT_TRUE(diff > min_len / 10);
    }
  }
}

// ==============================================
// Standard record-size distribution tests
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, ConsecutiveSameSizePayloads_ProduceDifferentWireSizes) {
  // 100 identical keepalive payloads must produce multiple distinct TLS record sizes
  // when DRS has non-trivial bins, preventing trivially detectable patterns.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto h = WireHarness::create(config, 7);
  h.write_and_flush("warm");

  for (int i = 0; i < 100; i++) {
    td::string payload(88, '\x00');
    h.write_and_flush(payload, TrafficHint::Interactive);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 100u);

  std::set<size_t> distinct_sizes(lengths.begin() + 1, lengths.end());
  // Must produce at least 3 distinct sizes (different DRS bins/jitter)
  ASSERT_TRUE(distinct_sizes.size() >= 3u);
}

TEST(TlsRecordPaddingDeepAdversarial, RecordSizeDistribution_NoMonotonicSequences) {
  // DPI could detect a monotonically increasing/decreasing sequence.
  // The DRS should produce a non-monotonic pattern.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto h = WireHarness::create(config, 7);
  h.write_and_flush("warm");

  for (int i = 0; i < 50; i++) {
    h.write_and_flush("data", TrafficHint::Interactive);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 50u);

  // Check that the sequence is not monotonically non-decreasing or non-increasing
  int inc_run = 0;
  int dec_run = 0;
  int max_inc_run = 0;
  int max_dec_run = 0;
  for (size_t i = 2; i < lengths.size(); i++) {
    if (lengths[i] >= lengths[i - 1]) {
      inc_run++;
      dec_run = 0;
    } else {
      dec_run++;
      inc_run = 0;
    }
    max_inc_run = std::max(max_inc_run, inc_run);
    max_dec_run = std::max(max_dec_run, dec_run);
  }
  // No monotonic run of > 20 records (would be suspicious)
  ASSERT_TRUE(max_inc_run < 20);
  ASSERT_TRUE(max_dec_run < 20);
}

TEST(TlsRecordPaddingDeepAdversarial, BulkTransferRecordSizes_ShouldBeLarge) {
  // During BulkData phase, records should approximate h2 DATA frames (near 16KB).
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto h = WireHarness::create(config, 42);
  h.write_and_flush("warm");

  for (int i = 0; i < 100; i++) {
    h.write_and_flush("data", TrafficHint::BulkData);
  }

  auto lengths = h.collect();
  ASSERT_TRUE(lengths.size() >= 100u);

  // Count records > 4000 bytes (should be majority for BulkData)
  int large_count = 0;
  for (size_t i = 1; i < lengths.size(); i++) {
    if (lengths[i] > 4000u) {
      large_count++;
    }
  }
  double large_frac = static_cast<double>(large_count) / static_cast<double>(lengths.size() - 1);
  // At least 50% should be large
  ASSERT_TRUE(large_frac > 0.50);
}

// ==============================================
// Padding-content entropy (security)
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, PaddingBytesAreNotAllZero) {
  // The padding bytes in TLS records must be random, not all-zero.
  // This test captures the wire output and checks byte entropy.
  auto config = make_config_with_min_cap(2000);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  transport->set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter primer(td::Slice("warm"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(primer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  transport->set_traffic_hint(TrafficHint::Interactive);
  td::string payload(4, 'X');
  td::BufferWriter writer(td::Slice(payload), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(writer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();

  // Skip preamble and first record; focus on the second record's payload area
  // The second TLS record starts after preamble(6) + first_record_header(5) + first_record_payload
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  size_t second_record_start = 6 + 5 + lengths[0] + 5;  // preamble + hdr1 + data1 + hdr2
  ASSERT_TRUE(second_record_start + lengths[1] <= wire.size());

  // Count non-zero bytes in the TLS record payload
  int non_zero = 0;
  for (size_t i = second_record_start; i < second_record_start + lengths[1]; i++) {
    if (wire[i] != 0) {
      non_zero++;
    }
  }

  // At least 75% of payload bytes should be non-zero (encrypted + random padding)
  double non_zero_frac = static_cast<double>(non_zero) / static_cast<double>(lengths[1]);
  ASSERT_TRUE(non_zero_frac >= 0.75);
}

TEST(TlsRecordPaddingDeepAdversarial, RecordPayloadByteDistribution_NotBiased) {
  // The encrypted payload bytes should have roughly uniform byte distribution.
  // A severe bias would indicate broken encryption or padding.
  auto config = make_config_with_min_cap(4096);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  // Write several records
  for (int i = 0; i < 20; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::string payload(4, static_cast<char>(i));
    td::BufferWriter writer(td::Slice(payload), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();

  // Count byte frequencies across entire wire output (after preamble)
  std::map<unsigned char, int> freq;
  for (size_t i = 6; i < wire.size(); i++) {
    freq[static_cast<unsigned char>(wire[i])]++;
  }

  // At least 200 distinct byte values should appear in 20*4096 ≈ 80KB of encrypted data
  ASSERT_TRUE(freq.size() >= 200u);
}

// ==============================================
// Quick-ack bit safety under large padding
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, QuickAckBitPreserved_WithLargePadding) {
  // The IntermediateTransport 4-byte header uses bit 31 for quick_ack.
  // Quick-ack frames remain tiny control records, but they must not consume the
  // next ordinary payload's padding target or corrupt the stream.
  auto config = make_config_with_min_cap(8192);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  // Primer
  transport->set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter primer(td::Slice("warm"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(primer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  // Write quick_ack with large padding
  transport->set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter writer(td::Slice("qack"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(writer), true);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  transport->set_traffic_hint(TrafficHint::Interactive);
  td::BufferWriter regular(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(regular), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 3u);
  ASSERT_TRUE(lengths[1] < 64u);
  ASSERT_EQ(8192u, lengths[2]);
}

// ==============================================
// Target boundary precision
// ==============================================

TEST(TlsRecordPaddingDeepAdversarial, ExactBoundaryTargets) {
  // Test boundary values: 256 (minimum TLS), 16384 (maximum TLS)
  for (td::int32 target : {256, 16384}) {
    auto config = make_config_with_min_cap(target);
    ASSERT_TRUE(config.validate().is_ok());
    auto h = WireHarness::create(config, 77);

    h.write_and_flush("warm");
    h.write_and_flush("test");

    auto lengths = h.collect();
    ASSERT_TRUE(lengths.size() >= 2u);
    ASSERT_EQ(static_cast<size_t>(target), lengths.back());
  }
}

}  // namespace
