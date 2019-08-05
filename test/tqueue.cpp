//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/int_types.h"
#include "td/utils/Status.h"
#include "td/utils/Slice.h"
#include "td/utils/Span.h"
#include "td/utils/VectorQueue.h"
#include "td/utils/HashMap.h"
#include "td/utils/misc.h"
#include "td/utils/tests.h"

namespace td {
using EventId = int32;
using TQueueId = int64;
struct Event {
  EventId id;
  Slice data;
  double expire_at;
};

class TQueue {
 public:
  EventId push(TQueueId queue_id, Slice data, double expire_at) {
    auto &q = queues_[queue_id];
    if (q.events.empty()) {
      q.head_id = 1;
    }
    EventId event_id = narrow_cast<EventId>(q.head_id + q.events.size());
    q.events.push<RawEvent>({data.str(), expire_at});
    return event_id;
  }

  EventId get_head(TQueueId queue_id) {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return 0;
    }
    auto &q = it->second;
    if (q.events.empty()) {
      return 0;
    }
    return q.head_id;
  }

  Result<EventId> get_tail(TQueueId queue_id) {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return 0;
    }
    auto &q = it->second;
    if (q.events.empty()) {
      return 0;
    }
    return narrow_cast<EventId>(q.head_id + q.events.size() - 1);
  }

  Result<size_t> get(TQueueId queue_id, EventId from_id, double now, MutableSpan<Event> events) {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return 0;
    }
    auto &q = it->second;
    while (!q.events.empty() && q.head_id < from_id) {
      q.head_id++;
      q.events.pop();
    }
    if (q.events.empty()) {
      return 0;
    }

    auto from_events = q.events.as_span();
    size_t res_n = 0;
    for (size_t i = 0; i < from_events.size(); i++) {
      auto &from = from_events[i];
      if (from.expire_at < now) {
        continue;
      }

      auto &to = events[res_n];
      to.data = from.data;
      to.id = narrow_cast<EventId>(q.head_id + i);
      to.expire_at = from.expire_at;

      res_n++;
      if (res_n == events.size()) {
        break;
      }
    }
    return res_n;
  }

  struct RawEvent {
    string data;
    double expire_at{0};
  };

  struct Queue {
    EventId head_id{0};
    VectorQueue<RawEvent> events;
  };

  HashMap<TQueueId, Queue> queues_;
};

TEST(TQueue, hands) {
  Event events[100];
  auto events_span = MutableSpan<Event>(events, 100);

  TQueue tqueue;
  auto qid = 12;
  ASSERT_EQ(0, tqueue.get_head(qid));
  ASSERT_EQ(0, tqueue.get_tail(qid));
  tqueue.push(qid, "hello", 0);
  auto head = tqueue.get_head(qid);
  ASSERT_EQ(head, tqueue.get_tail(qid));
  ASSERT_EQ(1, tqueue.get(qid, head, 0, events_span).move_as_ok());
}

}  // namespace td
