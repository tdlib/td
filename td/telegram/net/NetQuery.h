//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryCounter.h"
#include "td/telegram/net/NetQueryStats.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SignalSlot.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/ObjectPool.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/TsList.h"

#include <atomic>
#include <utility>

namespace td {

extern int VERBOSITY_NAME(net_query);

class NetQuery;
using NetQueryPtr = ObjectPool<NetQuery>::OwnerPtr;
using NetQueryRef = ObjectPool<NetQuery>::WeakPtr;

class NetQueryCallback : public Actor {
 public:
  virtual void on_result(NetQueryPtr query);
  virtual void on_result_resendable(NetQueryPtr query, Promise<NetQueryPtr> promise);
};

class NetQuery : public TsListNode<NetQueryDebug> {
 public:
  NetQuery() = default;

  enum class State : int8 { Empty, Query, OK, Error };
  enum class Type : int8 { Common, Upload, Download, DownloadSmall };
  enum class AuthFlag : int8 { Off, On };
  enum class GzipFlag : int8 { Off, On };
  enum Error : int32 { Resend = 202, Cancelled = 203, ResendInvokeAfter = 204 };

  uint64 id() const {
    return id_;
  }

  DcId dc_id() const {
    return dc_id_;
  }

  Type type() const {
    return type_;
  }

  GzipFlag gzip_flag() const {
    return gzip_flag_;
  }

  AuthFlag auth_flag() const {
    return auth_flag_;
  }

  int32 tl_constructor() const {
    return tl_constructor_;
  }

  void resend(DcId new_dc_id) {
    VLOG(net_query) << "Resend" << *this;
    {
      auto guard = lock();
      get_data_unsafe().resend_count_++;
    }
    dc_id_ = new_dc_id;
    status_ = Status::OK();
    state_ = State::Query;
  }

  void resend() {
    resend(dc_id_);
  }

  BufferSlice &query() {
    return query_;
  }

  BufferSlice &ok() {
    CHECK(state_ == State::OK);
    return answer_;
  }

  const BufferSlice &ok() const {
    CHECK(state_ == State::OK);
    return answer_;
  }

  Status &error() {
    CHECK(state_ == State::Error);
    return status_;
  }

  const Status &error() const {
    CHECK(state_ == State::Error);
    return status_;
  }

  BufferSlice move_as_ok() {
    auto ok = std::move(answer_);
    clear();
    return ok;
  }
  Status move_as_error() TD_WARN_UNUSED_RESULT {
    auto status = std::move(status_);
    clear();
    return status;
  }

  void set_ok(BufferSlice slice) {
    VLOG(net_query) << "Got answer " << *this;
    CHECK(state_ == State::Query);
    answer_ = std::move(slice);
    state_ = State::OK;
  }

  void on_net_write(size_t size);
  void on_net_read(size_t size);

  void set_error(Status status, string source = string());

  void set_error_resend() {
    set_error_impl(Status::Error<Error::Resend>());
  }

  void set_error_cancelled() {
    set_error_impl(Status::Error<Error::Cancelled>());
  }

  void set_error_resend_invoke_after() {
    set_error_impl(Status::Error<Error::ResendInvokeAfter>());
  }

  bool update_is_ready() {
    if (state_ == State::Query) {
      if (cancellation_token_.load(std::memory_order_relaxed) == 0 || cancel_slot_.was_signal()) {
        set_error_cancelled();
        return true;
      }
      return false;
    }
    return true;
  }

  bool is_ready() const {
    return state_ != State::Query;
  }

  bool is_error() const {
    return state_ == State::Error;
  }

  bool is_ok() const {
    return state_ == State::OK;
  }

  int32 ok_tl_constructor() const {
    return tl_magic(answer_);
  }

  void ignore() const {
    status_.ignore();
  }

  uint64 session_id() const {
    return session_id_.load(std::memory_order_relaxed);
  }
  void set_session_id(uint64 session_id) {
    session_id_.store(session_id, std::memory_order_relaxed);
  }

