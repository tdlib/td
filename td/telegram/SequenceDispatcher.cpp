//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SequenceDispatcher.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

#include <limits>

namespace td {

/*** Sequence Dispatcher ***/
// Sends queries with invokeAfter.
//
// Each query has three states Start/Wait/Finish
//
// finish_i points to the first not Finish query.
// next_i points to the next query to be sent.
//
// Each query has generation of InvokeAfter chain.
//
// When query is send, its generation is set to current chain generation.
//
// When query is failed and its generation is equals to current generation we must start new chain:
// increment the generation and set next_i to finish_i.
//
// last_sent_i points to the last sent query in current chain.
//
void SequenceDispatcher::send_with_callback(NetQueryPtr query, ActorShared<NetQueryCallback> callback) {
  cancel_timeout();
  query->debug("Waiting at SequenceDispatcher");
  auto query_weak_ref = query.get_weak();
  data_.push_back(Data{State::Start, std::move(query_weak_ref), std::move(query), std::move(callback), 0, 0.0, 0.0});
  loop();
}

void SequenceDispatcher::check_timeout(Data &data) {
  if (data.state_ != State::Start) {
    return;
  }
  data.query_->total_timeout_ += data.total_timeout_;
  data.total_timeout_ = 0;
  if (data.query_->total_timeout_ > data.query_->total_timeout_limit_) {
    LOG(WARNING) << "Fail " << data.query_ << " to " << data.query_->source_ << " because total_timeout "
                 << data.query_->total_timeout_ << " is greater than total_timeout_limit "
                 << data.query_->total_timeout_limit_;
    data.query_->set_error(Status::Error(
        429, PSLICE() << "Too Many Requests: retry after " << static_cast<int32>(data.last_timeout_ + 0.999)));
    data.state_ = State::Dummy;
    try_resend_query(data, std::move(data.query_));
  }
}

void SequenceDispatcher::try_resend_query(Data &data, NetQueryPtr query) {
  size_t pos = &data - &data_[0];
  CHECK(pos < data_.size());
  CHECK(data.state_ == State::Dummy);
  data.state_ = State::Wait;
  wait_cnt_++;
  auto token = pos + id_offset_;
  // TODO: if query is ok, use NetQueryCallback::on_result
  auto promise = PromiseCreator::lambda([&, self = actor_shared(this, token)](NetQueryPtr query) mutable {
    if (!query.empty()) {
      send_closure(std::move(self), &SequenceDispatcher::on_resend_ok, std::move(query));
    } else {
      send_closure(std::move(self), &SequenceDispatcher::on_resend_error);
    }
  });
  send_closure(data.callback_, &NetQueryCallback::on_result_resendable, std::move(query), std::move(promise));
}

SequenceDispatcher::Data &SequenceDispatcher::data_from_token() {
  auto token = narrow_cast<size_t>(get_link_token());
  auto pos = token - id_offset_;
  CHECK(pos < data_.size());
  auto &data = data_[pos];
  CHECK(data.state_ == State::Wait);
  CHECK(wait_cnt_ > 0);
  wait_cnt_--;
  data.state_ = State::Dummy;
  return data;
}

void SequenceDispatcher::on_resend_ok(NetQueryPtr query) {
  auto &data = data_from_token();
  data.query_ = std::move(query);
  do_resend(data);
  loop();
}

void SequenceDispatcher::on_resend_error() {
  auto &data = data_from_token();
  do_finish(data);
  loop();
}

void SequenceDispatcher::do_resend(Data &data) {
  CHECK(data.state_ == State::Dummy);
  data.state_ = State::Start;
  if (data.generation_ == generation_) {
    next_i_ = finish_i_;
    generation_++;
    last_sent_i_ = std::numeric_limits<size_t>::max();
  }
  check_timeout(data);
}

void SequenceDispatcher::do_finish(Data &data) {
  CHECK(data.state_ == State::Dummy);
  data.state_ = State::Finish;
  if (!parent_.empty()) {
    send_closure(parent_, &Parent::on_result);
  }
}

void SequenceDispatcher::on_result(NetQueryPtr query) {
  auto &data = data_from_token();
  size_t pos = &data - &data_[0];
  CHECK(pos < data_.size());

  if (query->last_timeout_ != 0) {
    for (auto i = pos + 1; i < data_.size(); i++) {
      data_[i].total_timeout_ += query->last_timeout_;
      data_[i].last_timeout_ = query->last_timeout_;
      check_timeout(data_[i]);
    }
  }

  if (query->is_error() && (query->error().code() == NetQuery::ResendInvokeAfter ||
                            (query->error().code() == 400 && query->error().message() == "MSG_WAIT_FAILED"))) {
    VLOG(net_query) << "Resend " << query;
    query->resend();
    query->debug("Waiting at SequenceDispatcher");
    data.query_ = std::move(query);
    do_resend(data);
  } else {
    try_resend_query(data, std::move(query));
  }
  loop();
}

void SequenceDispatcher::loop() {
  for (; finish_i_ < data_.size() && data_[finish_i_].state_ == State::Finish; finish_i_++) {
  }
  if (next_i_ < finish_i_) {
    next_i_ = finish_i_;
  }
  for (; next_i_ < data_.size() && data_[next_i_].state_ != State::Wait && wait_cnt_ < MAX_SIMULTANEOUS_WAIT;
       next_i_++) {
    if (data_[next_i_].state_ == State::Finish) {
      continue;
    }
    NetQueryRef invoke_after;
    if (last_sent_i_ != std::numeric_limits<size_t>::max() && data_[last_sent_i_].state_ == State::Wait) {
      invoke_after = data_[last_sent_i_].net_query_ref_;
    }
    data_[next_i_].query_->set_invoke_after(invoke_after);
    data_[next_i_].query_->last_timeout_ = 0;

    VLOG(net_query) << "Send " << data_[next_i_].query_;

    data_[next_i_].query_->debug("send to Td::send_with_callback");
    data_[next_i_].query_->set_session_rand(session_rand_);
    G()->net_query_dispatcher().dispatch_with_callback(std::move(data_[next_i_].query_),
                                                       actor_shared(this, next_i_ + id_offset_));
    data_[next_i_].state_ = State::Wait;
    wait_cnt_++;
    data_[next_i_].generation_ = generation_;
    last_sent_i_ = next_i_;
  }

  try_shrink();

  if (finish_i_ == data_.size() && !parent_.empty()) {
    set_timeout_in(5);
  }
}

void SequenceDispatcher::try_shrink() {
  if (finish_i_ * 2 > data_.size() && data_.size() > 5) {
    CHECK(finish_i_ <= next_i_);
    data_.erase(data_.begin(), data_.begin() + finish_i_);
    next_i_ -= finish_i_;
    if (last_sent_i_ != std::numeric_limits<size_t>::max()) {
      if (last_sent_i_ >= finish_i_) {
        last_sent_i_ -= finish_i_;
      } else {
        last_sent_i_ = std::numeric_limits<size_t>::max();
      }
    }
    id_offset_ += finish_i_;
    finish_i_ = 0;
  }
}

void SequenceDispatcher::timeout_expired() {
  if (finish_i_ != data_.size()) {
    return;
  }
  CHECK(!parent_.empty());
  set_timeout_in(1);
  LOG(DEBUG) << "SequenceDispatcher ready to close";
  send_closure(parent_, &Parent::ready_to_close);
}

void SequenceDispatcher::hangup() {
  stop();
}

void SequenceDispatcher::tear_down() {
  for (auto &data : data_) {
    if (data.query_.empty()) {
      continue;
    }
    data.state_ = State::Dummy;
    data.query_->set_error(Status::Error(500, "Request aborted"));
    do_finish(data);
  }
}

void SequenceDispatcher::close_silent() {
  for (auto &data : data_) {
    if (!data.query_.empty()) {
      data.query_->clear();
    }
  }
  stop();
}

/*** MultiSequenceDispatcher ***/
void MultiSequenceDispatcher::send_with_callback(NetQueryPtr query, ActorShared<NetQueryCallback> callback,
                                                 uint64 sequence_id) {
  CHECK(sequence_id != 0);
  auto it_ok = dispatchers_.emplace(sequence_id, Data{0, ActorOwn<SequenceDispatcher>()});
  auto &data = it_ok.first->second;
  if (it_ok.second) {
    LOG(DEBUG) << "Create SequenceDispatcher" << sequence_id;
    data.dispatcher_ = create_actor<SequenceDispatcher>("sequence dispatcher", actor_shared(this, sequence_id));
  }
  data.cnt_++;
  query->debug(PSTRING() << "send to SequenceDispatcher " << tag("sequence_id", sequence_id));
  send_closure(data.dispatcher_, &SequenceDispatcher::send_with_callback, std::move(query), std::move(callback));
}

void MultiSequenceDispatcher::on_result() {
  auto it = dispatchers_.find(get_link_token());
  CHECK(it != dispatchers_.end());
  it->second.cnt_--;
}

void MultiSequenceDispatcher::ready_to_close() {
  auto it = dispatchers_.find(get_link_token());
  CHECK(it != dispatchers_.end());
  if (it->second.cnt_ == 0) {
    LOG(DEBUG) << "Close SequenceDispatcher " << get_link_token();
    dispatchers_.erase(it);
  }
}

}  // namespace td
