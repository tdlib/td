//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockClock.h"
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <cmath>

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

struct TinyDelayFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

TinyDelayFixture make_tiny_delay_fixture() {
  MockRng rng(11);
  auto config = StealthConfig::default_config(rng);
  config.ring_capacity = 8;
  config.high_watermark = 6;
  config.low_watermark = 2;
  config.ipt_params.burst_mu_ms = std::log(0.0004);
  config.ipt_params.burst_sigma = 0.0;
  config.ipt_params.burst_max_ms = 0.0004;
  config.ipt_params.idle_alpha = 2.0;
  config.ipt_params.idle_scale_ms = 10.0;
  config.ipt_params.idle_max_ms = 20.0;
  config.ipt_params.p_burst_stay = 1.0;
  config.ipt_params.p_idle_to_burst = 1.0;

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorIptPrecisionAdversarial, TinyPositiveInteractiveDelayStaysScheduledInsteadOfBypassing) {
  auto fixture = make_tiny_delay_fixture();

  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(29), false);
  fixture.decorator->set_traffic_hint(TrafficHint::Interactive);
  fixture.decorator->write(make_test_buffer(31), false);

  ASSERT_EQ(fixture.clock->now(), fixture.decorator->get_shaping_wakeup());

  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(1u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[0]);

  auto wakeup = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup > fixture.clock->now());

  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(1, fixture.inner->write_calls);

  fixture.decorator->pre_flush_write(wakeup);
  ASSERT_EQ(2, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Interactive, fixture.inner->queued_hints[1]);
}

}  // namespace