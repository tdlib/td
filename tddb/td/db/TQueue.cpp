// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/db/TQueue.h"

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/binlog/BinlogInterface.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/StorerBase.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/tl_storers.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <set>

#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
#include <sanitizer/msan_interface.h>
#define TD_TQUEUE_MSAN_GLIBCXX_TREE_ACTIVE 1
#endif
#endif
#if defined(__SANITIZE_MEMORY__)
#include <sanitizer/msan_interface.h>
#define TD_TQUEUE_MSAN_GLIBCXX_TREE_ACTIVE 1
#endif
#if !defined(__GLIBCXX__) || !defined(TD_TQUEUE_MSAN_GLIBCXX_TREE_ACTIVE)
#define TD_TQUEUE_MSAN_GLIBCXX_TREE_ACTIVE 0
#endif

namespace td {

#if defined(__clang__)
#define TD_TQUEUE_RETURN_NO_SANITIZE_MEMORY __attribute__((no_sanitize("memory"), disable_sanitizer_instrumentation))
#define TD_TQUEUE_COMPARE_NO_SANITIZE_MEMORY __attribute__((no_sanitize("memory")))
#else
#define TD_TQUEUE_RETURN_NO_SANITIZE_MEMORY
#define TD_TQUEUE_COMPARE_NO_SANITIZE_MEMORY
#endif

using EventId = TQueue::EventId;

TD_TQUEUE_RETURN_NO_SANITIZE_MEMORY static std::map<EventId, TQueue::RawEvent> return_deleted_events(
    std::map<EventId, TQueue::RawEvent> deleted_events) {
  return deleted_events;
}

static bool add_deleted_event(std::map<EventId, TQueue::RawEvent> &deleted_events, EventId event_id,
                              TQueue::RawEvent &&raw_event) {
  const auto size_before = deleted_events.size();
  deleted_events.emplace_hint(deleted_events.end(), event_id, std::move(raw_event));
  return deleted_events.size() == size_before + 1;
}

static void unpoison_deleted_events_if_msan(std::map<EventId, TQueue::RawEvent> &deleted_events) {
#if TD_TQUEUE_MSAN_GLIBCXX_TREE_ACTIVE
  using DeletedEvents = std::map<EventId, TQueue::RawEvent>;
  using TreeNode = std::_Rb_tree_node<DeletedEvents::value_type>;

  __msan_unpoison(&deleted_events, sizeof(deleted_events));
  for (auto &entry : deleted_events) {
    auto *value_ptr = std::addressof(entry);
    auto *node_ptr = reinterpret_cast<TreeNode *>(reinterpret_cast<char *>(value_ptr) - offsetof(TreeNode, _M_storage));
    __msan_unpoison(node_ptr, sizeof(TreeNode));
  }
#else
  static_cast<void>(deleted_events);
#endif
}

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

TD_TQUEUE_COMPARE_NO_SANITIZE_MEMORY bool EventId::operator<(const EventId &other) const {
  return id_ < other.id_;
}

StringBuilder &operator<<(StringBuilder &string_builder, EventId id) {
  return string_builder << "EventId{" << id.value() << "}";
}

EventId::EventId(int32 id) : id_(id) {
  CHECK(is_valid_id(id));
}

bool EventId::is_valid_id(int32 id) {
  return 0 <= id && id < MAX_ID;
}

class TQueueImpl final : public TQueue {
  static constexpr size_t MAX_EVENT_LENGTH = 65536 * 8;
  static constexpr size_t MAX_QUEUE_EVENTS = 100000;
  static constexpr size_t MAX_TOTAL_EVENT_LENGTH = 1 << 27;

 public:
  void set_callback(unique_ptr<StorageCallback> callback) final {
    callback_ = std::move(callback);
  }
  unique_ptr<StorageCallback> extract_callback() final {
    return std::move(callback_);
  }

