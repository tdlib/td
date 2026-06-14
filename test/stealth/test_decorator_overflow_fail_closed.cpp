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

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

namespace {

using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

struct OverflowHarness final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};

  static OverflowHarness make(size_t ring_capacity) {
    MockRng config_rng(1);
    auto config = StealthConfig::default_config(config_rng);
    config.ring_capacity = ring_capacity;
    config.high_watermark = 1;
    config.low_watermark = 0;
    config.chaff_policy.enabled = false;
    config.greeting_camouflage_policy.greeting_record_count = 0;
    config.bidirectional_correlation_policy.enabled = false;

    OverflowHarness harness;
    auto inner = td::make_unique<RecordingTransport>();
    harness.inner = inner.get();
    auto clock = td::make_unique<MockClock>();
    harness.clock = clock.get();

    auto decorator_result = StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7),
                                                              std::move(clock));
    CHECK(decorator_result.is_ok());
    harness.decorator = decorator_result.move_as_ok();
    return harness;
  }
};

// CONTRACT: once the combined ring capacity is exceeded, the decorator must
// fail closed like the higher-level emulate_tls activation gate: stop accepting
// writes, refuse to flush queued plaintext, and surface a transport error on
// read instead of terminating the whole host process.
TEST(DecoratorOverflowFailClosed, OverflowTurnsTransportIntoTerminalErrorState) {
  auto harness = OverflowHarness::make(/*ring_capacity=*/2);

  harness.inner->can_write_result = false;
  harness.decorator->write(make_test_buffer(17), false);
  harness.decorator->write(make_test_buffer(19), false);

  // This is the red path under the historical behavior: it aborted the process.
  harness.decorator->write(make_test_buffer(23), false);

  ASSERT_FALSE(harness.decorator->can_write());
  ASSERT_TRUE(harness.decorator->can_read());

  td::BufferSlice packet;
  td::uint32 quick_ack = 0;
  auto result = harness.decorator->read_next(&packet, &quick_ack);
  ASSERT_TRUE(result.is_error());
  ASSERT_TRUE(result.error().message().str().find("ring overflow") != td::string::npos);
}

// Black-hat scenario: after the overflow trigger fires, no queued data may
// continue draining to the inner transport. Emitting already-buffered MTProto
// after the decorator declared its state invalid would violate fail-closed.
TEST(DecoratorOverflowFailClosed, OverflowPreventsQueuedWritesFromReachingInnerTransport) {
  auto harness = OverflowHarness::make(/*ring_capacity=*/2);

  harness.inner->can_write_result = false;
  harness.decorator->write(make_test_buffer(29), false);
  harness.decorator->write(make_test_buffer(31), false);
  harness.decorator->write(make_test_buffer(37), false);

  harness.inner->can_write_result = true;
  harness.decorator->pre_flush_write(harness.clock->now());

  ASSERT_EQ(0, harness.inner->write_calls);
}

}  // namespace
