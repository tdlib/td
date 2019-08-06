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
#include "td/utils/tl_storers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_helpers.h"
#include <map>
#include <set>

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"

namespace td {
class EventId {
 public:
  constexpr static int32 MAX_ID = 2000000000;
  EventId() = default;
  static Result<EventId> from_int32(int32 id) {
    if (!is_valid(id)) {
      return Status::Error("Invalid id");
    }
    return EventId(id);
  }
  static EventId create_random() {
    return from_int32(Random::fast_uint32() % (MAX_ID / 2) + 10).move_as_ok();
  }
  int32 value() const {
    return id_;
  }
  Result<EventId> next() const {
    return from_int32(id_ + 1);
  }
  Result<EventId> advance(size_t offset) const {
    TRY_RESULT(new_id, narrow_cast_safe<int32>(id_ + offset));
    return from_int32(new_id);
  }
  bool empty() const {
    return id_ == 0;
  }

  bool operator==(const EventId &other) const {
    return id_ == other.id_;
  }
  friend StringBuilder &operator<<(StringBuilder &sb, const EventId id) {
    return sb << "EventId{" << id.value() << "}";
  }

 private:
  int32 id_{0};
  explicit EventId(int32 id) : id_(id) {
    CHECK(is_valid(id));
  }
  static bool is_valid(int32 id) {
    return 0 <= id && id < MAX_ID;
  }
};

using TQueueId = int64;

class TQueue {
 public:
  struct Event {
    EventId id;
    Slice data;
    double expire_at;
  };
  struct RawEvent {
    int64 logevent_id{0};
    EventId event_id;
    string data;
    double expire_at{0};
  };
  class Callback {
   public:
    using RawEvent = TQueue::RawEvent;
    virtual ~Callback() {
    }
    virtual int64 push(TQueueId queue_id, const RawEvent &event) = 0;
    virtual void pop(int64 logevent_id) = 0;
  };

  void set_callback(unique_ptr<Callback> callback) {
    callback_ = std::move(callback);
  }
  unique_ptr<Callback> extract_callback() {
    return std::move(callback_);
  }

  void do_push(TQueueId queue_id, RawEvent &&raw_event) {
    CHECK(!raw_event.event_id.empty());
    if (callback_ && raw_event.logevent_id == 0) {
      raw_event.logevent_id = callback_->push(queue_id, raw_event);
    }
    auto &q = queues_[queue_id];
    q.tail_id = raw_event.event_id.next().move_as_ok();
    q.events.push(std::move(raw_event));
  }

  void on_pop(int64 logevent_id) {
    if (callback_) {
      callback_->pop(logevent_id);
    }
  }

