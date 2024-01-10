//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/DialogAdministrator.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogInviteLink.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
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

  void on_update_bot_stopped(UserId user_id, int32 date, bool is_stopped, bool force = false);

  void on_update_chat_participant(ChatId chat_id, UserId user_id, int32 date, DialogInviteLink invite_link,
                                  telegram_api::object_ptr<telegram_api::ChatParticipant> old_participant,
                                  telegram_api::object_ptr<telegram_api::ChatParticipant> new_participant);

  void on_update_channel_participant(ChannelId channel_id, UserId user_id, int32 date, DialogInviteLink invite_link,
                                     bool via_dialog_filter_invite_link,
                                     telegram_api::object_ptr<telegram_api::ChannelParticipant> old_participant,
                                     telegram_api::object_ptr<telegram_api::ChannelParticipant> new_participant);

  void on_update_chat_invite_requester(DialogId dialog_id, UserId user_id, string about, int32 date,
                                       DialogInviteLink invite_link);

  void get_dialog_participant(DialogId dialog_id, DialogId participant_dialog_id,
                              Promise<td_api::object_ptr<td_api::chatMember>> &&promise);

  void get_channel_participant(ChannelId channel_id, DialogId participant_dialog_id,
                               Promise<DialogParticipant> &&promise);

  void add_dialog_participant(DialogId dialog_id, UserId user_id, int32 forward_limit, Promise<Unit> &&promise);

  void add_dialog_participants(DialogId dialog_id, const vector<UserId> &user_ids, Promise<Unit> &&promise);

  void set_dialog_participant_status(DialogId dialog_id, DialogId participant_dialog_id,
                                     td_api::object_ptr<td_api::ChatMemberStatus> &&chat_member_status,
                                     Promise<Unit> &&promise);

  void ban_dialog_participant(DialogId dialog_id, DialogId participant_dialog_id, int32 banned_until_date,
                              bool revoke_messages, Promise<Unit> &&promise);

  void leave_dialog(DialogId dialog_id, Promise<Unit> &&promise);

  void on_set_channel_participant_status(ChannelId channel_id, DialogId participant_dialog_id,
                                         DialogParticipantStatus status);

  void send_update_add_chat_members_privacy_forbidden(DialogId dialog_id, vector<UserId> user_ids, const char *source);

  bool have_channel_participant_cache(ChannelId channel_id) const;

  void add_channel_participant_to_cache(ChannelId channel_id, const DialogParticipant &dialog_participant,
                                        bool allow_replace);

  void drop_channel_participant_cache(ChannelId channel_id);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  void tear_down() final;

  static constexpr int32 ONLINE_MEMBER_COUNT_UPDATE_TIME = 5 * 60;

  static constexpr int32 CHANNEL_PARTICIPANT_CACHE_TIME = 1800;  // some reasonable limit

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

  void send_update_chat_member(DialogId dialog_id, UserId agent_user_id, int32 date,
                               const DialogInviteLink &invite_link, bool via_dialog_filter_invite_link,
                               const DialogParticipant &old_dialog_participant,
                               const DialogParticipant &new_dialog_participant);

  void do_get_dialog_participant(DialogId dialog_id, DialogId participant_dialog_id,
                                 Promise<DialogParticipant> &&promise);

  void finish_get_dialog_participant(DialogParticipant &&dialog_participant,
                                     Promise<td_api::object_ptr<td_api::chatMember>> &&promise);

  void finish_get_channel_participant(ChannelId channel_id, DialogParticipant &&dialog_participant,
                                      Promise<DialogParticipant> &&promise);

  void add_channel_participant(ChannelId channel_id, UserId user_id, const DialogParticipantStatus &old_status,
                               Promise<Unit> &&promise);

  void on_join_channel(ChannelId channel_id, Result<Unit> &&result);

  void add_channel_participants(ChannelId channel_id, const vector<UserId> &user_ids, Promise<Unit> &&promise);

  void set_channel_participant_status(ChannelId channel_id, DialogId participant_dialog_id,
                                      td_api::object_ptr<td_api::ChatMemberStatus> &&chat_member_status,
                                      Promise<Unit> &&promise);

  void set_channel_participant_status_impl(ChannelId channel_id, DialogId participant_dialog_id,
                                           DialogParticipantStatus new_status, DialogParticipantStatus old_status,
                                           Promise<Unit> &&promise);

  void promote_channel_participant(ChannelId channel_id, UserId user_id, const DialogParticipantStatus &new_status,
                                   const DialogParticipantStatus &old_status, Promise<Unit> &&promise);

  void restrict_channel_participant(ChannelId channel_id, DialogId participant_dialog_id,
                                    DialogParticipantStatus &&new_status, DialogParticipantStatus &&old_status,
                                    Promise<Unit> &&promise);

  void speculative_add_channel_user(ChannelId channel_id, UserId user_id, const DialogParticipantStatus &new_status,
                                    const DialogParticipantStatus &old_status);

  void update_channel_participant_status_cache(ChannelId channel_id, DialogId participant_dialog_id,
                                               DialogParticipantStatus &&dialog_participant_status);

  const DialogParticipant *get_channel_participant_from_cache(ChannelId channel_id, DialogId participant_dialog_id);

  static void on_channel_participant_cache_timeout_callback(void *dialog_participant_manager_ptr,
                                                            int64 channel_id_long);

  void on_channel_participant_cache_timeout(ChannelId channel_id);

  struct OnlineMemberCountInfo {
    int32 online_member_count = 0;
    double update_time = 0;
    bool is_update_sent = false;
  };
  FlatHashMap<DialogId, OnlineMemberCountInfo, DialogIdHash> dialog_online_member_counts_;

  FlatHashMap<DialogId, vector<DialogAdministrator>, DialogIdHash> dialog_administrators_;

  // bot-administrators only
  struct ChannelParticipantInfo {
    DialogParticipant participant_;

    int32 last_access_date_ = 0;
  };
  struct ChannelParticipants {
    FlatHashMap<DialogId, ChannelParticipantInfo, DialogIdHash> participants_;
  };
  FlatHashMap<ChannelId, ChannelParticipants, ChannelIdHash> channel_participants_;

  FlatHashMap<ChannelId, vector<Promise<Unit>>, ChannelIdHash> join_channel_queries_;

  MultiTimeout update_dialog_online_member_count_timeout_{"UpdateDialogOnlineMemberCountTimeout"};
  MultiTimeout channel_participant_cache_timeout_{"ChannelParticipantCacheTimeout"};

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