  uint64 message_id() const {
    return message_id_;
  }
  void set_message_id(uint64 message_id) {
    message_id_ = message_id;
  }

  NetQueryRef invoke_after() const {
    return invoke_after_;
  }
  void set_invoke_after(NetQueryRef ref) {
    invoke_after_ = ref;
  }
  void set_session_rand(uint32 session_rand) {
    session_rand_ = session_rand;
  }
  uint32 session_rand() const {
    return session_rand_;
  }

  void cancel(int32 cancellation_token) {
    cancellation_token_.compare_exchange_strong(cancellation_token, 0, std::memory_order_relaxed);
  }
  void set_cancellation_token(int32 cancellation_token) {
    cancellation_token_.store(cancellation_token, std::memory_order_relaxed);
  }

  void clear() {
    if (!is_ready()) {
      auto guard = lock();
      LOG(ERROR) << "Destroy not ready query " << *this << " " << tag("state", get_data_unsafe().state_);
    }
    // TODO: CHECK if net_query is lost here
    cancel_slot_.close();
    *this = NetQuery();
  }
  bool empty() const {
    return state_ == State::Empty || !nq_counter_ || may_be_lost_;
  }

  void stop_track() {
    nq_counter_ = NetQueryCounter();
    remove();
  }

  void debug_send_failed() {
    auto guard = lock();
    get_data_unsafe().send_failed_count_++;
  }

  void debug(string state, bool may_be_lost = false) {
    may_be_lost_ = may_be_lost;
    VLOG(net_query) << *this << " " << tag("state", state);
    {
      auto guard = lock();
      auto &data = get_data_unsafe();
      data.state_ = std::move(state);
      data.state_timestamp_ = Time::now();
      data.state_change_count_++;
    }
  }

  void set_callback(ActorShared<NetQueryCallback> callback) {
    callback_ = std::move(callback);
  }

  ActorShared<NetQueryCallback> move_callback() {
    return std::move(callback_);
  }

  void start_migrate(int32 sched_id) {
    using ::td::start_migrate;
    start_migrate(cancel_slot_, sched_id);
  }
  void finish_migrate() {
    using ::td::finish_migrate;
    finish_migrate(cancel_slot_);
  }

  int8 priority() const {
    return priority_;
  }
  void set_priority(int8 priority) {
    priority_ = priority;
  }

 private:
  State state_ = State::Empty;
  Type type_ = Type::Common;
  AuthFlag auth_flag_ = AuthFlag::Off;
  GzipFlag gzip_flag_ = GzipFlag::Off;
  DcId dc_id_;

  NetQueryCounter nq_counter_;
  Status status_;
  uint64 id_ = 0;
  BufferSlice query_;
  BufferSlice answer_;
  int32 tl_constructor_ = 0;

  NetQueryRef invoke_after_;
  uint32 session_rand_ = 0;

  bool may_be_lost_ = false;
  int8 priority_{0};