  EventId push(TQueueId queue_id, string data, double expire_at, EventId new_id = EventId()) {
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

  EventId get_head(TQueueId queue_id) const {
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

  EventId get_tail(TQueueId queue_id) const {
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

  Result<size_t> get(TQueueId queue_id, EventId from_id, double now, MutableSpan<Event> events) {
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
    size_t res_n = 0;
    for (size_t i = 0; i < from_events.size(); i++) {
      auto &from = from_events[i];
      if (from.expire_at < now) {
        //TODO: pop this element
        continue;
      }

      auto &to = events[res_n];
      to.data = from.data;
      to.id = from.event_id;
      to.expire_at = from.expire_at;

      res_n++;
      if (res_n == events.size()) {
        break;
      }
    }
    return res_n;
  }

 private:
  struct Queue {
    EventId tail_id;
    VectorQueue<RawEvent> events;
  };

  HashMap<TQueueId, Queue> queues_;
  unique_ptr<Callback> callback_;

  void confirm_read(Queue &q, EventId till_id) {
    while (!q.events.empty() && q.events.front().event_id.value() < till_id.value()) {
      on_pop(q.events.front().logevent_id);
      q.events.pop();
    }
  }
};

template <class BinlogT>
class TQueueBinlog : public TQueue::Callback {
 public:
  struct LogEvent : public Storer {
    LogEvent() = default;
    int32 queue_id;
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
      parse(queue_id, parser);
      parse(event_id, parser);
      parse(expire_at, parser);
      parse(data, parser);
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

  int64 push(TQueueId queue_id, const RawEvent &event) override {
    LogEvent log_event;
    log_event.queue_id = queue_id;
    log_event.event_id = event.event_id.value();
    log_event.expire_at = static_cast<int32>(event.expire_at);
    log_event.data = event.data;
    return binlog_->add(magic_, log_event);
  }

  void pop(int64 logevent_id) override {
    binlog_->erase(binlog_, logevent_id);
  }

  Status replay(const BinlogEvent &binlog_event, TQueue &q) {
    LogEvent event;
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

  void set_binlog(std::shared_ptr<BinlogT> binlog) {
    binlog_ = std::move(binlog);
  }

 private:
  std::shared_ptr<BinlogT> binlog_;
  int32 magic_{0};
};

class MemoryStorage : public TQueue::Callback {
 public:
  int64 push(TQueueId queue_id, const RawEvent &event) override {
    auto logevent_id = next_logevent_id_++;
    events_[logevent_id] = std::make_pair(queue_id, event);

    return logevent_id;
  }
  void pop(int64 logevent_id) override {
    events_.erase(logevent_id);
  }

  void replay(TQueue &q) {
    LOG(ERROR) << events_.size();
    for (auto e : events_) {
      auto x = e.second;
      x.second.logevent_id = e.first;
      q.do_push(x.first, std::move(x.second));
    }
  }

 private:
  int64 next_logevent_id_{1};
  std::map<int64, std::pair<TQueueId, RawEvent>> events_;
};

struct Step {
  std::function<void()> func;
  td::uint32 weight;
};
class RandomSteps {
 public:
  RandomSteps(std::vector<Step> steps) : steps_(std::move(steps)) {
    for (auto &step : steps_) {
      steps_sum_ += step.weight;
    }
  }
  template <class Random>
  void step(Random &rnd) {
    auto w = rnd() % steps_sum_;
    for (auto &step : steps_) {
      if (w < step.weight) {
        step.func();
        break;
      }
      w -= step.weight;
    }
  }

 private:
  std::vector<Step> steps_;
  td::int32 steps_sum_ = 0;
};

TEST(TQueue, hands) {
  TQueue::Event events[100];
  auto events_span = MutableSpan<TQueue::Event>(events, 100);

  TQueue tqueue;
  auto qid = 12;
  ASSERT_EQ(true, tqueue.get_head(qid).empty());
  ASSERT_EQ(true, tqueue.get_tail(qid).empty());
  tqueue.push(qid, "hello", 0);
  auto head = tqueue.get_head(qid);
  ASSERT_EQ(head.next().ok(), tqueue.get_tail(qid));
  ASSERT_EQ(1u, tqueue.get(qid, head, 0, events_span).move_as_ok());
}

TEST(TQueue, random) {
  // Just do random ops with one queue
  auto qid = 12;
  EventId first_id = EventId::from_int32(EventId::MAX_ID - 100).move_as_ok();
  //first_id = {};

  TQueue tqueue_memory;
  //auto memory_storage = td::make_unique<MemoryStorage>();
  //auto memory_storage_ptr = memory_storage.get();
  //tqueue_memory.set_callback(std::move(memory_storage));

  TQueue tqueue_binlog;
  auto binlog_storage = td::make_unique<MemoryStorage>();
  auto binlog_storage_ptr = binlog_storage.get();
  tqueue_binlog.set_callback(std::move(binlog_storage));

  Random::Xorshift128plus rnd(123);
  auto push_event = [&] {
    auto data = PSTRING() << rnd();
    tqueue_memory.push(qid, data, 0, first_id);
    tqueue_binlog.push(qid, data, 0, first_id);
  };
  auto get_head = [&] { ASSERT_EQ(tqueue_memory.get_head(qid), tqueue_binlog.get_head(qid)); };
  auto get_tail = [&] { ASSERT_EQ(tqueue_memory.get_tail(qid), tqueue_binlog.get_tail(qid)); };

  TQueue::Event events_a[100];
  auto events_span_a = MutableSpan<TQueue::Event>(events_a, 100);
  TQueue::Event events_b[100];
  auto events_span_b = MutableSpan<TQueue::Event>(events_b, 100);

  auto get = [&] {
    auto a_from = tqueue_memory.get_head(qid);
    auto b_from = tqueue_binlog.get_head(qid);
    ASSERT_EQ(a_from, b_from);

    auto tmp = a_from.advance(rnd.fast(-10, 10));
    if (tmp.is_ok()) {
      a_from = tmp.move_as_ok();
    }
    auto a_size = tqueue_memory.get(qid, a_from, 0, events_span_a).move_as_ok();
    auto b_size = tqueue_binlog.get(qid, a_from, 0, events_span_b).move_as_ok();
    ASSERT_EQ(a_size, b_size);
    for (size_t i = 0; i < a_size; i++) {
      ASSERT_EQ(events_span_a[i].id, events_span_b[i].id);
      ASSERT_EQ(events_span_a[i].data, events_span_b[i].data);
    }
  };

  auto restart = [&] {
    tqueue_binlog.extract_callback().release();
    binlog_storage = unique_ptr<MemoryStorage>(binlog_storage_ptr);
    tqueue_binlog = TQueue();
    binlog_storage->replay(tqueue_binlog);
    tqueue_binlog.set_callback(std::move(binlog_storage));
  };

  RandomSteps steps({{push_event, 100}, {get_head, 10}, {get_tail, 10}, {get, 40}, {restart, 1}});
  for (int i = 0; i < 1000000; i++) {
    steps.step(rnd);
  }
}

}  // namespace td