  bool do_push(QueueId queue_id, RawEvent &&raw_event) final {
    CHECK(raw_event.event_id.is_valid());
    // raw_event.data can be empty when replaying binlog
    if (raw_event.data.size() > MAX_EVENT_LENGTH || queue_id == 0) {
      return false;
    }
    auto &q = get_or_create_queue(queue_id);
    if (q.events.size() >= MAX_QUEUE_EVENTS || q.total_event_length > MAX_TOTAL_EVENT_LENGTH - raw_event.data.size() ||
        raw_event.expires_at <= 0) {
      return false;
    }
    auto event_id = raw_event.event_id;
    if (event_id < q.tail_id) {
      return false;
    }

    if (!q.events.empty()) {
      auto it = q.events.end();
      --it;
      if (it->second.data.empty()) {
        if (callback_ != nullptr && it->second.log_event_id != 0) {
          callback_->pop(it->second.log_event_id);
        }
        q.events.erase(it);
      }
    }
    if (q.events.empty() && !raw_event.data.empty()) {
      schedule_queue_gc(queue_id, q, raw_event.expires_at);
    }

    if (raw_event.log_event_id == 0 && callback_ != nullptr) {
      raw_event.log_event_id = callback_->push(queue_id, raw_event);
    }
    q.tail_id = event_id.next().move_as_ok();
    q.total_event_length += raw_event.data.size();
    q.events.emplace_back(event_id, std::move(raw_event));
    return true;
  }

