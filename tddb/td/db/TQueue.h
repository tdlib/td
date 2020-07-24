//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

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

    bool is_valid() const;

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

    static bool is_valid_id(int32 id);
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

  class StorageCallback {
   public:
    using QueueId = TQueue::QueueId;
    using RawEvent = TQueue::RawEvent;

    StorageCallback() = default;
    StorageCallback(const StorageCallback &) = delete;
    StorageCallback &operator=(const StorageCallback &) = delete;
    StorageCallback(StorageCallback &&) = delete;
    StorageCallback &operator=(StorageCallback &&) = delete;
    virtual ~StorageCallback() = default;

    virtual uint64 push(QueueId queue_id, const RawEvent &event) = 0;
    virtual void pop(uint64 logevent_id) = 0;
    virtual void close(Promise<> promise) = 0;
  };

  static unique_ptr<TQueue> create();

  TQueue() = default;
  TQueue(const TQueue &) = delete;
  TQueue &operator=(const TQueue &) = delete;
  TQueue(TQueue &&) = delete;
  TQueue &operator=(TQueue &&) = delete;

  virtual ~TQueue() = default;

  virtual void set_callback(unique_ptr<StorageCallback> callback) = 0;
  virtual unique_ptr<StorageCallback> extract_callback() = 0;

  virtual bool do_push(QueueId queue_id, RawEvent &&raw_event) = 0;

  virtual Result<EventId> push(QueueId queue_id, string data, double expires_at, int64 extra, EventId hint_new_id) = 0;

  virtual void forget(QueueId queue_id, EventId event_id) = 0;

  virtual EventId get_head(QueueId queue_id) const = 0;
  virtual EventId get_tail(QueueId queue_id) const = 0;

  virtual Result<size_t> get(QueueId queue_id, EventId from_id, bool forget_previous, double now,
                             MutableSpan<Event> &result_events) = 0;

  virtual size_t get_size(QueueId queue_id) = 0;

  virtual std::pair<uint64, uint64> run_gc(double now) = 0;
  virtual void close(Promise<> promise) = 0;
};

StringBuilder &operator<<(StringBuilder &string_builder, const TQueue::EventId id);

struct BinlogEvent;

template <class BinlogT>
class TQueueBinlog : public TQueue::StorageCallback {
 public:
  TQueueBinlog();

  uint64 push(QueueId queue_id, const RawEvent &event) override;
  void pop(uint64 logevent_id) override;
  Status replay(const BinlogEvent &binlog_event, TQueue &q) const TD_WARN_UNUSED_RESULT;

  void set_binlog(std::shared_ptr<BinlogT> binlog) {
    binlog_ = std::move(binlog);
  }
  virtual void close(Promise<> promise) override;

 private:
  std::shared_ptr<BinlogT> binlog_;
  int32 magic_{2314};
  double diff_{0};
};

class TQueueMemoryStorage : public TQueue::StorageCallback {
 public:
  uint64 push(QueueId queue_id, const RawEvent &event) override;
  void pop(uint64 logevent_id) override;
  void replay(TQueue &q) const;
  virtual void close(Promise<> promise) override;

 private:
  uint64 next_logevent_id_{1};
  std::map<uint64, std::pair<QueueId, RawEvent>> events_;
};

}  // namespace td
