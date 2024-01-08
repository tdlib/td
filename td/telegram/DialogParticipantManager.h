//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class DialogParticipantManager final : public Actor {
 public:
  DialogParticipantManager(Td *td, ActorShared<> parent);

  static constexpr int32 ONLINE_MEMBER_COUNT_CACHE_EXPIRE_TIME = 30 * 60;

  void on_update_dialog_online_member_count(DialogId dialog_id, int32 online_member_count, bool is_from_server);

  void on_dialog_opened(DialogId dialog_id);

  void on_dialog_closed(DialogId dialog_id);

  void get_dialog_join_requests(DialogId dialog_id, const string &invite_link, const string &query,
                                td_api::object_ptr<td_api::chatJoinRequest> offset_request, int32 limit,
                                Promise<td_api::object_ptr<td_api::chatJoinRequests>> &&promise);

  void process_dialog_join_request(DialogId dialog_id, UserId user_id, bool approve, Promise<Unit> &&promise);

  void process_dialog_join_requests(DialogId dialog_id, const string &invite_link, bool approve,
                                    Promise<Unit> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void tear_down() final;

  static constexpr int32 ONLINE_MEMBER_COUNT_UPDATE_TIME = 5 * 60;

  static void on_update_dialog_online_member_count_timeout_callback(void *dialog_participant_manager_ptr,
                                                                    int64 dialog_id_int);

  void set_dialog_online_member_count(DialogId dialog_id, int32 online_member_count, bool is_from_server,
                                      const char *source);

  void on_update_dialog_online_member_count_timeout(DialogId dialog_id);

  void send_update_chat_online_member_count(DialogId dialog_id, int32 online_member_count) const;

  Status can_manage_dialog_join_requests(DialogId dialog_id);

  struct OnlineMemberCountInfo {
    int32 online_member_count = 0;
    double update_time = 0;
    bool is_update_sent = false;
  };
  FlatHashMap<DialogId, OnlineMemberCountInfo, DialogIdHash> dialog_online_member_counts_;

  MultiTimeout update_dialog_online_member_count_timeout_{"UpdateDialogOnlineMemberCountTimeout"};

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
