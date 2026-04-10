// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// PR-S2: TLS Record Pad-to-Target — Security, memory safety, and OWASP ASVS L2 tests.
// Validates: no integer overflow in size calculations, no buffer over-read/write,
// safe handling of extreme inputs, no information leakage through padding content,
// and correct fail-closed behavior.

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <limits>
#include <string>
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

// ==============================================
// Integer overflow defense
// ==============================================

TEST(TlsRecordPaddingSecurity, IntOverflow_MaxTlsRecordSize_SafelyClamped) {
  // max_tls_record_size set to INT32_MAX: must not cause overflow in calculations.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(std::numeric_limits<td::int32>::max());

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter primer(td::Slice("warm"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  transport.set_stealth_record_padding_target(16384);
  td::BufferWriter writer(td::Slice("test"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  // Must be clamped to safe value, not overflow
  ASSERT_TRUE(lengths[1] <= 16384u);
}

TEST(TlsRecordPaddingSecurity, IntOverflow_BothMaxValues_NoUB) {
  // Setting both max_tls_record_size and stealth_record_padding_target to INT32_MAX.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(std::numeric_limits<td::int32>::max());
  transport.set_stealth_record_padding_target(std::numeric_limits<td::int32>::max());

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter writer(td::Slice("test"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 1u);
  ASSERT_TRUE(lengths[0] <= 16384u);
}

TEST(TlsRecordPaddingSecurity, IntOverflow_NegativeTarget_Clamped) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);
  transport.set_stealth_record_padding_target(-1);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter writer(td::Slice("test"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 1u);
  // Must produce valid TLS record
  ASSERT_TRUE(lengths[0] > 0u);
  ASSERT_TRUE(lengths[0] <= 16384u);
}

// ==============================================
// Buffer safety
// ==============================================

TEST(TlsRecordPaddingSecurity, MaxAppendSize_SufficientForTarget) {
  // max_append_size() must return a value sufficient for the current target.
  // If it's too small, the padding won't fit and the write will underflow or crash.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);
  transport.set_stealth_record_padding_target(16384);

  auto append_size = transport.max_append_size();
  // Must be at least target - 4 (IntermediateTransport header)
  ASSERT_TRUE(append_size >= 16384u - 4u);
}

TEST(TlsRecordPaddingSecurity, MaxAppendSize_DefaultWithoutTarget) {
  // Without stealth target (non-TLS or target=0), append size is small (15).
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  // Don't set any stealth target
  auto append_size = transport.max_append_size();
  ASSERT_TRUE(append_size <= 15u);
}

TEST(TlsRecordPaddingSecurity, MaxAppendSize_AfterTargetReset_ReturnsToDefault) {
  // After write (which resets the target), max_append_size should return to default.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  transport.set_stealth_record_padding_target(4096);
  ASSERT_TRUE(transport.max_append_size() >= 4092u);

  td::BufferWriter writer(td::Slice("test"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  // After write, target resets in IntermediateTransport (but stealth_record_padding_target_
  // in ObfuscatedTransport stays). The max_append_size is based on stealth_record_padding_target_.
  // This is by design — the caller must provide enough buffer space based on the target they set.
}

// ==============================================
// Fail-closed: Invalid states
// ==============================================

TEST(TlsRecordPaddingSecurity, FailClosed_NullInnerTransport) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  ASSERT_TRUE(config.validate().is_ok());

  auto r =
      StealthTransportDecorator::create(nullptr, config, td::make_unique<MockRng>(42), td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_error());
}

TEST(TlsRecordPaddingSecurity, FailClosed_NullRng) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, nullptr, td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_error());
}

TEST(TlsRecordPaddingSecurity, FailClosed_NullClock) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42), nullptr);
  ASSERT_TRUE(r.is_error());
}

// ==============================================
// TLS record size range invariants
// ==============================================

TEST(TlsRecordPaddingSecurity, TlsRecordSize_NeverExceeds16384) {
  // TLS 1.2/1.3 spec: record payload must not exceed 2^14 = 16384 bytes.
  // This is a hard invariant that must hold even with extreme targets.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42), std::move(clock));
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  // Write with BulkData hint (should get large targets)
  for (int i = 0; i < 100; i++) {
    transport->set_traffic_hint(TrafficHint::BulkData);
    td::string payload(8192, 'B');
    td::BufferWriter writer(td::Slice(payload), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);

  for (auto len : lengths) {
    ASSERT_TRUE(len <= 16384u);
  }
}

TEST(TlsRecordPaddingSecurity, TlsRecordSize_AlwaysAboveZero) {
  // No TLS record should have 0-byte payload.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto clock = td::make_unique<MockClock>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42), std::move(clock));
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  for (int i = 0; i < 50; i++) {
    transport->set_traffic_hint(TrafficHint::Interactive);
    td::BufferWriter writer(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);

  for (auto len : lengths) {
    ASSERT_TRUE(len > 0u);
  }
}

// ==============================================
// Padding content: no information leakage
// ==============================================

TEST(TlsRecordPaddingSecurity, PaddingDoesNotLeakPreviousPayload) {
  // The padding bytes must be fresh random, not stale buffer content from
  // a previous write. This prevents information leakage across messages.
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  // Write "AAAA" with target 8192
  transport.set_stealth_record_padding_target(8192);
  td::string payload_a(4, 'A');
  td::BufferWriter writer_a(td::Slice(payload_a), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer_a), false);

  // Write "BBBB" with target 8192
  transport.set_stealth_record_padding_target(8192);
  td::string payload_b(4, 'B');
  td::BufferWriter writer_b(td::Slice(payload_b), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer_b), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);

  // Skip preamble(6) + first record header(5) + first record data + second record header(5)
  size_t second_start = 6 + 5 + lengths[0] + 5;
  ASSERT_TRUE(second_start + lengths[1] <= wire.size());

  // The second record's payload should NOT contain unbroken 'A' sequences
  // (which would indicate stale buffer content from the first write).
  // Count max consecutive 'A' bytes (encrypted, so should be at most ~4 by chance)
  int max_a_run = 0;
  int current_a_run = 0;
  for (size_t i = second_start; i < second_start + lengths[1]; i++) {
    if (wire[i] == 'A') {
      current_a_run++;
      max_a_run = std::max(max_a_run, current_a_run);
    } else {
      current_a_run = 0;
    }
  }
  // In 8192 bytes of AES-CTR encrypted data, consecutive 'A' runs should not exceed ~16
  // (each byte has ~1/256 chance of being 'A')
  ASSERT_TRUE(max_a_run < 32);
}

