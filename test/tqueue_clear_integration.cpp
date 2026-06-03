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

}  // namespace

TEST(TQueueClear, returns_deleted_events_with_original_payloads_when_tail_is_kept) {
  auto queue = td::TQueue::create();
  constexpr td::TQueue::QueueId queue_id = 7;

  const auto first_id = push_event(*queue, queue_id, "first", 101, 11);
  const auto second_id = push_event(*queue, queue_id, "second", 202, 22);
  const auto third_id = push_event(*queue, queue_id, "third", 303, 33);

  const auto deleted_events = queue->clear(queue_id, 1);

  ASSERT_EQ(2u, deleted_events.size());
  auto first_it = deleted_events.find(first_id);
  ASSERT_TRUE(first_it != deleted_events.end());
  ASSERT_EQ("first", first_it->second.data);
  ASSERT_EQ(101, first_it->second.expires_at);
  ASSERT_EQ(11, first_it->second.extra);

  auto second_it = deleted_events.find(second_id);
  ASSERT_TRUE(second_it != deleted_events.end());
  ASSERT_EQ("second", second_it->second.data);
  ASSERT_EQ(202, second_it->second.expires_at);
  ASSERT_EQ(22, second_it->second.extra);

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
  ASSERT_EQ("alpha", deleted_events.at(first_id).data);
  ASSERT_EQ("beta", deleted_events.at(second_id).data);
  ASSERT_EQ(0u, queue->get_size(queue_id));
  ASSERT_EQ(queue->get_head(queue_id), queue->get_tail(queue_id));
}