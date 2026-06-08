// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/db/TQueue.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

td::TQueue::EventId push_event(td::TQueue &queue, td::TQueue::QueueId queue_id, td::Slice data, td::int32 expires_at,
                               td::int64 extra) {
  return queue.push(queue_id, data.str(), expires_at, extra, {}).move_as_ok();
}

void assert_raw_event(const std::pair<const td::TQueue::EventId, td::TQueue::RawEvent> &entry,
                      td::TQueue::EventId expected_id, td::Slice expected_data, td::int32 expected_expires_at,
                      td::int64 expected_extra) {
  ASSERT_EQ(expected_id, entry.first);
  ASSERT_EQ(expected_data.str(), entry.second.data);
  ASSERT_EQ(expected_expires_at, entry.second.expires_at);
  ASSERT_EQ(expected_extra, entry.second.extra);
}

}  // namespace

TEST(TQueueClear, returns_deleted_events_with_original_payloads_when_tail_is_kept) {
  auto queue = td::TQueue::create();
  constexpr td::TQueue::QueueId queue_id = 7;

  const auto first_id = push_event(*queue, queue_id, "first", 101, 11);
  const auto second_id = push_event(*queue, queue_id, "second", 202, 22);
  const auto third_id = push_event(*queue, queue_id, "third", 303, 33);

  const auto deleted_events = queue->clear(queue_id, 1);

  ASSERT_EQ(2u, deleted_events.size());
  auto deleted_it = deleted_events.begin();
  assert_raw_event(*deleted_it++, first_id, "first", 101, 11);
  assert_raw_event(*deleted_it++, second_id, "second", 202, 22);
  ASSERT_TRUE(deleted_it == deleted_events.end());

  ASSERT_EQ(1u, queue->get_size(queue_id));
  ASSERT_EQ(third_id, queue->get_head(queue_id));
  ASSERT_EQ(third_id.next().move_as_ok(), queue->get_tail(queue_id));
}

TEST(TQueueClear, returns_all_deleted_events_when_keep_count_is_zero) {
  auto queue = td::TQueue::create();
  constexpr td::TQueue::QueueId queue_id = 11;

  const auto first_id = push_event(*queue, queue_id, "alpha", 501, 44);
  const auto second_id = push_event(*queue, queue_id, "beta", 502, 55);

  const auto deleted_events = queue->clear(queue_id, 0);

  ASSERT_EQ(2u, deleted_events.size());
  auto deleted_it = deleted_events.begin();
  assert_raw_event(*deleted_it++, first_id, "alpha", 501, 44);
  assert_raw_event(*deleted_it++, second_id, "beta", 502, 55);
  ASSERT_TRUE(deleted_it == deleted_events.end());
  ASSERT_EQ(0u, queue->get_size(queue_id));
  ASSERT_EQ(queue->get_head(queue_id), queue->get_tail(queue_id));
}
