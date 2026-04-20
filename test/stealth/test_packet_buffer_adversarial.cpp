// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
// Adversarial and edge-case tests for the packet-write ring buffer.
// Threat model: the buffer must maintain FIFO order, capacity invariants, and
// deterministic deadline behavior regardless of extreme constructor arguments or
// degenerate timestamp values.

#include "td/mtproto/stealth/ShaperRingBuffer.h"

#include "td/utils/buffer.h"
#include "td/utils/tests.h"

#include <cmath>
#include <limits>
#include <vector>

namespace {

using td::mtproto::stealth::ShaperPendingWrite;
using td::mtproto::stealth::ShaperRingBuffer;
using td::mtproto::stealth::TrafficHint;

td::BufferWriter make_test_buf(size_t size) {
  return td::BufferWriter(td::Slice(td::string(size, 'a')), 0, 0);
}

ShaperPendingWrite make_write(size_t size, double send_at, TrafficHint hint = TrafficHint::Unknown) {
  ShaperPendingWrite pw;
  pw.message = make_test_buf(size);
  pw.send_at = send_at;
  pw.hint = hint;
  return pw;
}

// ─── Capacity edge cases ─────────────────────────────────────────────────

TEST(PacketBuffer, CapacityOneAcceptsSingleItemAndBlocksSecond) {
  ShaperRingBuffer ring(1);
  ASSERT_TRUE(ring.try_enqueue(make_write(1, 1.0)));
  ASSERT_FALSE(ring.try_enqueue(make_write(2, 2.0)));
  ASSERT_EQ(1u, ring.size());
}

TEST(PacketBuffer, CapacityZeroConstructedAsSingleSlot) {
  // Constructor maps capacity=0 to 1 to avoid degenerate state.
  ShaperRingBuffer ring(0);
  ASSERT_TRUE(ring.try_enqueue(make_write(1, 1.0)));
  ASSERT_EQ(1u, ring.size());
  ASSERT_FALSE(ring.empty());
}

TEST(PacketBuffer, DefaultCapacityIsThirtyTwo) {
  ShaperRingBuffer ring;
  for (size_t i = 0; i < 32; i++) {
    ASSERT_TRUE(ring.try_enqueue(make_write(i + 1, static_cast<double>(i))));
  }
  ASSERT_FALSE(ring.try_enqueue(make_write(33, 100.0)));
  ASSERT_EQ(32u, ring.size());
}

// ─── FIFO ordering invariants ─────────────────────────────────────────────

TEST(PacketBuffer, FifoOrderMaintainedAfterWrapAround) {
  ShaperRingBuffer ring(3);
  // Fill all 3 slots.
  ASSERT_TRUE(ring.try_enqueue(make_write(10, 1.0, TrafficHint::Keepalive)));
  ASSERT_TRUE(ring.try_enqueue(make_write(20, 2.0, TrafficHint::BulkData)));
  ASSERT_TRUE(ring.try_enqueue(make_write(30, 3.0, TrafficHint::Interactive)));

  std::vector<TrafficHint> hints;
  // Drain first 2 entries.
  ring.drain_ready(2.5, [&](ShaperPendingWrite &pw) {
    hints.push_back(pw.hint);
    return true;
  });
  ASSERT_EQ(2u, hints.size());
  ASSERT_EQ(TrafficHint::Keepalive, hints[0]);
  ASSERT_EQ(TrafficHint::BulkData, hints[1]);

  // Add 2 new entries causing wrap-around.
  ASSERT_TRUE(ring.try_enqueue(make_write(40, 4.0, TrafficHint::AuthHandshake)));
  ASSERT_TRUE(ring.try_enqueue(make_write(50, 5.0, TrafficHint::Unknown)));

  ring.drain_ready(10.0, [&](ShaperPendingWrite &pw) {
    hints.push_back(pw.hint);
    return true;
  });
  ASSERT_EQ(5u, hints.size());
  ASSERT_EQ(TrafficHint::Interactive, hints[2]);
  ASSERT_EQ(TrafficHint::AuthHandshake, hints[3]);
  ASSERT_EQ(TrafficHint::Unknown, hints[4]);
  ASSERT_TRUE(ring.empty());
}

// ─── Drain callback interaction ───────────────────────────────────────────

TEST(PacketBuffer, DrainStopsWhenCallbackReturnsFalse) {
  ShaperRingBuffer ring(5);
  for (int i = 0; i < 5; i++) {
    ASSERT_TRUE(ring.try_enqueue(make_write(i + 1, static_cast<double>(i), TrafficHint::Keepalive)));
  }

  int drained = 0;
  ring.drain_ready(100.0, [&](ShaperPendingWrite &) {
    drained++;
    return drained < 3;
  });
  ASSERT_EQ(3, drained);
  ASSERT_EQ(3u, ring.size());
}

TEST(PacketBuffer, DrainIdempotentOnEmptyBuffer) {
  ShaperRingBuffer ring(4);
  int calls = 0;
  ring.drain_ready(100.0, [&](ShaperPendingWrite &) {
    calls++;
    return true;
  });
  ASSERT_EQ(0, calls);
  ASSERT_TRUE(ring.empty());
}

// ─── earliest_deadline semantics ──────────────────────────────────────────

TEST(PacketBuffer, EarliestDeadlineIsZeroWhenEmpty) {
  ShaperRingBuffer ring(4);
  ASSERT_EQ(0.0, ring.earliest_deadline());
}

TEST(PacketBuffer, EarliestDeadlineReflectsHeadElementSendAt) {
  ShaperRingBuffer ring(4);
  ASSERT_TRUE(ring.try_enqueue(make_write(1, 99.0)));
  ASSERT_TRUE(ring.try_enqueue(make_write(2, 1.0)));  // later insert, earlier? no - FIFO
  // earliest_deadline returns the head's send_at, not the minimum.
  ASSERT_EQ(99.0, ring.earliest_deadline());
}

TEST(PacketBuffer, EarliestDeadlineUpdatesAfterDrain) {
  ShaperRingBuffer ring(4);
  ASSERT_TRUE(ring.try_enqueue(make_write(10, 10.0)));
  ASSERT_TRUE(ring.try_enqueue(make_write(20, 20.0)));

  ring.drain_ready(15.0, [](ShaperPendingWrite &) { return true; });

  ASSERT_EQ(1u, ring.size());
  ASSERT_EQ(20.0, ring.earliest_deadline());
}

// ─── Extreme send_at values ───────────────────────────────────────────────

TEST(PacketBuffer, VeryLargeSendAtDoesNotDrainPrematurely) {
  constexpr double huge = std::numeric_limits<double>::max() / 2.0;
  ShaperRingBuffer ring(3);
  ASSERT_TRUE(ring.try_enqueue(make_write(1, huge)));
  ASSERT_TRUE(ring.try_enqueue(make_write(2, 1.0)));

  // drain at t=1e15 — should not drain the huge-deadline head first (FIFO blocked).
  int drained = 0;
  ring.drain_ready(1e15, [&](ShaperPendingWrite &) {
    drained++;
    return true;
  });
  // head has deadline=huge > 1e15, so nothing is drained.
  ASSERT_EQ(0, drained);
  ASSERT_EQ(2u, ring.size());
}

TEST(PacketBuffer, ZeroSendAtItemDrainsImmediately) {
  ShaperRingBuffer ring(2);
  ASSERT_TRUE(ring.try_enqueue(make_write(1, 0.0)));

  int drained = 0;
  ring.drain_ready(0.0, [&](ShaperPendingWrite &) {
    drained++;
    return true;
  });
  ASSERT_EQ(1, drained);
  ASSERT_TRUE(ring.empty());
}

TEST(PacketBuffer, NegativeSendAtItemDrainsAtAnyNonNegativeNow) {
  ShaperRingBuffer ring(2);
  ASSERT_TRUE(ring.try_enqueue(make_write(1, -100.0)));

  int drained = 0;
  ring.drain_ready(0.0, [&](ShaperPendingWrite &) {
    drained++;
    return true;
  });
  ASSERT_EQ(1, drained);
  ASSERT_TRUE(ring.empty());
}

// ─── for_each correctness ─────────────────────────────────────────────────

TEST(PacketBuffer, ForEachVisitsAllLiveItemsInOrder) {
  ShaperRingBuffer ring(4);
  ASSERT_TRUE(ring.try_enqueue(make_write(11, 1.0, TrafficHint::Keepalive)));
  ASSERT_TRUE(ring.try_enqueue(make_write(22, 2.0, TrafficHint::BulkData)));
  ASSERT_TRUE(ring.try_enqueue(make_write(33, 3.0, TrafficHint::Interactive)));

  std::vector<size_t> visited_sizes;
  ring.for_each([&](const ShaperPendingWrite &pw) { visited_sizes.push_back(pw.message.as_buffer_slice().size()); });

  ASSERT_EQ(3u, visited_sizes.size());
  ASSERT_EQ(11u, visited_sizes[0]);
  ASSERT_EQ(22u, visited_sizes[1]);
  ASSERT_EQ(33u, visited_sizes[2]);
}

TEST(PacketBuffer, ForEachOnEmptyBufferVisitsNothing) {
  ShaperRingBuffer ring(4);
  int visits = 0;
  ring.for_each([&](const ShaperPendingWrite &) { visits++; });
  ASSERT_EQ(0, visits);
}

// ─── Full-cycle stress: repeated fill-and-drain ───────────────────────────

TEST(PacketBuffer, FullCycleRepeatPreservesCorrectness) {
  ShaperRingBuffer ring(4);
  size_t total_drained = 0;
  size_t expected_size = 1;

  for (int round = 0; round < 64; round++) {
    for (size_t i = 0; i < 4; i++) {
      ASSERT_TRUE(ring.try_enqueue(make_write(expected_size++, static_cast<double>(round * 10 + i))));
    }
    ASSERT_EQ(4u, ring.size());
    ASSERT_FALSE(ring.try_enqueue(make_write(0, 9999.0)));

    ring.drain_ready(static_cast<double>(round * 10 + 10), [&](ShaperPendingWrite &) {
      total_drained++;
      return true;
    });
    ASSERT_TRUE(ring.empty());
  }
  ASSERT_EQ(256u, total_drained);
}

}  // namespace
