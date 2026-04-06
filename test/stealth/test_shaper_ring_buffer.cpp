//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/ShaperRingBuffer.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <vector>

namespace {

using td::mtproto::stealth::ShaperPendingWrite;
using td::mtproto::stealth::ShaperRingBuffer;
using td::mtproto::stealth::TrafficHint;

td::BufferWriter make_test_buffer(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'x')), 32, 0);
}

ShaperPendingWrite make_pending_write(size_t payload_size, double send_at, TrafficHint hint) {
  ShaperPendingWrite pending_write;
  pending_write.message = make_test_buffer(payload_size);
  pending_write.quick_ack = false;
  pending_write.send_at = send_at;
  pending_write.hint = hint;
  return pending_write;
}

TEST(ShaperRingBufferAdversarial, BlockedDrainPreservesCurrentHeadAndRemainingOrder) {
  ShaperRingBuffer ring(4);
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(17, 10.0, TrafficHint::Keepalive)));
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(19, 20.0, TrafficHint::BulkData)));
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(23, 30.0, TrafficHint::AuthHandshake)));

  std::vector<TrafficHint> drained_hints;
  bool should_continue = true;
  ring.drain_ready(100.0, [&](ShaperPendingWrite &pending_write) {
    if (!should_continue) {
      return false;
    }
    drained_hints.push_back(pending_write.hint);
    should_continue = false;
    return true;
  });

  ASSERT_EQ(1u, drained_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, drained_hints[0]);
  ASSERT_EQ(2u, ring.size());
  ASSERT_EQ(20.0, ring.earliest_deadline());

  ring.drain_ready(100.0, [&](ShaperPendingWrite &pending_write) {
    drained_hints.push_back(pending_write.hint);
    return true;
  });

  ASSERT_EQ(3u, drained_hints.size());
  ASSERT_EQ(TrafficHint::BulkData, drained_hints[1]);
  ASSERT_EQ(TrafficHint::AuthHandshake, drained_hints[2]);
  ASSERT_TRUE(ring.empty());
  ASSERT_EQ(0.0, ring.earliest_deadline());
}

TEST(ShaperRingBufferAdversarial, WrapAroundRetainsFifoOrderAndDeadlines) {
  ShaperRingBuffer ring(3);
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(29, 10.0, TrafficHint::Keepalive)));
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(31, 20.0, TrafficHint::BulkData)));
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(37, 30.0, TrafficHint::AuthHandshake)));

  std::vector<TrafficHint> drained_hints;
  ring.drain_ready(20.0, [&](ShaperPendingWrite &pending_write) {
    drained_hints.push_back(pending_write.hint);
    return true;
  });

  ASSERT_EQ(2u, drained_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, drained_hints[0]);
  ASSERT_EQ(TrafficHint::BulkData, drained_hints[1]);
  ASSERT_EQ(1u, ring.size());
  ASSERT_EQ(30.0, ring.earliest_deadline());

  ASSERT_TRUE(ring.try_enqueue(make_pending_write(41, 40.0, TrafficHint::Interactive)));
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(43, 50.0, TrafficHint::Unknown)));
  ASSERT_EQ(3u, ring.size());
  ASSERT_EQ(30.0, ring.earliest_deadline());

  ring.drain_ready(100.0, [&](ShaperPendingWrite &pending_write) {
    drained_hints.push_back(pending_write.hint);
    return true;
  });

  ASSERT_EQ(5u, drained_hints.size());
  ASSERT_EQ(TrafficHint::AuthHandshake, drained_hints[2]);
  ASSERT_EQ(TrafficHint::Interactive, drained_hints[3]);
  ASSERT_EQ(TrafficHint::Unknown, drained_hints[4]);
  ASSERT_TRUE(ring.empty());
}

TEST(ShaperRingBufferAdversarial, NotReadyHeadPreventsLaterItemsFromBypassingQueue) {
  ShaperRingBuffer ring(4);
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(47, 50.0, TrafficHint::Keepalive)));
  ASSERT_TRUE(ring.try_enqueue(make_pending_write(53, 10.0, TrafficHint::BulkData)));

  std::vector<TrafficHint> drained_hints;
  ring.drain_ready(25.0, [&](ShaperPendingWrite &pending_write) {
    drained_hints.push_back(pending_write.hint);
    return true;
  });

  ASSERT_TRUE(drained_hints.empty());
  ASSERT_EQ(2u, ring.size());
  ASSERT_EQ(50.0, ring.earliest_deadline());

  ring.drain_ready(100.0, [&](ShaperPendingWrite &pending_write) {
    drained_hints.push_back(pending_write.hint);
    return true;
  });

  ASSERT_EQ(2u, drained_hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, drained_hints[0]);
  ASSERT_EQ(TrafficHint::BulkData, drained_hints[1]);
}

}  // namespace