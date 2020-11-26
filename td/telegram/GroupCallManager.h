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

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include <unordered_map>

namespace td {

class Td;

class GroupCallManager : public Actor {
 public:
  GroupCallManager(Td *td, ActorShared<> parent);

  void create_group_call(ChannelId channel_id, Promise<InputGroupCallId> &&promise);

  void leave_group_call(InputGroupCallId group_call_id, int32 source, Promise<Unit> &&promise);

  void discard_group_call(InputGroupCallId group_call_id, Promise<Unit> &&promise);

  void on_update_group_call(tl_object_ptr<telegram_api::GroupCall> group_call_ptr);

 private:
  struct GroupCall;

  void tear_down() override;

  InputGroupCallId update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr);

  static tl_object_ptr<td_api::updateGroupCall> get_update_group_call_object(InputGroupCallId group_call_id,
                                                                             const GroupCall *group_call);

  static tl_object_ptr<td_api::groupCall> get_group_call_object(InputGroupCallId group_call_id,
                                                                const GroupCall *group_call);

  Td *td_;
  ActorShared<> parent_;

  std::unordered_map<InputGroupCallId, unique_ptr<GroupCall>, InputGroupCallIdHash> group_calls_;
};

}  // namespace td