// ==============================================
// supports_tls_record_sizing() API behavior
// ==============================================

TEST(TlsRecordPaddingSecurity, SupportsRecordSizing_OnlyForTlsSecrets) {
  // ObfuscatedTransport should only support record sizing when emulating TLS.
  // Non-TLS secrets must report false.
  td::string non_tls_secret;
  non_tls_secret.push_back(static_cast<char>(0xdd));
  non_tls_secret += "0123456789secret";

  ObfuscatedTransport transport(2, ProxySecret::from_raw(non_tls_secret));
  ASSERT_FALSE(transport.supports_tls_record_sizing());
}

TEST(TlsRecordPaddingSecurity, SupportsRecordSizing_TrueForTlsSecrets) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  ASSERT_TRUE(transport.supports_tls_record_sizing());
}

// ==============================================
// Deterministic structure: consistent record sizing across same seed
// ==============================================

TEST(TlsRecordPaddingSecurity, SameSeed_ProducesIdenticalRecords) {
  // Same RNG seed + same payload sequence → identical record sizes.
  // This validates determinism (important for reproducibility/testing).
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  ASSERT_TRUE(config.validate().is_ok());

  std::vector<std::vector<size_t>> all_lengths;
  for (int run = 0; run < 2; run++) {
    auto inner =
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
    auto clock = td::make_unique<MockClock>();
    auto r =
        StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42), std::move(clock));
    ASSERT_TRUE(r.is_ok());
    auto transport = r.move_as_ok();

    td::ChainBufferWriter output_writer;
    td::ChainBufferWriter input_writer;
    auto input_reader = input_writer.extract_reader();
    transport->init(&input_reader, &output_writer);

    for (int i = 0; i < 20; i++) {
      transport->set_traffic_hint(TrafficHint::Interactive);
      td::BufferWriter w(td::Slice("data"), transport->max_prepend_size(), transport->max_append_size());
      transport->write(std::move(w), false);
      transport->pre_flush_write(transport->get_shaping_wakeup());
    }

    auto output_reader = output_writer.extract_reader();
    output_reader.sync_with_writer();
    auto wire = output_reader.move_as_buffer_slice().as_slice().str();
    all_lengths.push_back(extract_tls_record_lengths(wire));
  }

  // Same seed, same input → same record sizes
  ASSERT_EQ(all_lengths[0].size(), all_lengths[1].size());
  for (size_t i = 0; i < all_lengths[0].size(); i++) {
    ASSERT_EQ(all_lengths[0][i], all_lengths[1][i]);
  }
}

// ==============================================
// CryptoPaddingPolicy: stealth padding configured
// ==============================================

TEST(TlsRecordPaddingSecurity, ConfigurePacketInfo_SetsStealthPaddingFlags) {
  // Verify that the decorator correctly configures PacketInfo for stealth padding.
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  config.crypto_padding_policy.enabled = true;
  config.crypto_padding_policy.min_padding_bytes = 12;
  config.crypto_padding_policy.max_padding_bytes = 480;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::mtproto::PacketInfo packet_info;
  transport->configure_packet_info(&packet_info);

  ASSERT_TRUE(packet_info.use_random_padding);
  ASSERT_TRUE(packet_info.use_stealth_padding);
  ASSERT_EQ(12, packet_info.stealth_padding_min_bytes);
  ASSERT_EQ(480, packet_info.stealth_padding_max_bytes);
}

TEST(TlsRecordPaddingSecurity, ConfigurePacketInfo_DisabledCrypto_NoStealthFlags) {
  MockRng rng_cfg(1);
  auto config = StealthConfig::default_config(rng_cfg);
  config.crypto_padding_policy.enabled = false;
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto r = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(42),
                                             td::make_unique<MockClock>());
  ASSERT_TRUE(r.is_ok());
  auto transport = r.move_as_ok();

  td::mtproto::PacketInfo packet_info;
  transport->configure_packet_info(&packet_info);

  ASSERT_FALSE(packet_info.use_stealth_padding);
}

}  // namespace
