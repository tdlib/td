//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/TQueue.h"

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/binlog/BinlogInterface.h"

#include "td/utils/format.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Random.h"
#include "td/utils/StorerBase.h"
#include "td/utils/Time.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/VectorQueue.h"

#include <algorithm>
#include <unordered_map>

namespace td {

using EventId = TQueue::EventId;

static constexpr int32 MAX_DELAY = 7 * 86400;
static constexpr size_t MAX_EVENT_LEN = 65536 * 8;
static constexpr size_t MAX_QUEUE_EVENTS = 1000000;

EventId::EventId() {
}

Result<EventId> EventId::from_int32(int32 id) {
  if (!is_valid(id)) {
    return Status::Error("Invalid ID");
  }
  return EventId(id);
}

EventId EventId::create_random() {
  return from_int32(Random::fast_uint32() % (MAX_ID / 2) + 10 + MAX_QUEUE_EVENTS).move_as_ok();
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
  void set_callback(unique_ptr<Callback> callback) override {
    callback_ = std::move(callback);
  }
  unique_ptr<Callback> extract_callback() override {
    return std::move(callback_);
  }

  void emulate_restart() override {
  }

  void do_push(QueueId queue_id, RawEvent &&raw_event) override {
    //LOG(ERROR) << "Push " << queue_id << " " << raw_event.event_id;
    CHECK(!raw_event.event_id.empty());
    if (raw_event.logevent_id == 0 && callback_) {
      raw_event.logevent_id = callback_->push(queue_id, raw_event);
    }
    auto &q = queues_[queue_id];
    q.tail_id = raw_event.event_id.next().move_as_ok();
    q.events.push(std::move(raw_event));
  }

  Result<EventId> push(QueueId queue_id, string data, double expires_at, EventId new_id = EventId(),
                       int64 extra = 0) override {
    auto &q = queues_[queue_id];
    if (q.events.size() >= MAX_QUEUE_EVENTS) {
      return Status::Error("Queue is full");
    }
    if (data.empty()) {
      return Status::Error("Data is empty");
    }
    EventId event_id;
    while (true) {
      if (q.tail_id.empty()) {
        q.tail_id = new_id.empty() ? EventId::create_random() : new_id;
      }
      event_id = q.tail_id;
      CHECK(!event_id.empty());
      if (event_id.next().is_ok()) {
        break;
      }
      for (auto &e : q.events.as_mutable_span()) {
        try_pop(queue_id, e, EventId{}, EventId{}, 0, true);
      }
      q.tail_id = {};
      q.events = {};
    }

    RawEvent raw_event;
    raw_event.event_id = event_id;
    raw_event.data = std::move(data);
    raw_event.expires_at = expires_at;
    raw_event.extra = extra;
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
    try_pop(queue_id, *it, {}, q.tail_id, 0, true /*force*/);
  }

  Result<size_t> get(QueueId queue_id, EventId from_id, bool forget_previous, double now,
                     MutableSpan<Event> &result_events) override {
    //LOG(ERROR) << "Get " << queue_id << " " << from_id;
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
    if (from_id.value() < q.tail_id.value() - narrow_cast<int32>(MAX_QUEUE_EVENTS) * 2) {
      return Status::Error("Specified from_id is in the past");
    }

    auto from_events = q.events.as_mutable_span();
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
      //LOG(ERROR) << tag("first_i", first_i) << tag("size", from_events.size());
      for (i = first_i; i < from_events.size(); i++) {
        auto &from = from_events[i];
        try_pop(queue_id, from, forget_previous ? from_id : EventId{}, q.tail_id, now);
        if (from.data.empty()) {
          continue;
        }

        if (ready_n == result_events.size()) {
          break;
        }

        if (from.event_id < from_id) {
          // should not happend
          UNREACHABLE();
          continue;
        }

        auto &to = result_events[ready_n];
        to.data = from.data;
        to.id = from.event_id;
        to.expires_at = from.expires_at;
        to.extra = from.extra;
        ready_n++;
      }

      // compactify skipped events
      if ((first_i + ready_n) * 2 < i) {
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
  unique_ptr<Callback> callback_;

  void compactify(VectorQueue<RawEvent> &events, size_t prefix) {
    auto processed = events.as_mutable_span().substr(0, prefix);
    auto removed_n =
        processed.rend() - std::remove_if(processed.rbegin(), processed.rend(), [](auto &e) { return e.data.empty(); });
    events.pop_n(removed_n);
  }

  void try_pop(QueueId queue_id, RawEvent &event, EventId from_id, EventId tail_id, double now, bool force = false) {
    //LOG(ERROR) << event.expires_at << " < " << now << " = " << (event.expires_at < now) << " "
    //<< (event.event_id.value() < from_id.value()) << " " << force << " " << event.data.empty();
    bool should_drop =
        event.expires_at < now || event.event_id.value() < from_id.value() || force || event.data.empty();
    if (!callback_ || event.logevent_id == 0) {
      if (should_drop) {
        event.data = {};
      }
      return;
    }

    if (!should_drop) {
      return;
    }

    //LOG(ERROR) << "Drop " << queue_id << " " << event.event_id;
    if (event.event_id.value() + 1 == tail_id.value()) {
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
  TQueueLogEvent() = default;
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
  log_event.expires_at = static_cast<int32>(event.expires_at + diff_);
  log_event.data = event.data;
  log_event.extra = event.extra;
  if (event.logevent_id == 0) {
    return binlog_->add(magic_ + (log_event.extra != 0), log_event);
  }
  binlog_->rewrite(event.logevent_id, magic_ + (log_event.extra != 0), log_event);
  return event.logevent_id;
}

template <class BinlogT>
void TQueueBinlog<BinlogT>::pop(uint64 logevent_id) {
  binlog_->erase(logevent_id);
}

template <class BinlogT>
Status TQueueBinlog<BinlogT>::replay(const BinlogEvent &binlog_event, TQueue &q) {
  TQueueLogEvent event;
  TlParser parser(binlog_event.data_);
  event.parse(parser, binlog_event.type_ - magic_);
  TRY_STATUS(parser.get_status());
  TRY_RESULT(event_id, EventId::from_int32(event.event_id));
  RawEvent raw_event;
  raw_event.logevent_id = binlog_event.id_;
  raw_event.event_id = event_id;
  raw_event.expires_at = event.expires_at - diff_ + 1;
  raw_event.data = event.data.str();
  raw_event.extra = event.extra;
  q.do_push(event.queue_id, std::move(raw_event));
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

void TQueueMemoryStorage::replay(TQueue &q) {
  for (auto e : events_) {
    auto x = e.second;
    x.second.logevent_id = e.first;
    q.do_push(x.first, std::move(x.second));
  }
}

}  // namespace td
