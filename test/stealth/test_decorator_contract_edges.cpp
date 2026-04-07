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

namespace {

using td::mtproto::ProxySecret;
using td::mtproto::stealth::StealthConfig;
using td::mtproto::stealth::StealthTransportDecorator;
using td::mtproto::stealth::TrafficHint;
using td::mtproto::test::MockClock;
using td::mtproto::test::MockRng;
using td::mtproto::test::RecordingTransport;
using td::mtproto::TransportType;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

struct DecoratorFixture final {
  td::unique_ptr<StealthTransportDecorator> decorator;
  RecordingTransport *inner{nullptr};
  MockClock *clock{nullptr};
};

DecoratorFixture make_test_decorator(size_t capacity = 8, size_t high = 6, size_t low = 2,
                                     bool supports_tls_record_sizing = true) {
  MockRng rng(1);
  auto config = StealthConfig::default_config(rng);
  config.ring_capacity = capacity;
  config.high_watermark = high;
  config.low_watermark = low;

  auto inner = td::make_unique<RecordingTransport>();
  inner->supports_tls_record_sizing_result = supports_tls_record_sizing;
  auto *inner_ptr = inner.get();
  auto clock = td::make_unique<MockClock>();
  auto *clock_ptr = clock.get();
  auto decorator =
      StealthTransportDecorator::create(std::move(inner), config, td::make_unique<MockRng>(7), std::move(clock));
  CHECK(decorator.is_ok());
  return {decorator.move_as_ok(), inner_ptr, clock_ptr};
}

TEST(DecoratorContractEdges, DelegatesInitCanReadTypeAndPaddingStateVerbatim) {
  auto fixture = make_test_decorator();
  fixture.inner->support_quick_ack_result = false;
  fixture.inner->can_read_result = false;
  fixture.inner->use_random_padding_result = true;
  fixture.inner->max_prepend_size_result = 41;
  fixture.inner->max_append_size_result = 13;
  fixture.inner->type = TransportType{TransportType::Http, 5, ProxySecret::from_raw("example.com")};

  fixture.decorator->init(nullptr, nullptr);

  ASSERT_EQ(1, fixture.inner->init_calls);
  ASSERT_FALSE(fixture.decorator->support_quick_ack());
  ASSERT_FALSE(fixture.decorator->can_read());
  ASSERT_TRUE(fixture.decorator->use_random_padding());
  ASSERT_EQ(41u, fixture.decorator->max_prepend_size());
  ASSERT_EQ(13u, fixture.decorator->max_append_size());
  ASSERT_EQ(TransportType::Http, fixture.decorator->get_type().type);
  ASSERT_EQ(5, fixture.decorator->get_type().dc_id);
  ASSERT_EQ("example.com", fixture.decorator->get_type().secret.get_raw_secret().str());
}

TEST(DecoratorContractEdges, PendingHintQueuedDuringBlockedFlushDoesNotRebindOlderMessages) {
  auto fixture = make_test_decorator(4, 3, 1);

  fixture.decorator->set_traffic_hint(TrafficHint::Keepalive);
  fixture.decorator->write(make_test_buffer(17), false);
  fixture.decorator->write(make_test_buffer(19), true);

  fixture.inner->can_write_result = false;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  ASSERT_EQ(0, fixture.inner->write_calls);

  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(23), false);

  fixture.inner->can_write_result = true;
  fixture.decorator->pre_flush_write(fixture.clock->now());
  auto wakeup_at = fixture.decorator->get_shaping_wakeup();
  ASSERT_TRUE(wakeup_at > fixture.clock->now());
  fixture.decorator->pre_flush_write(wakeup_at);

  ASSERT_EQ(3, fixture.inner->write_calls);
  ASSERT_EQ(3u, fixture.inner->queued_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, fixture.inner->queued_hints[0]);
  ASSERT_EQ(TrafficHint::BulkData, fixture.inner->queued_hints[1]);
  ASSERT_EQ(TrafficHint::Unknown, fixture.inner->queued_hints[2]);
  ASSERT_EQ(3u, fixture.inner->written_payloads.size());
  ASSERT_EQ(td::string(17, 'x'), fixture.inner->written_payloads[0]);
  ASSERT_EQ(td::string(23, 'x'), fixture.inner->written_payloads[1]);
  ASSERT_EQ(td::string(19, 'x'), fixture.inner->written_payloads[2]);
  ASSERT_EQ(3u, fixture.inner->written_quick_acks.size());
  ASSERT_FALSE(fixture.inner->written_quick_acks[0]);
  ASSERT_FALSE(fixture.inner->written_quick_acks[1]);
  ASSERT_TRUE(fixture.inner->written_quick_acks[2]);
}

TEST(DecoratorContractEdges, SupportsTlsRecordSizingTracksInnerCapabilityWithoutCaching) {
  auto fixture = make_test_decorator(8, 6, 2, false);

  ASSERT_FALSE(fixture.decorator->supports_tls_record_sizing());
  ASSERT_TRUE(fixture.inner->max_tls_record_sizes.empty());

  fixture.inner->supports_tls_record_sizing_result = true;

  ASSERT_TRUE(fixture.decorator->supports_tls_record_sizing());

  fixture.decorator->set_max_tls_record_size(2048);
  fixture.decorator->set_traffic_hint(TrafficHint::BulkData);
  fixture.decorator->write(make_test_buffer(29), false);
  fixture.decorator->pre_flush_write(fixture.clock->now());

  ASSERT_EQ(1, fixture.inner->write_calls);
  ASSERT_EQ(2u, fixture.inner->max_tls_record_sizes.size());
  ASSERT_EQ(2048, fixture.inner->max_tls_record_sizes[0]);
  ASSERT_EQ(2048, fixture.inner->max_tls_record_sizes[1]);
}

}  // namespace