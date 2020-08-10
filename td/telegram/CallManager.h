//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CallActor.h"
#include "td/telegram/CallId.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/Status.h"

#include <map>
#include <unordered_map>

namespace td {

class CallManager : public Actor {
 public:
  using Update = telegram_api::object_ptr<telegram_api::updatePhoneCall>;
  explicit CallManager(ActorShared<> parent);
  void update_call(Update call);
  void update_call_signaling_data(int64 call_id, string data);

  void create_call(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user, CallProtocol &&protocol,
                   bool is_video, Promise<CallId> promise);
  void accept_call(CallId call_id, CallProtocol &&protocol, Promise<> promise);
  void send_call_signaling_data(CallId call_id, string &&data, Promise<> promise);
  void discard_call(CallId call_id, bool is_disconnected, int32 duration, bool is_video, int64 connection_id,
                    Promise<> promise);
  void rate_call(CallId call_id, int32 rating, string comment,
                 vector<td_api::object_ptr<td_api::CallProblem>> &&problems, Promise<> promise);
  void send_call_debug_information(CallId call_id, string data, Promise<> promise);

 private:
  bool close_flag_ = false;
  ActorShared<> parent_;

  struct CallInfo {
    CallId call_id{0};
    std::vector<Update> updates;
  };
  std::map<int64, CallInfo> call_info_;
  int32 next_call_id_{1};
  std::unordered_map<CallId, ActorOwn<CallActor>, CallIdHash> id_to_actor_;

  ActorId<CallActor> get_call_actor(CallId call_id);
  CallId create_call_actor();
  void set_call_id(CallId call_id, Result<int64> r_server_call_id);

  void hangup() override;
  void hangup_shared() override;
};
}  // namespace td
