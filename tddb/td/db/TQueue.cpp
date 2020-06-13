//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/TQueue.h"

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/binlog/BinlogInterface.h"

#include "td/utils/format.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Random.h"
#include "td/utils/StorerBase.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"
#include "td/utils/VectorQueue.h"

#include <algorithm>
#include <unordered_map>

namespace td {

using EventId = TQueue::EventId;

EventId::EventId() {
}

Result<EventId> EventId::from_int32(int32 id) {
  if (!is_valid_id(id)) {
    return Status::Error("Invalid ID");
  }
  return EventId(id);
}

bool EventId::is_valid() const {
  return !empty() && is_valid_id(id_);
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

bool EventId::operator!=(const EventId &other) const {
  return !(*this == other);
}

bool EventId::operator<(const EventId &other) const {
  return id_ < other.id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const EventId id) {
  return string_builder << "EventId{" << id.value() << "}";
}

EventId::EventId(int32 id) : id_(id) {
  CHECK(is_valid_id(id));
}

bool EventId::is_valid_id(int32 id) {
  return 0 <= id && id < MAX_ID;
}

class TQueueImpl : public TQueue {
  static constexpr size_t MAX_EVENT_LEN = 65536 * 8;
  static constexpr size_t MAX_QUEUE_EVENTS = 1000000;

 public:
  void set_callback(unique_ptr<StorageCallback> callback) override {
    callback_ = std::move(callback);
  }
  unique_ptr<StorageCallback> extract_callback() override {
    return std::move(callback_);
  }

  bool do_push(QueueId queue_id, RawEvent &&raw_event) override {
    CHECK(raw_event.event_id.is_valid());
    auto &q = queues_[queue_id];
    if (q.events.empty() || q.events.back().event_id < raw_event.event_id) {
      if (raw_event.logevent_id == 0 && callback_ != nullptr) {
        raw_event.logevent_id = callback_->push(queue_id, raw_event);
      }
      q.tail_id = raw_event.event_id.next().move_as_ok();
      q.events.push(std::move(raw_event));
      return true;
    }
    return false;
  }

  Result<EventId> push(QueueId queue_id, string data, double expires_at, int64 extra, EventId hint_new_id) override {
    auto &q = queues_[queue_id];
    if (q.events.size() >= MAX_QUEUE_EVENTS) {
      return Status::Error("Queue is full");
    }
    if (data.empty()) {
      return Status::Error("Data is empty");
    }
    if (data.size() > MAX_EVENT_LEN) {
      return Status::Error("Data is too big");
    }
    EventId event_id;
    while (true) {
      if (q.tail_id.empty()) {
        if (hint_new_id.empty()) {
          q.tail_id = EventId::from_int32(Random::fast(2 * MAX_QUEUE_EVENTS + 1, EventId::MAX_ID / 2)).move_as_ok();
        } else {
          q.tail_id = hint_new_id;
        }
      }
      event_id = q.tail_id;
      CHECK(event_id.is_valid());
      if (event_id.next().is_ok()) {
        break;
      }
      for (auto &event : q.events.as_mutable_span()) {
        pop(queue_id, event, {});
      }
      q.tail_id = EventId();
      q.events = {};
      CHECK(hint_new_id.next().is_ok());
    }

    RawEvent raw_event;
    raw_event.event_id = event_id;
    raw_event.data = std::move(data);
    raw_event.expires_at = expires_at;
    raw_event.extra = extra;
    bool is_added = do_push(queue_id, std::move(raw_event));
    CHECK(is_added);
    return event_id;
  }

  EventId get_head(QueueId queue_id) const override {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return EventId();
    }
    auto &q = it->second;
    if (q.events.empty()) {
      return q.tail_id;
    }
    return q.events.front().event_id;
  }

  EventId get_tail(QueueId queue_id) const override {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      return EventId();
    }
    auto &q = it->second;
    return q.tail_id;
  }

  void forget(QueueId queue_id, EventId event_id) override {
    auto q_it = queues_.find(queue_id);
    if (q_it == queues_.end()) {
      return;
    }
    auto &q = q_it->second;
    auto from_events = q.events.as_mutable_span();
    auto it = std::lower_bound(from_events.begin(), from_events.end(), event_id,
                               [](auto &event, EventId event_id) { return event.event_id < event_id; });
    if (it == from_events.end() || !(it->event_id == event_id)) {
      return;
    }
    pop(queue_id, *it, q.tail_id);
  }

  Result<size_t> get(QueueId queue_id, EventId from_id, bool forget_previous, double now,
                     MutableSpan<Event> &result_events) override {
    auto it = queues_.find(queue_id);
    if (it == queues_.end()) {
      result_events.truncate(0);
      return 0;
    }
    auto &q = it->second;
    // Some sanity checks
    if (from_id.value() > q.tail_id.value() + 10) {
      return Status::Error("Specified from_id is in the future");
    }
    if (from_id.value() < q.tail_id.value() - static_cast<int32>(MAX_QUEUE_EVENTS) * 2) {
      return Status::Error("Specified from_id is in the past");
    }

    MutableSpan<RawEvent> from_events;
    size_t ready_n = 0;
    size_t i = 0;

    while (true) {
      from_events = q.events.as_mutable_span();
      ready_n = 0;
      size_t first_i = 0;
      if (!forget_previous) {
        first_i = std::lower_bound(from_events.begin(), from_events.end(), from_id,
                                   [](auto &event, EventId event_id) { return event.event_id < event_id; }) -
                  from_events.begin();
      }
      for (i = first_i; i < from_events.size(); i++) {
        auto &from = from_events[i];
        try_pop(queue_id, from, forget_previous ? from_id : EventId{}, q.tail_id, now);
        if (from.data.empty()) {
          continue;
        }

        if (ready_n == result_events.size()) {
          break;
        }

        CHECK(!(from.event_id < from_id));

        auto &to = result_events[ready_n];
        to.data = from.data;
        to.id = from.event_id;
        to.expires_at = from.expires_at;
        to.extra = from.extra;
        ready_n++;
      }

      // compactify skipped events
      if ((ready_n + 1) * 2 < i + first_i) {
        compactify(q.events, i);
        continue;
      }

      break;
    }

    result_events.truncate(ready_n);
    size_t left_n = from_events.size() - i;
    return ready_n + left_n;
  }

