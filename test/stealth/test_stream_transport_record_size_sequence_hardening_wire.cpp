// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"
#include "td/mtproto/TcpTransport.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::tcp::ObfuscatedTransport;
using td::mtproto::test::MockClock;

constexpr size_t kPayloadSize = 1300;
constexpr size_t kPrimerHeaderOverhead = 64 + 6;

class DominantBinRng final : public IRng {
 public:
  void fill_secure_bytes(td::MutableSlice dest) final {
    dest.fill('\0');
  }

  td::uint32 secure_uint32() final {
    return 0u;
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0u);
    return 0u;
  }
};

td::string make_tls_secret() {
  td::string secret;
  secret.push_back(static_cast<char>(0xee));
  secret += "0123456789secret";
  secret += "www.google.com";
  return secret;
}

DrsPhaseModel make_phase() {
  DrsPhaseModel phase;
  phase.bins = {{900, 900, 100}, {1200, 1200, 1}};
  phase.max_repeat_run = 2;
  phase.local_jitter = 0;
  return phase;
}

StealthConfig make_config() {
  DominantBinRng rng;
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase();
  config.drs_policy.congestion_open = make_phase();
  config.drs_policy.steady_state = make_phase();
  config.drs_policy.slow_start_records = 1024;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 100;
  config.drs_policy.idle_reset_ms_max = 100;
  config.drs_policy.min_payload_cap = 900;
  config.drs_policy.max_payload_cap = 1200;
  return config;
}

// `WireFixture` is non-movable / non-copyable: `transport->init` captures
// raw pointers into `input_reader` and `output_writer`, and any
// copy/move (or NRVO failure on MSVC Debug builds) would invalidate
// those pointers because the writer/reader storage moves with the
// struct. Construct in place via the constructor and bind by reference
// at the call sites — same pattern as the seven other test scaffolds
// that were converted under REG-21/22.
class WireFixture final {
 public:
  td::unique_ptr<StealthTransportDecorator> transport;
  MockClock *clock{nullptr};
  td::ChainBufferWriter output_writer;
  td::ChainBufferWriter input_writer;
  td::ChainBufferReader input_reader;

  WireFixture() {
    auto clock_local = td::make_unique<MockClock>();
    clock = clock_local.get();
    auto r = StealthTransportDecorator::create(
        td::make_unique<ObfuscatedTransport>(static_cast<td::int16>(2), ProxySecret::from_raw(make_tls_secret())),
        make_config(), td::make_unique<DominantBinRng>(), std::move(clock_local));
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

void enqueue_flush(WireFixture &fixture) {
  fixture.transport->set_traffic_hint(TrafficHint::BulkData);
  td::BufferWriter writer(td::Slice(td::string(kPayloadSize, 'x')), fixture.transport->max_prepend_size(),
                          fixture.transport->max_append_size());
  fixture.transport->write(std::move(writer), false);
  fixture.transport->pre_flush_write(fixture.transport->get_shaping_wakeup());
}

td::string drain_output(td::ChainBufferWriter &output_writer) {
  auto output_reader = output_writer.extract_reader();
  output_reader.sync_with_writer();
  return output_reader.move_as_buffer_slice().as_slice().str();
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
    }
    LOG(FATAL) << "Unexpected TLS record sequence at flush " << flush << ", length=" << len;
  }
  CHECK(pos == lengths.size());
  return caps;
}

TEST(StreamTransportRecordSizeSequenceHardeningWire, DominantSkewHonorsAntiRepeatAcrossFlushes) {
  WireFixture fixture;

  constexpr size_t kFlushCount = 9;
  for (size_t i = 0; i < kFlushCount; i++) {
    enqueue_flush(fixture);
  }

  auto caps = decode_flush_caps(extract_tls_record_lengths(drain_output(fixture.output_writer)), kFlushCount);

  ASSERT_EQ((std::vector<td::int32>{900, 900, 1200, 900, 900, 1200, 900, 900, 1200}), caps);
}

TEST(StreamTransportRecordSizeSequenceHardeningWire, IdleResetClearsRepeatPressureWithoutReintroducingPrimer) {
  WireFixture fixture;

  for (size_t i = 0; i < 3; i++) {
    enqueue_flush(fixture);
  }
  fixture.clock->advance(0.2);
  for (size_t i = 0; i < 3; i++) {
    enqueue_flush(fixture);
  }

  auto caps = decode_flush_caps(extract_tls_record_lengths(drain_output(fixture.output_writer)), 6);

  ASSERT_EQ((std::vector<td::int32>{900, 900, 1200, 900, 900, 1200}), caps);
}

}  // namespace