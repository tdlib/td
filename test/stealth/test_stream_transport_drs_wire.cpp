//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

constexpr size_t kFirstTlsRecordObfuscatedHeaderSize = 64;
constexpr size_t kIntermediateTransportFrameHeaderSize = 4;
constexpr size_t kIntermediateTransportMaxRandomPadding = 15;

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

StealthConfig make_wire_drs_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_fixed_phase(256);
  config.drs_policy.congestion_open = make_fixed_phase(256);
  config.drs_policy.steady_state = make_fixed_phase(256);
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 256;
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

td::string flush_bulk_write(td::mtproto::IStreamTransport &transport, size_t payload_size) {
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport.init(&input_reader, &output_writer);

  transport.set_traffic_hint(TrafficHint::BulkData);
  td::BufferWriter writer(td::Slice(td::string(payload_size, 'x')), transport.max_prepend_size(),
                          transport.max_append_size());
  transport.write(std::move(writer), false);
  transport.pre_flush_write(transport.get_shaping_wakeup());

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  return output_reader.move_as_buffer_slice().as_slice().str();
}

TEST(StreamTransportDrsWire, FirstTlsRecordExceedsUncompensatedCapRange) {
  auto config = make_wire_drs_config();
  ASSERT_TRUE(config.validate().is_ok());

  auto inner =
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret()));

  auto decorator = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                                     td::make_unique<MockClock>());
  ASSERT_TRUE(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  auto wire = flush_bulk_write(*transport, 900);
  auto lengths = extract_tls_record_lengths(wire);
  ASSERT_TRUE(lengths.size() >= 2u);

  ASSERT_TRUE(lengths[0] > 256u);
  ASSERT_TRUE(lengths[0] <= 256u + kFirstTlsRecordObfuscatedHeaderSize + kIntermediateTransportFrameHeaderSize +
                                kIntermediateTransportMaxRandomPadding);
}

}  // namespace