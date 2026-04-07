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

#include <limits>

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

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator() {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);

  auto inner = td::make_unique<RecordingTransport>();
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorRecordSizingAdversarial, ClampsHostileLowOverrideBeforeForwardingToInnerTransport) {
  auto fixture = make_test_decorator();
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_max_tls_record_size(std::numeric_limits<td::int32>::min());

  ASSERT_EQ(256, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(67), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(256, fixture.inner->max_tls_record_sizes.back());
}

TEST(DecoratorRecordSizingAdversarial, ClampsHostileHighOverrideBeforeForwardingToInnerTransport) {
  auto fixture = make_test_decorator();
  ASSERT_FALSE(fixture.inner->max_tls_record_sizes.empty());

  fixture.decorator->set_max_tls_record_size(std::numeric_limits<td::int32>::max());

  ASSERT_EQ(16384, fixture.inner->max_tls_record_sizes.back());

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(79), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(16384, fixture.inner->max_tls_record_sizes.back());
}

}  // namespace