  Result<EventId> push(QueueId queue_id, string data, int32 expires_at, int64 extra, EventId hint_new_id) final {
    if (data.empty()) {
      return Status::Error("Data is empty");
    }
    if (data.size() > MAX_EVENT_LENGTH) {
      return Status::Error("Data is too big");
    }
    if (queue_id == 0) {
      return Status::Error("Queue identifier is invalid");
    }

    auto &q = get_or_create_queue(queue_id);
    if (q.events.size() >= MAX_QUEUE_EVENTS) {
      return Status::Error("Queue is full");
    }
    if (q.total_event_length > MAX_TOTAL_EVENT_LENGTH - data.size()) {
      return Status::Error("Queue size is too big");
    }
    if (expires_at <= 0) {
      return Status::Error("Failed to add already expired event");
    }
    EventId event_id;
    while (true) {
      if (q.tail_id.empty()) {
        if (hint_new_id.empty()) {
          q.tail_id = EventId::from_int32(
                          Random::fast(2 * max(static_cast<int>(MAX_QUEUE_EVENTS), 1000000) + 1, EventId::MAX_ID / 2))
                          .move_as_ok();
        } else {
          q.tail_id = hint_new_id;
        }
      }
      event_id = q.tail_id;
      CHECK(event_id.is_valid());
      if (event_id.next().is_ok()) {
        break;
      }
      for (auto it = q.events.begin(); it != q.events.end();) {
        pop(q, queue_id, it, {});
      }
      q.tail_id = EventId();
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

  EventId get_head(QueueId queue_id) const final {
    auto q = find_queue(queue_id);
    if (q == nullptr) {
      return EventId();
    }
    return get_queue_head(*q);
  }

  EventId get_tail(QueueId queue_id) const final {
    auto q = find_queue(queue_id);
    if (q == nullptr) {
      return EventId();
    }
    return q->tail_id;
  }

  void forget(QueueId queue_id, EventId event_id) final {
    auto q = find_queue(queue_id);
    if (q == nullptr) {
      return;
    }
    auto it = std::lower_bound(q->events.begin(), q->events.end(), event_id,
                               [](const auto &pair, EventId id) { return pair.first < id; });
    if (it == q->events.end() || it->first != event_id) {
      return;
    }
    pop(*q, queue_id, it, q->tail_id);
  }

  std::map<EventId, RawEvent> clear(QueueId queue_id, size_t keep_count) final {
    auto q = find_queue(queue_id);
    if (q == nullptr) {
      return {};
    }
    auto size = get_size(*q);
    if (size <= keep_count) {
      return {};
    }

    auto start_time = Time::now();
    auto total_event_length = q->total_event_length;

    auto end_it = q->events.end();
    for (size_t i = 0; i < keep_count; i++) {
      --end_it;
    }
    if (keep_count == 0) {
      --end_it;
      auto &event = end_it->second;
      if (callback_ == nullptr || event.log_event_id == 0) {
        ++end_it;
      } else if (!event.data.empty()) {
        clear_event_data(*q, event);
        callback_->push(queue_id, event);
      }
    }

    auto collect_deleted_event_ids_time = 0.0;
    if (callback_ != nullptr) {
      vector<uint64> deleted_log_event_ids;
      deleted_log_event_ids.reserve(size - keep_count);
      for (auto it = q->events.begin(); it != end_it; ++it) {
        auto &event = it->second;
        if (event.log_event_id != 0) {
          deleted_log_event_ids.push_back(event.log_event_id);
        }
      }
      collect_deleted_event_ids_time = Time::now() - start_time;
      callback_->pop_batch(std::move(deleted_log_event_ids));
    }
    auto callback_clear_time = Time::now() - start_time;

    std::map<EventId, RawEvent> deleted_events;
    if (keep_count > size / 2) {
      for (auto it = q->events.begin(); it != end_it;) {
        q->total_event_length -= it->second.data.size();
        bool is_inserted = add_deleted_event(deleted_events, it->first, std::move(it->second));
        CHECK(is_inserted);
        it = q->events.erase(it);
      }
    } else {
      td::vector<std::pair<EventId, RawEvent>> kept_events;
      kept_events.reserve(q->events.end() - end_it);

      q->total_event_length = 0;
      for (auto it = end_it; it != q->events.end(); ++it) {
        q->total_event_length += it->second.data.size();
        kept_events.push_back(std::move(*it));
      }

      for (auto it = q->events.begin(); it != end_it; ++it) {
        bool is_inserted = add_deleted_event(deleted_events, it->first, std::move(it->second));
        CHECK(is_inserted);
      }

      q->events = std::move(kept_events);
    }

    unpoison_deleted_events_if_msan(deleted_events);

    auto clear_time = Time::now() - start_time;
    if (clear_time > 0.02) {
      LOG(WARNING) << "Cleared " << (size - keep_count) << " TQueue events with total size "
                   << (total_event_length - q->total_event_length) << " in " << clear_time - callback_clear_time
                   << " seconds, collected their identifiers in " << collect_deleted_event_ids_time
                   << " seconds, and deleted them from callback in "
                   << callback_clear_time - collect_deleted_event_ids_time << " seconds";
    }
    return return_deleted_events(std::move(deleted_events));
  }

  Result<size_t> get(QueueId queue_id, EventId from_id, bool forget_previous, int32 unix_time_now,
                     MutableSpan<Event> &result_events) final {
    auto q = find_queue(queue_id);
    if (q == nullptr) {
      result_events.truncate(0);
      return 0;
    }
    // Some sanity checks
    if (from_id.value() > q->tail_id.value() + 10) {
      return Status::Error("Specified from_id is in the future");
    }
    if (from_id.value() < get_queue_head(*q).value() - static_cast<int32>(MAX_QUEUE_EVENTS)) {
      return Status::Error("Specified from_id is in the past");
    }

    do_get(queue_id, *q, from_id, forget_previous, unix_time_now, result_events);
    return get_size(*q);
  }

  std::pair<int64, bool> run_gc(int32 unix_time_now) final {
    int64 deleted_events = 0;
    auto max_finish_time = Time::now() + 0.05;
    int64 counter = 0;
    while (!queue_gc_at_.empty()) {
      auto it = queue_gc_at_.begin();
      if (it->first >= unix_time_now) {
        break;
      }
      auto queue_id = it->second;
      auto &q = get_or_create_queue(queue_id);
      CHECK(q.gc_at == it->first);
      int32 new_gc_at = 0;

      if (!q.events.empty()) {
        size_t size_before = get_size(q);
        for (auto event_it = q.events.begin(); event_it != q.events.end();) {
          auto &event = event_it->second;
          if ((++counter & 128) == 0 && Time::now() >= max_finish_time) {
            if (new_gc_at == 0) {
              new_gc_at = event.expires_at;
            }
            break;
          }
          if (event.expires_at < unix_time_now || event.data.empty()) {
            pop(q, queue_id, event_it, q.tail_id);
          } else {
            if (new_gc_at != 0) {
              break;
            }
            new_gc_at = event.expires_at;
            ++event_it;
          }
        }
        size_t size_after = get_size(q);
        CHECK(size_after <= size_before);
        deleted_events += size_before - size_after;
      }
      schedule_queue_gc(queue_id, q, new_gc_at);
      if (Time::now() >= max_finish_time) {
        return {deleted_events, false};
      }
    }
    return {deleted_events, true};
  }

  size_t get_size(QueueId queue_id) const final {
    auto q = find_queue(queue_id);
    if (q == nullptr) {
      return 0;
    }
    return get_size(*q);
  }

  void close(Promise<> promise) final {
    if (callback_ != nullptr) {
      callback_->close(std::move(promise));
      callback_ = nullptr;
    }
  }

 private:
  struct Queue {
    EventId tail_id;
    td::vector<std::pair<EventId, RawEvent>> events;  // Ordered vector (always strictly increasing EventId)
    size_t total_event_length = 0;
    int32 gc_at = 0;
  };

  td::vector<std::pair<QueueId, Queue>> queues_;
  td::vector<std::pair<int32, QueueId>> queue_gc_at_;
  unique_ptr<StorageCallback> callback_;

  static EventId get_queue_head(const Queue &q) {
    if (q.events.empty()) {
      return q.tail_id;
    }
    return q.events.begin()->first;
  }

  static size_t get_size(const Queue &q) {
    if (q.events.empty()) {
      return 0;
    }

    return q.events.size() - (q.events.rbegin()->second.data.empty() ? 1 : 0);
  }

  Queue *find_queue(QueueId queue_id) {
    auto it = std::lower_bound(queues_.begin(), queues_.end(), queue_id,
                               [](const auto &entry, QueueId id) { return entry.first < id; });
    if (it == queues_.end() || it->first != queue_id) {
      return nullptr;
    }
    return &it->second;
  }

  const Queue *find_queue(QueueId queue_id) const {
    auto it = std::lower_bound(queues_.begin(), queues_.end(), queue_id,
                               [](const auto &entry, QueueId id) { return entry.first < id; });
    if (it == queues_.end() || it->first != queue_id) {
      return nullptr;
    }
    return &it->second;
  }

  Queue &get_or_create_queue(QueueId queue_id) {
    auto it = std::lower_bound(queues_.begin(), queues_.end(), queue_id,
                               [](const auto &entry, QueueId id) { return entry.first < id; });
    if (it == queues_.end() || it->first != queue_id) {
      it = queues_.insert(it, {queue_id, Queue{}});
    }
    return it->second;
  }

  void pop(Queue &q, QueueId queue_id, td::vector<std::pair<EventId, RawEvent>>::iterator &it, EventId tail_id) {
    auto &event = it->second;
    if (callback_ == nullptr || event.log_event_id == 0) {
      remove_event(q, it);
      return;
    }

    if (event.event_id.next().ok() == tail_id) {
      if (!event.data.empty()) {
        clear_event_data(q, event);
        callback_->push(queue_id, event);
      }
      ++it;
    } else {
      callback_->pop(event.log_event_id);
      remove_event(q, it);
    }
  }

  static void remove_event(Queue &q, td::vector<std::pair<EventId, RawEvent>>::iterator &it) {
    q.total_event_length -= it->second.data.size();
    it = q.events.erase(it);
  }

  static void clear_event_data(Queue &q, RawEvent &event) {
    q.total_event_length -= event.data.size();
    event.data = {};
  }

  void do_get(QueueId queue_id, Queue &q, EventId from_id, bool forget_previous, int32 unix_time_now,
              MutableSpan<Event> &result_events) {
    if (forget_previous) {
      for (auto it = q.events.begin(); it != q.events.end() && it->first < from_id;) {
        pop(q, queue_id, it, q.tail_id);
      }
    }

    size_t ready_n = 0;
    auto it = std::lower_bound(q.events.begin(), q.events.end(), from_id,
                               [](const auto &pair, EventId id) { return pair.first < id; });
    for (; it != q.events.end();) {
      auto &event = it->second;
      if (event.expires_at < unix_time_now || event.data.empty()) {
        pop(q, queue_id, it, q.tail_id);
      } else {
        CHECK(!(event.event_id < from_id));
        if (ready_n == result_events.size()) {
          break;
        }

        auto &to = result_events[ready_n];
        to.data = event.data;
        to.id = event.event_id;
        to.expires_at = event.expires_at;
        to.extra = event.extra;
        ready_n++;
        ++it;
      }
    }

    result_events.truncate(ready_n);
  }

  void schedule_queue_gc(QueueId queue_id, Queue &q, int32 gc_at) {
    if (q.gc_at != 0) {
      auto old_key = std::make_pair(q.gc_at, queue_id);
      auto old_it = std::lower_bound(queue_gc_at_.begin(), queue_gc_at_.end(), old_key);
      CHECK(old_it != queue_gc_at_.end() && *old_it == old_key);
      queue_gc_at_.erase(old_it);
    }
    q.gc_at = gc_at;
    if (q.gc_at != 0) {
      auto new_key = std::make_pair(gc_at, queue_id);
      auto new_it = std::lower_bound(queue_gc_at_.begin(), queue_gc_at_.end(), new_key);
      CHECK(new_it == queue_gc_at_.end() || *new_it != new_key);
      queue_gc_at_.insert(new_it, new_key);
    }
  }
};

unique_ptr<TQueue> TQueue::create() {
  return make_unique<TQueueImpl>();
}

struct TQueueLogEvent final : public Storer {
  int64 queue_id;
  int32 event_id;
  int32 expires_at;
  Slice data;
  int64 extra;

  template <class StorerT>
  void store(StorerT &storer) const {
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
  void parse(ParserT &parser, int32 has_extra) {
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

  size_t size() const final {
    TlStorerCalcLength storer;
    store(storer);
    return storer.get_length();
  }

  size_t store(uint8 *ptr) const final {
    TlStorerUnsafe storer(ptr);
    store(storer);
    return static_cast<size_t>(storer.get_buf() - ptr);
  }
};

template <class BinlogT>
uint64 TQueueBinlog<BinlogT>::push(QueueId queue_id, const RawEvent &event) {
  TQueueLogEvent log_event;
  log_event.queue_id = queue_id;
  log_event.event_id = event.event_id.value();
  log_event.expires_at = event.expires_at;
  log_event.data = event.data;
  log_event.extra = event.extra;
  auto magic = BINLOG_EVENT_TYPE + (log_event.extra != 0);
  if (event.log_event_id == 0) {
    return binlog_->add(magic, log_event);
  }
  binlog_->rewrite(event.log_event_id, magic, log_event);
  return event.log_event_id;
}

template <class BinlogT>
void TQueueBinlog<BinlogT>::pop(uint64 log_event_id) {
  binlog_->erase(log_event_id);
}

template <class BinlogT>
void TQueueBinlog<BinlogT>::pop_batch(std::vector<uint64> log_event_ids) {
  binlog_->erase_batch(std::move(log_event_ids));
}

template <class BinlogT>
Status TQueueBinlog<BinlogT>::replay(const BinlogEvent &binlog_event, TQueue &q) const {
  TQueueLogEvent event;
  TlParser parser(binlog_event.get_data());
  int32 has_extra = binlog_event.type_ - BINLOG_EVENT_TYPE;
  if (has_extra != 0 && has_extra != 1) {
    return Status::Error("Wrong magic");
  }
  event.parse(parser, has_extra);
  parser.fetch_end();
  TRY_STATUS(parser.get_status());
  TRY_RESULT(event_id, EventId::from_int32(event.event_id));
  RawEvent raw_event;
  raw_event.log_event_id = binlog_event.id_;
  raw_event.event_id = event_id;
  raw_event.expires_at = event.expires_at;
  raw_event.data = event.data.str();
  raw_event.extra = event.extra;
  if (!q.do_push(event.queue_id, std::move(raw_event))) {
    return Status::Error("Failed to add event");
  }
  return Status::OK();
}

template <class BinlogT>
void TQueueBinlog<BinlogT>::close(Promise<> promise) {
  binlog_->close(std::move(promise));
}

template class TQueueBinlog<BinlogInterface>;
template class TQueueBinlog<Binlog>;

uint64 TQueueMemoryStorage::push(QueueId queue_id, const RawEvent &event) {
  auto log_event_id = event.log_event_id == 0 ? next_log_event_id_++ : event.log_event_id;
  events_[log_event_id] = std::make_pair(queue_id, event);
  return log_event_id;
}

void TQueueMemoryStorage::pop(uint64 log_event_id) {
  events_.erase(log_event_id);
}

void TQueueMemoryStorage::replay(TQueue &q) const {
  td::vector<std::pair<uint64, std::pair<QueueId, RawEvent>>> sorted_events;
  sorted_events.reserve(events_.size());
  for (auto &e : events_) {
    sorted_events.emplace_back(e.first, e.second);
  }
  std::sort(sorted_events.begin(), sorted_events.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
  for (auto &e : sorted_events) {
    auto x = e.second;
    x.second.log_event_id = e.first;
    bool is_added = q.do_push(x.first, std::move(x.second));
    CHECK(is_added);
  }
}
void TQueueMemoryStorage::close(Promise<> promise) {
  events_.clear();
  promise.set_value({});
}

void TQueue::StorageCallback::pop_batch(std::vector<uint64> log_event_ids) {
  for (auto id : log_event_ids) {
    pop(id);
  }
}

#undef TD_TQUEUE_RETURN_NO_SANITIZE_MEMORY
#undef TD_TQUEUE_COMPARE_NO_SANITIZE_MEMORY

}  // namespace td
