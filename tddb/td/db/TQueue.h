//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Span.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include <map>
#include <memory>
#include <utility>

namespace td {

class TQueue {
 public:
  class EventId {
   public:
    static constexpr int32 MAX_ID = 2000000000;

    EventId();

    static Result<EventId> from_int32(int32 id);

    static EventId create_random();

    int32 value() const;

    Result<EventId> next() const;

    Result<EventId> advance(size_t offset) const;

    bool empty() const;

    bool operator==(const EventId &other) const;
    bool operator!=(const EventId &other) const;
    bool operator<(const EventId &other) const;

   private:
    int32 id_{0};

    explicit EventId(int32 id);

    static bool is_valid(int32 id);
  };

  struct Event {
    EventId id;
    Slice data;
    int64 extra{0};
    double expires_at{0};
  };

  struct RawEvent {
    uint64 logevent_id{0};
    EventId event_id;
    string data;
    int64 extra{0};
    double expires_at{0};
  };

  using QueueId = int64;

  class Callback {
   public:
    using QueueId = TQueue::QueueId;
    using RawEvent = TQueue::RawEvent;

    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    Callback(Callback &&) = delete;
    Callback &operator=(Callback &&) = delete;
    virtual ~Callback() = default;

    virtual uint64 push(QueueId queue_id, const RawEvent &event) = 0;
    virtual void pop(uint64 logevent_id) = 0;
  };

  static unique_ptr<TQueue> create();

  TQueue() = default;
  TQueue(const TQueue &) = delete;
  TQueue &operator=(const TQueue &) = delete;
  TQueue(TQueue &&) = delete;
  TQueue &operator=(TQueue &&) = delete;

  virtual ~TQueue() = default;

  virtual void set_callback(unique_ptr<Callback> callback) = 0;
  virtual unique_ptr<Callback> extract_callback() = 0;

  virtual void emulate_restart() = 0;  // for testing only

  virtual void do_push(QueueId queue_id, RawEvent &&raw_event) = 0;

  virtual Result<EventId> push(QueueId queue_id, string data, double expires_at, EventId new_id = EventId(),
                               int64 extra = 0) = 0;

  virtual void forget(QueueId queue_id, EventId event_id) = 0;

  virtual EventId get_head(QueueId queue_id) const = 0;
  virtual EventId get_tail(QueueId queue_id) const = 0;

  virtual Result<size_t> get(QueueId queue_id, EventId from_id, bool forget_previous, double now,
                             MutableSpan<Event> &result_events) = 0;

  virtual void run_gc(double now) = 0;
};

StringBuilder &operator<<(StringBuilder &sb, const TQueue::EventId id);

struct BinlogEvent;

template <class BinlogT>
class TQueueBinlog : public TQueue::Callback {
 public:
  TQueueBinlog();

  uint64 push(QueueId queue_id, const RawEvent &event) override;
  void pop(uint64 logevent_id) override;
  Status replay(const BinlogEvent &binlog_event, TQueue &q);

  void set_binlog(std::shared_ptr<BinlogT> binlog) {
    binlog_ = std::move(binlog);
  }

 private:
  std::shared_ptr<BinlogT> binlog_;
  int32 magic_{2314};
  double diff_{0};
};

class TQueueMemoryStorage : public TQueue::Callback {
 public:
  uint64 push(QueueId queue_id, const RawEvent &event) override;
  void pop(uint64 logevent_id) override;
  void replay(TQueue &q);

 private:
  uint64 next_logevent_id_{1};
  std::map<uint64, std::pair<QueueId, RawEvent>> events_;
};

}  // namespace td
