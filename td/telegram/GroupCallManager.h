//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/InputGroupCallId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include <unordered_map>

namespace td {

class Td;

class GroupCallManager : public Actor {
 public:
  GroupCallManager(Td *td, ActorShared<> parent);
  GroupCallManager(const GroupCallManager &) = delete;
  GroupCallManager &operator=(const GroupCallManager &) = delete;
  GroupCallManager(GroupCallManager &&) = delete;
  GroupCallManager &operator=(GroupCallManager &&) = delete;
  ~GroupCallManager() override;

  void create_voice_chat(ChannelId channel_id, Promise<InputGroupCallId> &&promise);

  void join_group_call(InputGroupCallId group_call_id, td_api::object_ptr<td_api::groupCallPayload> &&payload,
                       int32 source, bool is_muted,
                       Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> &&promise);

  void toggle_group_call_mute_new_members(InputGroupCallId group_call_id, bool mute_new_members,
                                          Promise<Unit> &&promise);

  void invite_group_call_members(InputGroupCallId group_call_id, vector<UserId> &&user_ids, Promise<Unit> &&promise);

  void toggle_group_call_member_is_muted(InputGroupCallId group_call_id, UserId user_id, bool is_muted,
                                         Promise<Unit> &&promise);

  void check_group_call_source(InputGroupCallId group_call_id, int32 source, Promise<Unit> &&promise);

  void leave_group_call(InputGroupCallId group_call_id, int32 source, Promise<Unit> &&promise);

  void discard_group_call(InputGroupCallId group_call_id, Promise<Unit> &&promise);

  void on_update_group_call(tl_object_ptr<telegram_api::GroupCall> group_call_ptr);

  void process_join_group_call_response(InputGroupCallId group_call_id, uint64 generation,
                                        tl_object_ptr<telegram_api::Updates> &&updates, Promise<Unit> &&promise);

 private:
  struct GroupCall;
  struct PendingJoinRequest;

  void tear_down() override;

  void on_join_group_call_response(InputGroupCallId group_call_id, string json_response);

  void finish_join_group_call(InputGroupCallId group_call_id, uint64 generation, Status error);

  InputGroupCallId update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr);

  static Result<td_api::object_ptr<td_api::groupCallJoinResponse>> get_group_call_join_response_object(
      string json_response);

  static tl_object_ptr<td_api::updateGroupCall> get_update_group_call_object(InputGroupCallId group_call_id,
                                                                             const GroupCall *group_call);

  static tl_object_ptr<td_api::groupCall> get_group_call_object(InputGroupCallId group_call_id,
                                                                const GroupCall *group_call);

  Td *td_;
  ActorShared<> parent_;

  std::unordered_map<InputGroupCallId, unique_ptr<GroupCall>, InputGroupCallIdHash> group_calls_;

  std::unordered_map<InputGroupCallId, unique_ptr<PendingJoinRequest>, InputGroupCallIdHash> pending_join_requests_;
  uint64 join_group_request_generation_ = 0;
};

}  // namespace td
