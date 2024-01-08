//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogAdministrator.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"

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
  DialogParticipantManager(const DialogParticipantManager &) = delete;
  DialogParticipantManager &operator=(const DialogParticipantManager &) = delete;
  DialogParticipantManager(DialogParticipantManager &&) = delete;
  DialogParticipantManager &operator=(DialogParticipantManager &&) = delete;
  ~DialogParticipantManager() final;

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

  void speculative_update_dialog_administrators(DialogId dialog_id, UserId user_id,
                                                const DialogParticipantStatus &new_status,
                                                const DialogParticipantStatus &old_status);

  void on_update_dialog_administrators(DialogId dialog_id, vector<DialogAdministrator> &&administrators,
                                       bool have_access, bool from_database);

  void get_dialog_administrators(DialogId dialog_id, Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise);

  void reload_dialog_administrators(DialogId dialog_id, const vector<DialogAdministrator> &dialog_administrators,
                                    Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise);

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

  td_api::object_ptr<td_api::chatAdministrators> get_chat_administrators_object(
      const vector<DialogAdministrator> &dialog_administrators);

  static string get_dialog_administrators_database_key(DialogId dialog_id);

  void on_load_dialog_administrators_from_database(DialogId dialog_id, string value,
                                                   Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise);

  void on_load_administrator_users_finished(DialogId dialog_id, vector<DialogAdministrator> administrators,
                                            Result<Unit> result,
                                            Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise);

  void on_reload_dialog_administrators(DialogId dialog_id,
                                       Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise);

  struct OnlineMemberCountInfo {
    int32 online_member_count = 0;
    double update_time = 0;
    bool is_update_sent = false;
  };
  FlatHashMap<DialogId, OnlineMemberCountInfo, DialogIdHash> dialog_online_member_counts_;

  MultiTimeout update_dialog_online_member_count_timeout_{"UpdateDialogOnlineMemberCountTimeout"};

  FlatHashMap<DialogId, vector<DialogAdministrator>, DialogIdHash> dialog_administrators_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
