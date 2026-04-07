//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "test/stealth/MockRng.h"
#include "test/stealth/RecordingTransport.h"

#include "td/mtproto/stealth/StealthConfig.h"
#include "td/mtproto/stealth/StealthTransportDecorator.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::IClock;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

class ZeroOriginClock final : public IClock {
 public:
  double now() const final {
    return now_;
  }

  void advance(double seconds) {
    now_ += seconds;
  }

 private:
  double now_{0.0};
};

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  ZeroOriginClock *clock{nullptr};
};

DecoratorFixture make_test_decorator() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<ZeroOriginClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorWakeupEdgeCases, ZeroDeadlineFromRingIsNotTreatedAsEmptyWakeupSentinel) {
  auto fixture = make_test_decorator();
  fixture.inner->shaping_wakeup_result = 42.0;

  fixture.decorator->set_traffic_hint(td::mtproto::stealth::TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(64), false);

  ASSERT_EQ(0.0, fixture.clock->now());
  ASSERT_EQ(0.0, fixture.decorator->get_shaping_wakeup());

  fixture.clock->advance(0.001);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
}

}  // namespace
