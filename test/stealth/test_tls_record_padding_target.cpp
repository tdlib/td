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
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/tests.h"

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
using td::mtproto::test::RecordingTransport;

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

TEST(TlsRecordPaddingTarget, DecoratorManualRecordSizeOverrideForwardsPadUpTargetToInnerTransport) {
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  transport->set_max_tls_record_size(1400);
  ASSERT_FALSE(inner_ptr->stealth_record_padding_targets.empty());
  ASSERT_EQ(1400, inner_ptr->stealth_record_padding_targets.back());
}

TEST(TlsRecordPaddingTarget, DecoratorManualRecordSizeOverridePreservesRequestedWireRecordLength) {
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(19),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  td::BufferWriter primer(td::Slice("warm"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(primer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  transport->set_max_tls_record_size(1400);
  td::BufferWriter writer(td::Slice("tiny"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(writer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(1400u, lengths[1]);
}

TEST(TlsRecordPaddingTarget, ObfuscatedTlsWritePadsSmallPayloadUpToRequestedTarget) {
  ObfuscatedTransport transport(2, ProxySecret::from_raw(make_tls_secret()));
  transport.set_max_tls_record_size(16384);

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  td::BufferWriter primer(td::Slice("warm"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(primer), false);

  transport.set_stealth_record_padding_target(1400);
  td::BufferWriter writer(td::Slice("tiny"), transport.max_prepend_size(), transport.max_append_size());
  transport.write(std::move(writer), false);

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(1400u, lengths[1]);
}

TEST(TlsRecordPaddingTarget, DecoratorDrsPadsSecondBulkRecordUpToFixedTarget) {
  auto config = make_fixed_record_size_config(256);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(9),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  for (int i = 0; i < 2; i++) {
    transport->set_traffic_hint(TrafficHint::BulkData);
    td::BufferWriter writer(td::Slice("tiny"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(256u, lengths[1]);
}

TEST(TlsRecordPaddingTarget, DecoratorDrsLargePadTargetDoesNotDependOnAllocatorSlack) {
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));
  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(17),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  for (int i = 0; i < 2; i++) {
    transport->set_traffic_hint(TrafficHint::BulkData);
    td::BufferWriter writer(td::Slice("tiny"), transport->max_prepend_size(), transport->max_append_size());
    transport->write(std::move(writer), false);
    transport->pre_flush_write(transport->get_shaping_wakeup());
  }

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto wire = output_reader.move_as_buffer_slice().as_slice().str();
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);
  ASSERT_EQ(1400u, lengths[1]);
}

TEST(TlsRecordPaddingTarget, DecoratorReappliesPadTargetForNonCoalescedWrites) {
  auto config = make_fixed_record_size_config(1400);
  ASSERT_TRUE(config.validate().is_ok());

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  inner_ptr->supports_tls_record_sizing_result = true;

  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(13),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  transport->set_traffic_hint(TrafficHint::BulkData);
  td::BufferWriter first(td::Slice("first"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(first), true);

  transport->set_traffic_hint(TrafficHint::BulkData);
  td::BufferWriter second(td::Slice("second"), transport->max_prepend_size(), transport->max_append_size());
  transport->write(std::move(second), false);

  transport->pre_flush_write(transport->get_shaping_wakeup());

  ASSERT_EQ(2, inner_ptr->write_calls);
  ASSERT_TRUE(inner_ptr->stealth_record_padding_targets.size() >= 3u);
  auto size = inner_ptr->stealth_record_padding_targets.size();
  ASSERT_EQ(1400, inner_ptr->stealth_record_padding_targets[size - 1]);
  ASSERT_EQ(1400, inner_ptr->stealth_record_padding_targets[size - 2]);
}

}  // namespace