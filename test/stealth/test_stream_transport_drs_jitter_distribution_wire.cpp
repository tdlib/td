// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <set>
#include <unordered_map>
#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::tcp::ObfuscatedTransport;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;

constexpr size_t kSampleCount = 256;
constexpr size_t kPayloadSize = 4096;
constexpr size_t kObfuscatedHeaderSize = 64;
constexpr size_t kMaxRandomPadding = 15;

enum class Bucket : td::uint8 { Low = 0, Mid = 1, High = 2 };

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 local_jitter) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = 8;
  phase.local_jitter = local_jitter;
  return phase;
}

StealthConfig make_distribution_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{256, 256, 1}}, 0);
  config.drs_policy.congestion_open = make_phase({{256, 256, 1}}, 0);
  config.drs_policy.steady_state = make_phase({{256, 264, 1}, {320, 328, 6}, {448, 456, 1}}, 3);
  config.drs_policy.slow_start_records = 1;
  config.drs_policy.congestion_bytes = 1;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 456;
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

size_t sample_first_bulk_record_length(td::uint64 seed) {
  auto decorator = StealthTransportDecorator::create(
      td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret())),
      make_distribution_config(), td::make_unique<MockRng>(seed), td::make_unique<MockClock>());
  CHECK(decorator.is_ok());
  auto transport = decorator.move_as_ok();

  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  auto input_reader = input_writer.extract_reader();
  transport->init(&input_reader, &output_writer);

  transport->set_traffic_hint(TrafficHint::BulkData);
  td::BufferWriter writer(td::Slice(td::string(kPayloadSize, 'x')), transport->max_prepend_size(),
                          transport->max_append_size());
  transport->write(std::move(writer), false);
  transport->pre_flush_write(transport->get_shaping_wakeup());

  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto lengths = extract_tls_record_lengths(output_reader.move_as_buffer_slice().as_slice());
  CHECK(lengths.size() >= 2u);
  return lengths[0];
}

Bucket classify_first_record_length(size_t len) {
  if (len >= 256 + kObfuscatedHeaderSize && len <= 264 + kObfuscatedHeaderSize + kMaxRandomPadding) {
    return Bucket::Low;
  }
  if (len >= 320 + kObfuscatedHeaderSize && len <= 328 + kObfuscatedHeaderSize + kMaxRandomPadding) {
    return Bucket::Mid;
  }
  if (len >= 448 + kObfuscatedHeaderSize && len <= 456 + kObfuscatedHeaderSize + kMaxRandomPadding) {
    return Bucket::High;
  }
  LOG(FATAL) << "Unexpected first record length " << len;
  UNREACHABLE();
}

TEST(StreamTransportDrsJitterDistributionWire, BulkFirstRecordLengthsStayWithinJitteredWireBins) {
  std::unordered_map<Bucket, size_t> bucket_counts;
  std::unordered_map<Bucket, std::set<size_t>> bucket_values;
  for (size_t seed = 1; seed <= kSampleCount; seed++) {
    auto len = sample_first_bulk_record_length(static_cast<td::uint64>(seed));
    auto bucket = classify_first_record_length(len);
    bucket_counts[bucket]++;
    bucket_values[bucket].insert(len);
  }

  ASSERT_EQ(kSampleCount, bucket_counts[Bucket::Low] + bucket_counts[Bucket::Mid] + bucket_counts[Bucket::High]);
  ASSERT_TRUE(bucket_counts[Bucket::Low] > 0u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > 0u);
  ASSERT_TRUE(bucket_counts[Bucket::High] > 0u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > bucket_counts[Bucket::Low] * 2u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > bucket_counts[Bucket::High] * 2u);
  ASSERT_TRUE(bucket_counts[Bucket::Mid] > kSampleCount / 2u);
  ASSERT_TRUE(bucket_values[Bucket::Low].size() >= 6u);
  ASSERT_TRUE(bucket_values[Bucket::Mid].size() >= 8u);
  ASSERT_TRUE(bucket_values[Bucket::High].size() >= 6u);
}

}  // namespace