  template <class T>
  struct movable_atomic : public std::atomic<T> {
    movable_atomic() = default;
    movable_atomic(T &&x) : std::atomic<T>(std::forward<T>(x)) {
    }
    movable_atomic(movable_atomic &&other) {
      this->store(other.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    movable_atomic &operator=(movable_atomic &&other) {
      this->store(other.load(std::memory_order_relaxed), std::memory_order_relaxed);
      return *this;
    }
    movable_atomic(const movable_atomic &other) = delete;
    movable_atomic &operator=(const movable_atomic &other) = delete;
    ~movable_atomic() = default;
  };

  movable_atomic<uint64> session_id_{0};
  uint64 message_id_{0};

  movable_atomic<int32> cancellation_token_{-1};  // == 0 if query is canceled
  ActorShared<NetQueryCallback> callback_;

  void set_error_impl(Status status, string source = string()) {
    VLOG(net_query) << "Got error " << *this << " " << status;
    status_ = std::move(status);
    state_ = State::Error;
    source_ = std::move(source);
  }

  static int32 get_my_id();

  static int32 tl_magic(const BufferSlice &buffer_slice);

 public:
  double next_timeout_ = 1;          // for NetQueryDelayer
  double total_timeout_ = 0;         // for NetQueryDelayer/SequenceDispatcher
  double total_timeout_limit_ = 60;  // for NetQueryDelayer/SequenceDispatcher and to be set by caller
  double last_timeout_ = 0;          // for NetQueryDelayer/SequenceDispatcher
  string source_;                    // for NetQueryDelayer/SequenceDispatcher
  bool need_resend_on_503_ = true;   // for NetQueryDispatcher and to be set by caller
  int32 dispatch_ttl_ = -1;          // for NetQueryDispatcher and to be set by caller
  Slot cancel_slot_;                 // for Session and to be set by caller
  Promise<> quick_ack_promise_;      // for Session and to be set by caller
  int32 file_type_ = -1;             // to be set by caller

  NetQuery(State state, uint64 id, BufferSlice &&query, BufferSlice &&answer, DcId dc_id, Type type, AuthFlag auth_flag,
           GzipFlag gzip_flag, int32 tl_constructor, double total_timeout_limit, NetQueryStats *stats)
      : state_(state)
      , type_(type)
      , auth_flag_(auth_flag)
      , gzip_flag_(gzip_flag)
      , dc_id_(dc_id)
      , status_()
      , id_(id)
      , query_(std::move(query))
      , answer_(std::move(answer))
      , tl_constructor_(tl_constructor)
      , total_timeout_limit_(total_timeout_limit) {
    auto &data = get_data_unsafe();
    data.my_id_ = get_my_id();
    data.start_timestamp_ = data.state_timestamp_ = Time::now();
    LOG(INFO) << *this;
    if (stats) {
      nq_counter_ = stats->register_query(this);
    }
  }
};

inline StringBuilder &operator<<(StringBuilder &stream, const NetQuery &net_query) {
  stream << "[Query:";
  stream << tag("id", net_query.id());
  stream << tag("tl", format::as_hex(net_query.tl_constructor()));
  if (!net_query.is_ready()) {
    stream << tag("state", "Query");
  } else if (net_query.is_error()) {
    stream << tag("state", "Error");
    stream << net_query.error();
  } else if (net_query.is_ok()) {
    stream << tag("state", "Result");
    stream << tag("tl", format::as_hex(net_query.ok_tl_constructor()));
  }
  stream << "]";
  return stream;
}

inline StringBuilder &operator<<(StringBuilder &stream, const NetQueryPtr &net_query_ptr) {
  return stream << *net_query_ptr;
}

void dump_pending_network_queries();

inline void cancel_query(NetQueryRef &ref) {
  if (ref.empty()) {
    return;
  }
  ref->cancel(ref.generation());
}

template <class T>
Result<typename T::ReturnType> fetch_result(const BufferSlice &message) {
  TlBufferParser parser(&message);
  auto result = T::fetch_result(parser);
  parser.fetch_end();

  const char *error = parser.get_error();
  if (error != nullptr) {
    LOG(ERROR) << "Can't parse: " << format::as_hex_dump<4>(message.as_slice());
    return Status::Error(500, Slice(error));
  }

  return std::move(result);
}

template <class T>
Result<typename T::ReturnType> fetch_result(NetQueryPtr query) {
  CHECK(!query.empty());
  if (query->is_error()) {
    return query->move_as_error();
  }
  auto buffer = query->move_as_ok();
  return fetch_result<T>(buffer);
}

template <class T>
Result<typename T::ReturnType> fetch_result(Result<NetQueryPtr> r_query) {
  TRY_RESULT(query, std::move(r_query));
  return fetch_result<T>(std::move(query));
}

inline void NetQueryCallback::on_result(NetQueryPtr query) {
  on_result_resendable(std::move(query), Auto());
}

inline void NetQueryCallback::on_result_resendable(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  on_result(std::move(query));
}

inline void start_migrate(NetQueryPtr &net_query, int32 sched_id) {
  net_query->start_migrate(sched_id);
}

inline void finish_migrate(NetQueryPtr &net_query) {
  net_query->finish_migrate();
}

}  // namespace td
