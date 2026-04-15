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

#include <algorithm>
#include <array>
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

constexpr size_t kPayloadSize = 1300;
constexpr size_t kPrimerHeaderOverhead = 64 + 6;

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

// Non-movable / non-copyable: `transport->init` captures raw pointers
// into the writer/reader members; copy/move (or NRVO failure on MSVC
// Debug builds) would invalidate those pointers. See REG-21/22 for the
// canonical pattern.
class WireFixture final {
 public:
  td::unique_ptr<StealthTransportDecorator> transport;
  MockClock *clock{nullptr};
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  td::ChainBufferReader input_reader;

  WireFixture(StealthConfig config, td::uint64 seed) {
    auto clock_local = td::make_unique<MockClock>();
    clock = clock_local.get();
    auto r = StealthTransportDecorator::create(
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret())),
        std::move(config), td::make_unique<MockRng>(seed), std::move(clock_local));
    CHECK(r.is_ok());
    transport = r.move_as_ok();
    input_reader = input_writer.extract_reader();
    transport->init(&input_reader, &output_writer);
  }

  WireFixture(const WireFixture &) = delete;
  WireFixture &operator=(const WireFixture &) = delete;
  WireFixture(WireFixture &&) = delete;
  WireFixture &operator=(WireFixture &&) = delete;
};

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 max_repeat_run) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_anti_repeat_config() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 2}, {1500, 1500, 1}}, 2);
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1500;
  return config;
}

StealthConfig make_reset_config() {
  MockRng rng(2);
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{900, 900, 1}, {1200, 1200, 1}}, 2);
  config.drs_policy.congestion_open = make_phase({{1500, 1500, 1}}, 2);
  config.drs_policy.steady_state = config.drs_policy.congestion_open;
  config.drs_policy.slow_start_records = 2;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 100;
  config.drs_policy.idle_reset_ms_max = 100;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1500;
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

void enqueue_flush(WireFixture &fixture, size_t payload_size, TrafficHint hint) {
  fixture.transport->set_traffic_hint(hint);
  td::BufferWriter writer(td::Slice(td::string(payload_size, 'x')), fixture.transport->max_prepend_size(),
                          fixture.transport->max_append_size());
  fixture.transport->write(std::move(writer), false);
  fixture.transport->pre_flush_write(fixture.transport->get_shaping_wakeup());
}

std::vector<td::int32> decode_flush_caps(const std::vector<size_t> &lengths, size_t flush_count) {
  std::vector<td::int32> caps;
  caps.reserve(flush_count);
  size_t pos = 0;
  for (size_t flush = 0; flush < flush_count; flush++) {
    CHECK(pos < lengths.size());
    auto len = lengths[pos];
    if (flush == 0) {
      if (len == 900 + kPrimerHeaderOverhead) {
        caps.push_back(900);
        pos += 2;
        continue;
      }
      if (len == 1200 + kPrimerHeaderOverhead) {
        caps.push_back(1200);
        pos += 2;
        continue;
      }
      if (len == 1500 + kPrimerHeaderOverhead) {
        caps.push_back(1500);
        pos += 1;
        continue;
      }
      if (len >= kPayloadSize + 4 + kPrimerHeaderOverhead && len <= kPayloadSize + 19 + kPrimerHeaderOverhead) {
        caps.push_back(1500);
        pos += 1;
        continue;
      }
    } else {
      if (len == 900) {
        caps.push_back(900);
        pos += 2;
        continue;
      }
      if (len == 1200) {
        caps.push_back(1200);
        pos += 2;
        continue;
      }
      if (len == 1500) {
        caps.push_back(1500);
        pos += 1;
        continue;
      }
      if (len >= kPayloadSize + 4 && len <= kPayloadSize + 19) {
        caps.push_back(1500);
        pos += 1;
        continue;
      }
    }
    LOG(FATAL) << "Unexpected TLS record sequence at flush " << flush << ", length=" << len;
  }
  CHECK(pos == lengths.size());
  return caps;
}

size_t max_repeat_run(const std::vector<td::int32> &series) {
  if (series.empty()) {
    return 0;
  }
  size_t best = 1;
  size_t current = 1;
  for (size_t i = 1; i < series.size(); i++) {
    if (series[i] == series[i - 1]) {
      current++;
      best = std::max(best, current);
    } else {
      current = 1;
    }
  }
  return best;
}

TEST(StreamTransportDrsSoakWire, SeedMatrixPreservesSameConnectionAntiRepeatAcrossFlushes) {
  constexpr std::array<td::uint64, 6> kSeeds = {7, 19, 41, 77, 123, 255};

  for (auto seed : kSeeds) {
    WireFixture fixture(make_anti_repeat_config(), seed);
    constexpr size_t kFlushCount = 24;
    for (size_t i = 0; i < kFlushCount; i++) {
      enqueue_flush(fixture, kPayloadSize, TrafficHint::BulkData);
    }

    auto output_reader = fixture.output_writer.extract_reader();
    output_reader.sync_with_writer();
    auto lengths = extract_tls_record_lengths(output_reader.move_as_buffer_slice().as_slice());
    auto caps = decode_flush_caps(lengths, kFlushCount);

    ASSERT_TRUE(
        std::all_of(caps.begin(), caps.end(), [](td::int32 cap) { return cap == 900 || cap == 1200 || cap == 1500; }));
    ASSERT_TRUE(max_repeat_run(caps) <= 2u);
    ASSERT_TRUE(std::count(caps.begin(), caps.end(), 1200) > 5);
  }
}

TEST(StreamTransportDrsSoakWire, SameConnectionIdleResetReturnsToSlowStartAfterPromotion) {
  WireFixture fixture(make_reset_config(), 77);

  enqueue_flush(fixture, kPayloadSize, TrafficHint::Interactive);
  enqueue_flush(fixture, kPayloadSize, TrafficHint::Interactive);
  enqueue_flush(fixture, kPayloadSize, TrafficHint::Interactive);
  fixture.clock->advance(0.2);
  enqueue_flush(fixture, kPayloadSize, TrafficHint::Interactive);

  auto output_reader = fixture.output_writer.extract_reader();
  output_reader.sync_with_writer();
  auto lengths = extract_tls_record_lengths(output_reader.move_as_buffer_slice().as_slice());
  auto caps = decode_flush_caps(lengths, 4);

  ASSERT_TRUE(caps[0] == 900 || caps[0] == 1200);
  ASSERT_TRUE(caps[1] == 900 || caps[1] == 1200);
  ASSERT_EQ(1500, caps[2]);
  ASSERT_TRUE(caps[3] == 900 || caps[3] == 1200);
}

}  // namespace