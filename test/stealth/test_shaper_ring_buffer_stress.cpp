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

TEST(ShaperRingBufferStress, RepeatedWrapAroundCyclesPreserveFifoOrder) {
  ShaperRingBuffer ring(5);
  std::vector<size_t> drained_sizes;
  std::vector<size_t> expected_sizes;
  size_t next_payload_size = 101;

  for (size_t round = 0; round < 24; round++) {
    auto base_deadline = static_cast<double>(round * 100);

    for (size_t i = 0; i < 5; i++) {
      expected_sizes.push_back(next_payload_size);
      ASSERT_TRUE(ring.try_enqueue(
          make_pending_write(next_payload_size++, base_deadline + static_cast<double>(i), TrafficHint::Unknown)));
    }
    ASSERT_FALSE(ring.try_enqueue(make_pending_write(9999, base_deadline + 99.0, TrafficHint::Unknown)));

    ring.drain_ready(base_deadline + 1.5, [&](ShaperPendingWrite &pending_write) {
      drained_sizes.push_back(pending_write.message.as_buffer_slice().size());
      return true;
    });

    ASSERT_EQ(3u, ring.size());

    for (size_t i = 0; i < 2; i++) {
      expected_sizes.push_back(next_payload_size);
      ASSERT_TRUE(ring.try_enqueue(make_pending_write(
          next_payload_size++, base_deadline + 10.0 + static_cast<double>(i), TrafficHint::Interactive)));
    }

    ring.drain_ready(base_deadline + 50.0, [&](ShaperPendingWrite &pending_write) {
      drained_sizes.push_back(pending_write.message.as_buffer_slice().size());
      return true;
    });

    ASSERT_TRUE(ring.empty());
    ASSERT_EQ(0.0, ring.earliest_deadline());
  }

  ASSERT_EQ(expected_sizes.size(), drained_sizes.size());
  ASSERT_EQ(expected_sizes, drained_sizes);
}

}  // namespace