  void run_gc(double now) override {
    for (auto &it : queues_) {
      for (auto &e : it.second.events.as_mutable_span()) {
        try_pop(it.first, e, EventId(), it.second.tail_id, now);
      }
    }
  }

 private:
  struct Queue {
    EventId tail_id;
    VectorQueue<RawEvent> events;
  };

  std::unordered_map<QueueId, Queue> queues_;
  unique_ptr<StorageCallback> callback_;

  static void compactify(VectorQueue<RawEvent> &events, size_t prefix) {
    if (prefix == events.size()) {
      CHECK(!events.empty());
      prefix--;
    }
    auto processed = events.as_mutable_span().substr(0, prefix);
    auto removed_n =
        processed.rend() - std::remove_if(processed.rbegin(), processed.rend(), [](auto &e) { return e.data.empty(); });
    events.pop_n(removed_n);
  }

  void try_pop(QueueId queue_id, RawEvent &event, EventId from_id, EventId tail_id, double now) {
    if (event.expires_at < now || event.event_id < from_id || event.data.empty()) {
      pop(queue_id, event, tail_id);
    }
  }

  void pop(QueueId queue_id, RawEvent &event, EventId tail_id) {
    if (callback_ == nullptr || event.logevent_id == 0) {
      event.logevent_id = 0;
      event.data = {};
      return;
    }

    if (event.event_id.next().ok() == tail_id) {
      if (!event.data.empty()) {
        event.data = {};
        callback_->push(queue_id, event);
      }
    } else {
      callback_->pop(event.logevent_id);
      event.logevent_id = 0;
      event.data = {};
    }
  }
};

unique_ptr<TQueue> TQueue::create() {
  return make_unique<TQueueImpl>();
}

struct TQueueLogEvent : public Storer {
  int64 queue_id;
  int32 event_id;
  int32 expires_at;
  Slice data;
  int64 extra;

  template <class StorerT>
  void store(StorerT &&storer) const {
    using td::store;
    store(queue_id, storer);
    store(event_id, storer);
    store(expires_at, storer);
    store(data, storer);
    if (extra != 0) {
      store(extra, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &&parser, int32 has_extra) {
    using td::parse;
    parse(queue_id, parser);
    parse(event_id, parser);
    parse(expires_at, parser);
    data = parser.template fetch_string<Slice>();
    if (has_extra == 0) {
      extra = 0;
    } else {
      parse(extra, parser);
    }
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
TQueueBinlog<BinlogT>::TQueueBinlog() {
  diff_ = Clocks::system() - Time::now();
}

template <class BinlogT>
uint64 TQueueBinlog<BinlogT>::push(QueueId queue_id, const RawEvent &event) {
  TQueueLogEvent log_event;
  log_event.queue_id = queue_id;
  log_event.event_id = event.event_id.value();
  log_event.expires_at = static_cast<int32>(event.expires_at + diff_ + 1);
  log_event.data = event.data;
  log_event.extra = event.extra;
  auto magic = magic_ + (log_event.extra != 0);
  if (event.logevent_id == 0) {
    return binlog_->add(magic, log_event);
  }
  binlog_->rewrite(event.logevent_id, magic, log_event);
  return event.logevent_id;
}

template <class BinlogT>
void TQueueBinlog<BinlogT>::pop(uint64 logevent_id) {
  binlog_->erase(logevent_id);
}

template <class BinlogT>
Status TQueueBinlog<BinlogT>::replay(const BinlogEvent &binlog_event, TQueue &q) const {
  TQueueLogEvent event;
  TlParser parser(binlog_event.data_);
  int32 has_extra = binlog_event.type_ - magic_;
  if (has_extra != 0 && has_extra != 1) {
    return Status::Error("Wrong magic");
  }
  event.parse(parser, has_extra);
  parser.fetch_end();
  TRY_STATUS(parser.get_status());
  TRY_RESULT(event_id, EventId::from_int32(event.event_id));
  RawEvent raw_event;
  raw_event.logevent_id = binlog_event.id_;
  raw_event.event_id = event_id;
  raw_event.expires_at = event.expires_at - diff_;
  raw_event.data = event.data.str();
  raw_event.extra = event.extra;
  if (!q.do_push(event.queue_id, std::move(raw_event))) {
    return Status::Error("Failed to add event");
  }
  return Status::OK();
}

template class TQueueBinlog<BinlogInterface>;
template class TQueueBinlog<Binlog>;

uint64 TQueueMemoryStorage::push(QueueId queue_id, const RawEvent &event) {
  auto logevent_id = event.logevent_id == 0 ? next_logevent_id_++ : event.logevent_id;
  events_[logevent_id] = std::make_pair(queue_id, event);
  return logevent_id;
}

void TQueueMemoryStorage::pop(uint64 logevent_id) {
  events_.erase(logevent_id);
}

void TQueueMemoryStorage::replay(TQueue &q) const {
  for (auto &e : events_) {
    auto x = e.second;
    x.second.logevent_id = e.first;
    bool is_added = q.do_push(x.first, std::move(x.second));
    CHECK(is_added);
  }
}

}  // namespace td
