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
  int32 magic_{2314};
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

class TestTQueue {
 public:
  CSlice binlog_path() {
    return "test_binlog";
  }
  TestTQueue() {
    auto memory_storage = td::make_unique<MemoryStorage>();
    memory_storage_ = memory_storage.get();
    memory_.set_callback(std::move(memory_storage));

    auto tqueue_binlog = make_unique<TQueueBinlog<Binlog>>();
    Binlog::destroy(binlog_path()).ensure();
    auto binlog = std::make_shared<Binlog>();
    binlog->init(binlog_path().str(), [&](const BinlogEvent &event) { UNREACHABLE(); }).ensure();
    tqueue_binlog->set_binlog(binlog);
    binlog_.set_callback(std::move(tqueue_binlog));
  }

  void restart(Random::Xorshift128plus &rnd) {
    memory_.extract_callback().release();
    auto memory_storage = unique_ptr<MemoryStorage>(memory_storage_);
    memory_ = TQueue();
    memory_storage->replay(memory_);
    memory_.set_callback(std::move(memory_storage));

    if (rnd.fast(0, 100) != 0) {
      return;
    }

    LOG(ERROR) << "RESTART BINLOG";
    binlog_ = TQueue();
    auto tqueue_binlog = make_unique<TQueueBinlog<Binlog>>();
    auto binlog = std::make_shared<Binlog>();
    binlog->init(binlog_path().str(), [&](const BinlogEvent &event) { tqueue_binlog->replay(event, binlog_); })
        .ensure();
    tqueue_binlog->set_binlog(binlog);
    binlog_.set_callback(std::move(tqueue_binlog));
  }

  EventId push(TQueueId queue_id, string data, double expire_at, EventId new_id = EventId()) {
    auto a_id = baseline_.push(queue_id, data, expire_at, new_id);
    auto b_id = memory_.push(queue_id, data, expire_at, new_id);
    auto c_id = binlog_.push(queue_id, data, expire_at, new_id);
    ASSERT_EQ(a_id, b_id);
    ASSERT_EQ(a_id, c_id);
    return a_id;
  }

  void check_head_tail(TQueueId qid) {
    ASSERT_EQ(baseline_.get_head(qid), memory_.get_head(qid));
    ASSERT_EQ(baseline_.get_head(qid), binlog_.get_head(qid));
    ASSERT_EQ(baseline_.get_tail(qid), memory_.get_tail(qid));
    ASSERT_EQ(baseline_.get_tail(qid), binlog_.get_tail(qid));
  }

  void check_get(TQueueId qid, Random::Xorshift128plus &rnd) {
    TQueue::Event a[10];
    MutableSpan<TQueue::Event> a_span(a, 10);
    TQueue::Event b[10];
    MutableSpan<TQueue::Event> b_span(b, 10);
    TQueue::Event c[10];
    MutableSpan<TQueue::Event> c_span(b, 10);

    auto a_from = baseline_.get_head(qid);
    auto b_from = memory_.get_head(qid);
    auto c_from = binlog_.get_head(qid);
    ASSERT_EQ(a_from, b_from);
    ASSERT_EQ(a_from, c_from);

    auto tmp = a_from.advance(rnd.fast(-10, 10));
    if (tmp.is_ok()) {
      a_from = tmp.move_as_ok();
    }
    auto a_size = baseline_.get(qid, a_from, 0, a_span).move_as_ok();
    auto b_size = memory_.get(qid, a_from, 0, b_span).move_as_ok();
    auto c_size = binlog_.get(qid, a_from, 0, c_span).move_as_ok();
    ASSERT_EQ(a_size, b_size);
    ASSERT_EQ(a_size, c_size);
    for (size_t i = 0; i < a_size; i++) {
      ASSERT_EQ(a_span[i].id, b_span[i].id);
      ASSERT_EQ(a_span[i].id, c_span[i].id);
      ASSERT_EQ(a_span[i].data, b_span[i].data);
      ASSERT_EQ(a_span[i].data, c_span[i].data);
    }
  }

 private:
  TQueue baseline_;
  TQueue memory_;
  TQueue binlog_;
  MemoryStorage *memory_storage_{nullptr};
  //TQueue binlog_;
};

TEST(TQueue, random) {
  Random::Xorshift128plus rnd(123);
  auto next_qid = [&] { return rnd.fast(1, 10); };
  auto next_first_id = [&] {
    if (rnd.fast(0, 3) == 0) {
      return EventId::from_int32(EventId::MAX_ID - 20).move_as_ok();
    }
    return EventId::from_int32(rnd.fast(1000000000, 1500000000)).move_as_ok();
  };
  TestTQueue q;
  auto push_event = [&] {
    auto data = PSTRING() << rnd();
    q.push(next_qid(), data, 0, next_first_id());
  };
  auto check_head_tail = [&] { q.check_head_tail(next_qid()); };
  auto restart = [&] { q.restart(rnd); };
  auto get = [&] { q.check_get(next_qid(), rnd); };
  RandomSteps steps({{push_event, 100}, {check_head_tail, 10}, {get, 40}, {restart, 1}});
  for (int i = 0; i < 1000000; i++) {
    steps.step(rnd);
  }
}

}  // namespace td
