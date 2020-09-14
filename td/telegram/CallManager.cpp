//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CallManager.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include "td/telegram/telegram_api.hpp"

#include <limits>

namespace td {

CallManager::CallManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

void CallManager::update_call(Update call) {
  int64 call_id = 0;
  downcast_call(*call->phone_call_, [&](auto &update) { call_id = update.id_; });
  LOG(DEBUG) << "Receive UpdateCall for " << call_id;

  auto &info = call_info_[call_id];

  if (!info.call_id.is_valid() && call->phone_call_->get_id() == telegram_api::phoneCallRequested::ID) {
    info.call_id = create_call_actor();
  }

  if (!info.call_id.is_valid()) {
    LOG(INFO) << "Call_id is not valid for " << call_id << ", postpone update " << to_string(call);
    info.updates.push_back(std::move(call));
    return;
  }

  auto actor = get_call_actor(info.call_id);
  if (actor.empty()) {
    LOG(INFO) << "Drop update: " << to_string(call);
  }
  send_closure(actor, &CallActor::update_call, std::move(call->phone_call_));
}

void CallManager::update_call_signaling_data(int64 call_id, string data) {
  auto info_it = call_info_.find(call_id);
  if (info_it == call_info_.end() || !info_it->second.call_id.is_valid()) {
    LOG(INFO) << "Ignore signaling data for " << call_id;
    return;
  }

  auto actor = get_call_actor(info_it->second.call_id);
  if (actor.empty()) {
    LOG(INFO) << "Ignore signaling data for " << info_it->second.call_id;
    return;
  }
  send_closure(actor, &CallActor::update_call_signaling_data, std::move(data));
}

void CallManager::create_call(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
                              CallProtocol &&protocol, bool is_video, Promise<CallId> promise) {
  LOG(INFO) << "Create call with " << user_id;
  auto call_id = create_call_actor();
  auto actor = get_call_actor(call_id);
  CHECK(!actor.empty());
  send_closure(actor, &CallActor::create_call, user_id, std::move(input_user), std::move(protocol), is_video,
               std::move(promise));
}

void CallManager::accept_call(CallId call_id, CallProtocol &&protocol, Promise<> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(Status::Error(400, "Call not found"));
  }
  send_closure(actor, &CallActor::accept_call, std::move(protocol), std::move(promise));
}

void CallManager::send_call_signaling_data(CallId call_id, string &&data, Promise<> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(Status::Error(400, "Call not found"));
  }
  send_closure(actor, &CallActor::send_call_signaling_data, std::move(data), std::move(promise));
}

void CallManager::discard_call(CallId call_id, bool is_disconnected, int32 duration, bool is_video, int64 connection_id,
                               Promise<> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(Status::Error(400, "Call not found"));
  }
  send_closure(actor, &CallActor::discard_call, is_disconnected, duration, is_video, connection_id, std::move(promise));
}

void CallManager::rate_call(CallId call_id, int32 rating, string comment,
                            vector<td_api::object_ptr<td_api::CallProblem>> &&problems, Promise<> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(Status::Error(400, "Call not found"));
  }
  send_closure(actor, &CallActor::rate_call, rating, std::move(comment), std::move(problems), std::move(promise));
}

void CallManager::send_call_debug_information(CallId call_id, string data, Promise<> promise) {
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return promise.set_error(Status::Error(400, "Call not found"));
  }
  send_closure(actor, &CallActor::send_call_debug_information, std::move(data), std::move(promise));
}

CallId CallManager::create_call_actor() {
  if (next_call_id_ == std::numeric_limits<int32>::max()) {
    next_call_id_ = 1;
  }
  auto id = CallId(next_call_id_++);
  CHECK(id.is_valid());
  auto it_flag = id_to_actor_.emplace(id, ActorOwn<CallActor>());
  CHECK(it_flag.second);
  LOG(INFO) << "Create CallActor: " << id;
  auto main_promise = PromiseCreator::lambda([actor_id = actor_id(this), id](Result<int64> call_id) {
    send_closure(actor_id, &CallManager::set_call_id, id, std::move(call_id));
  });
  it_flag.first->second = create_actor<CallActor>(PSLICE() << "Call " << id.get(), id, actor_shared(this, id.get()),
                                                  std::move(main_promise));
  return id;
}

void CallManager::set_call_id(CallId call_id, Result<int64> r_server_call_id) {
  if (r_server_call_id.is_error()) {
    return;
  }
  auto server_call_id = r_server_call_id.move_as_ok();
  auto &call_info = call_info_[server_call_id];
  CHECK(!call_info.call_id.is_valid() || call_info.call_id == call_id);
  call_info.call_id = call_id;
  auto actor = get_call_actor(call_id);
  if (actor.empty()) {
    return;
  }
  for (auto &update : call_info.updates) {
    send_closure(actor, &CallActor::update_call, std::move(update->phone_call_));
  }
  call_info.updates.clear();
}

ActorId<CallActor> CallManager::get_call_actor(CallId call_id) {
  auto it = id_to_actor_.find(call_id);
  if (it == id_to_actor_.end()) {
    return ActorId<CallActor>();
  }
  return it->second.get();
}

void CallManager::hangup() {
  close_flag_ = true;
  for (auto &it : id_to_actor_) {
    LOG(INFO) << "Ask close CallActor " << it.first;
    it.second.reset();
  }
  if (id_to_actor_.empty()) {
    stop();
  }
}
void CallManager::hangup_shared() {
  auto token = narrow_cast<int32>(get_link_token());
  auto it = id_to_actor_.find(CallId(token));
  if (it != id_to_actor_.end()) {
    LOG(INFO) << "Close CallActor " << tag("id", it->first);
    it->second.release();
    id_to_actor_.erase(it);
  } else {
    LOG(FATAL) << "Unknown CallActor hangup " << tag("id", static_cast<int32>(token));
  }
  if (close_flag_ && id_to_actor_.empty()) {
    stop();
  }
}
}  // namespace td
