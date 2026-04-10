// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "test/stealth/MockClock.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::stealth::DrsPhaseModel;
using td::mtproto::stealth::GreetingCamouflagePolicy;
using td::mtproto::stealth::IRng;
using td::mtproto::stealth::RecordSizeBin;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::RecordingTransport;

class SequenceRng final : public IRng {
 public:
  explicit SequenceRng(std::initializer_list<td::uint32> bounded_values) : bounded_values_(bounded_values) {
    CHECK(!bounded_values_.empty());
  }

  void fill_secure_bytes(td::MutableSlice dest) final {
    dest.fill('\0');
  }

  td::uint32 secure_uint32() final {
    return 0;
  }

  td::uint32 bounded(td::uint32 n) final {
    CHECK(n != 0);
    auto value = bounded_values_[index_ % bounded_values_.size()];
    index_++;
    return value % n;
  }

 private:
  std::vector<td::uint32> bounded_values_;
  size_t index_{0};
};

DrsPhaseModel make_phase(std::initializer_list<RecordSizeBin> bins, td::int32 max_repeat_run) {
  DrsPhaseModel phase;
  phase.bins.assign(bins.begin(), bins.end());
  phase.max_repeat_run = max_repeat_run;
  phase.local_jitter = 0;
  return phase;
}

td::BufferWriter make_test_buffer(td::Slice payload) {
  return td::BufferWriter(payload, 32, 0);
}

std::vector<td::int32> emitted_padding_targets(const std::vector<td::int32> &targets) {
  std::vector<td::int32> result;
  result.reserve(targets.size() / 2);
  for (size_t i = 1; i < targets.size(); i += 2) {
    result.push_back(targets[i]);
  }
  return result;
}

StealthConfig make_config() {
  SequenceRng rng({0, 0, 0, 1, 0, 1, 0, 1});
  auto config = StealthConfig::default_config(rng);
  config.drs_policy.slow_start = make_phase({{320, 320, 1}, {1400, 1400, 1}}, 1);
  config.drs_policy.congestion_open = config.drs_policy.slow_start;
  config.drs_policy.steady_state = config.drs_policy.slow_start;
  config.drs_policy.slow_start_records = 16;
  config.drs_policy.congestion_bytes = 1 << 20;
  config.drs_policy.idle_reset_ms_min = 1000;
  config.drs_policy.idle_reset_ms_max = 1000;
  config.drs_policy.min_payload_cap = 256;
  config.drs_policy.max_payload_cap = 1400;

  config.ipt_params.burst_mu_ms = 0.0;
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 1.0;
  config.ipt_params.idle_alpha = 1.0;
  config.ipt_params.idle_scale_ms = 1.0;
  config.ipt_params.idle_max_ms = 2.0;
  config.ipt_params.p_burst_stay = 0.0;
  config.ipt_params.p_idle_to_burst = 0.0;

  GreetingCamouflagePolicy greeting_policy;
  greeting_policy.greeting_record_count = 2;
  greeting_policy.record_models[0] = make_phase({{320, 320, 1}}, 64);
  greeting_policy.record_models[1] = make_phase({{320, 320, 1}}, 64);
  config.greeting_camouflage_policy = greeting_policy;
  return config;
}

struct Fixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static Fixture create() {
    auto inner = td::make_unique<RecordingTransport>();
    inner->supports_tls_record_sizing_result = true;
    auto *inner_ptr = inner.get();

    auto clock = td::make_unique<MockClock>();
    auto *clock_ptr = clock.get();

    auto decorator = StealthTransportDecorator::create(
        std::move(inner), make_config(),
        td::make_unique<SequenceRng>(std::initializer_list<td::uint32>{0, 0, 0, 1, 0, 1, 0, 1}), std::move(clock));
    CHECK(decorator.is_ok());
    return {decorator.move_as_ok(), inner_ptr, clock_ptr};
  }

  void flush_until_write_count(int expected_writes) {
    while (inner->write_calls < expected_writes) {
      decorator->pre_flush_write(clock->now());
      if (inner->write_calls >= expected_writes) {
        break;
      }
      auto wakeup = decorator->get_shaping_wakeup();
      ASSERT_TRUE(wakeup > clock->now());
      clock->advance(wakeup - clock->now());
    }
  }
};

TEST(PostHandshakeGreetingRepeatedPrimeIntegration,
     TwoRepeatedGreetingRecordsPrimeDrsAwayFromImmediateThirdRepeatInSameFlushWindow) {
  auto fixture = Fixture::create();

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("first"), true);
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("second"), false);
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer("third"), true);

  fixture.flush_until_write_count(3);

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_TRUE(fixture.inner->stealth_record_padding_targets.size() >= 6u);
  ASSERT_EQ((std::vector<td::int32>{320, 320, 1400}),
            emitted_padding_targets(fixture.inner->stealth_record_padding_targets));
}

}  // namespace