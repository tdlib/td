//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Random.h"

#include <limits>
#include <unordered_map>

namespace td {

class SequenceDispatcher : public NetQueryCallback {
 public:
  class Parent : public Actor {
   public:
    virtual void ready_to_close() = 0;
    virtual void on_result() = 0;
  };
  SequenceDispatcher() = default;
  explicit SequenceDispatcher(ActorShared<Parent> parent) : parent_(std::move(parent)) {
  }
  void send_with_callback(NetQueryPtr query, ActorShared<NetQueryCallback> callback);
  void on_result(NetQueryPtr query) override;
  void close_silent();

 private:
  enum class State : int32 { Start, Wait, Finish, Dummy };
  struct Data {
    State state_;
    NetQueryRef net_query_ref_;
    NetQueryPtr query_;
    ActorShared<NetQueryCallback> callback_;
    uint64 generation_;
    double total_timeout_;
    double last_timeout_;
  };

  ActorShared<Parent> parent_;
  size_t id_offset_ = 1;
  std::vector<Data> data_;
  size_t finish_i_ = 0;  // skip state_ == State::Finish
  size_t next_i_ = 0;
  size_t last_sent_i_ = std::numeric_limits<size_t>::max();
  uint64 generation_ = 1;
  uint32 session_rand_ = Random::secure_int32();

  static constexpr int32 MAX_SIMULTANEOUS_WAIT = 10;
  uint32 wait_cnt_ = 0;

  void check_timeout(Data &data);

  void try_resend_query(Data &data, NetQueryPtr query);
  Data &data_from_token();
  void on_resend_ok(NetQueryPtr query);
  void on_resend_error();
  void do_resend(Data &data);
  void do_finish(Data &data);

  void loop() override;
  void try_shrink();

  void timeout_expired() override;
  void hangup() override;
  void tear_down() override;
};

class MultiSequenceDispatcher : public SequenceDispatcher::Parent {
 public:
  void send_with_callback(NetQueryPtr query, ActorShared<NetQueryCallback> callback, uint64 sequence_id);

 private:
  struct Data {
    int32 cnt_;
    ActorOwn<SequenceDispatcher> dispatcher_;
  };
  std::unordered_map<uint64, Data> dispatchers_;
  void on_result() override;
  void ready_to_close() override;
};

}  // namespace td
