//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/TQueue.h"

#include "td/utils/Random.h"
#include "td/utils/VectorQueue.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"
#include "td/utils/tl_helpers.h"

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/BinlogInterface.h"
#include "td/db/binlog/BinlogHelper.h"

#include <unordered_map>

namespace td {

using EventId = TQueue::EventId;

EventId::EventId() {
}

Result<EventId> EventId::from_int32(int32 id) {
  if (!is_valid(id)) {
    return Status::Error("Invalid id");
  }
  return EventId(id);
}

EventId EventId::create_random() {
  return from_int32(Random::fast_uint32() % (MAX_ID / 2) + 10).move_as_ok();
}

int32 EventId::value() const {
  return id_;
}

Result<EventId> EventId::next() const {
  return from_int32(id_ + 1);
}

Result<EventId> EventId::advance(size_t offset) const {
  TRY_RESULT(new_id, narrow_cast_safe<int32>(id_ + offset));
  return from_int32(new_id);
}

bool EventId::empty() const {
  return id_ == 0;
}

bool EventId::operator==(const EventId &other) const {
  return id_ == other.id_;
}

StringBuilder &operator<<(StringBuilder &sb, const EventId id) {
  return sb << "EventId{" << id.value() << "}";
}

EventId::EventId(int32 id) : id_(id) {
  CHECK(is_valid(id));
}

bool EventId::is_valid(int32 id) {
  return 0 <= id && id < MAX_ID;
}

class TQueueImpl : public TQueue {
 public:
  static constexpr int32 MAX_DELAY = 7 * 86400;
  static constexpr size_t MAX_EVENT_LEN = 65536 * 8;

  void set_callback(unique_ptr<Callback> callback) override {
    callback_ = std::move(callback);
  }
  unique_ptr<Callback> extract_callback() override {
    return std::move(callback_);
  }

  void do_push(QueueId queue_id, RawEvent &&raw_event) override {
    CHECK(!raw_event.event_id.empty());
    if (callback_ && raw_event.logevent_id == 0) {
      raw_event.logevent_id = callback_->push(queue_id, raw_event);
    }
    auto &q = queues_[queue_id];
    q.tail_id = raw_event.event_id.next().move_as_ok();
    q.events.push(std::move(raw_event));
  }

  EventId push(QueueId queue_id, string data, double expire_at, EventId new_id = EventId()) override {
    auto &q = queues_[queue_id];
    EventId event_id;
    while (true) {
      if (q.events.empty()) {
        q.tail_id = new_id.empty() ? EventId::create_random() : new_id;
      }
      event_id = q.tail_id;
      CHECK(!event_id.empty());
      if (event_id.next().is_ok()) {
        break;
      }
      confirm_read(q, event_id);
    }

    RawEvent raw_event;
    raw_event.event_id = event_id;
    raw_event.data = data;
    raw_event.expire_at = expire_at;
    do_push(queue_id, std::move(raw_event));
    return event_id;
  }

  EventId get_head(QueueId queue_id) const override {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return EventId();
    }
    auto &q = it->second;
    if (q.events.empty()) {
      return EventId();
    }
    return q.events.front().event_id;
  }

  EventId get_tail(QueueId queue_id) const override {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return EventId();
    }
    auto &q = it->second;
    if (q.events.empty()) {
      return EventId();
    }
    return q.tail_id;
  }

  Result<size_t> get(QueueId queue_id, EventId from_id, double now, MutableSpan<Event> &events) override {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return 0;
    }
    auto &q = it->second;
    //TODO: sanity check for from_id
    confirm_read(q, from_id);
    if (q.events.empty()) {
      return 0;
    }

    auto from_events = q.events.as_span();
    size_t ready_n = 0;
    size_t left_n = 0;
    for (size_t i = 0; i < from_events.size(); i++) {
      auto &from = from_events[i];
      if (from.expire_at < now) {
        //TODO: pop this element
        continue;
      }

      auto &to = events[ready_n];
      to.data = from.data;
      to.id = from.event_id;
      to.expire_at = from.expire_at;

      ready_n++;
      if (ready_n == events.size()) {
        left_n += from_events.size() - i - 1;
        break;
      }
    }
    events.truncate(ready_n);
    return ready_n + left_n;
  }

 private:
  struct Queue {
    EventId tail_id;
    VectorQueue<RawEvent> events;
  };

  std::unordered_map<QueueId, Queue> queues_;
  unique_ptr<Callback> callback_;

  void confirm_read(Queue &q, EventId till_id) {
    while (!q.events.empty() && q.events.front().event_id.value() < till_id.value()) {
      if (callback_) {
        callback_->pop(q.events.front().logevent_id);
      }
      q.events.pop();
    }
  }
};
unique_ptr<TQueue> TQueue::create(unique_ptr<Callback> callback) {
  auto res = make_unique<TQueueImpl>();
  if (callback) {
    res->set_callback(std::move(callback));
  }
  return res;
}

struct TQueueLogEvent : public Storer {
  TQueueLogEvent() = default;
  int64 queue_id;
  int32 event_id;
  int32 expire_at;
  Slice data;

  template <class StorerT>
  void store(StorerT &&storer) const {
    using td::store;
    store(queue_id, storer);
    store(event_id, storer);
    store(expire_at, storer);
    store(data, storer);
  }

  template <class ParserT>
  void parse(ParserT &&parser) {
    using td::parse;
    parse(queue_id, parser);
    parse(event_id, parser);
    parse(expire_at, parser);
    data = parser.template fetch_string<Slice>();
  }

  size_t size() const override {
    TlStorerCalcLength storer;
    store(storer);
    return storer.get_length();
  }

  size_t store(uint8 *ptr) const override {
    TlStorerUnsafe storer(ptr);
    store(storer);
    return static_cast<size_t>(storer.get_buf() - ptr);
  }
};

template <class BinlogT>
int64 TQueueBinlog<BinlogT>::push(QueueId queue_id, const RawEvent &event) {
  TQueueLogEvent log_event;
  log_event.queue_id = queue_id;
  log_event.event_id = event.event_id.value();
  log_event.expire_at = static_cast<int32>(event.expire_at);
  log_event.data = event.data;
  return binlog_->add(magic_, log_event);
}

template <class BinlogT>
void TQueueBinlog<BinlogT>::pop(int64 logevent_id) {
  binlog_->erase(logevent_id);
}

template <class BinlogT>
Status TQueueBinlog<BinlogT>::replay(const BinlogEvent &binlog_event, TQueue &q) {
  TQueueLogEvent event;
  TlParser parser(binlog_event.data_);
  event.parse(parser);
  TRY_STATUS(parser.get_status());
  TRY_RESULT(event_id, EventId::from_int32(event.event_id));
  RawEvent raw_event;
  raw_event.logevent_id = binlog_event.id_;
  raw_event.event_id = event_id;
  raw_event.expire_at = event.expire_at;
  raw_event.data = event.data.str();
  q.do_push(event.queue_id, std::move(raw_event));
  return Status::OK();
}

template class TQueueBinlog<BinlogInterface>;
template class TQueueBinlog<Binlog>;

int64 MemoryStorage::push(QueueId queue_id, const RawEvent &event) {
  auto logevent_id = next_logevent_id_++;
  events_[logevent_id] = std::make_pair(queue_id, event);

  return logevent_id;
}
void MemoryStorage::pop(int64 logevent_id) {
  events_.erase(logevent_id);
}

void MemoryStorage::replay(TQueue &q) {
  for (auto e : events_) {
    auto x = e.second;
    x.second.logevent_id = e.first;
    q.do_push(x.first, std::move(x.second));
  }
}

}  // namespace td
