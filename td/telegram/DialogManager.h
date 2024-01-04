//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AccentColorId.h"
#include "td/telegram/AccessRights.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/EmojiStatus.h"
#include "td/telegram/InputDialogId.h"
#include "td/telegram/NotificationSettingsScope.h"
#include "td/telegram/Photo.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class Td;

class DialogManager final : public Actor {
 public:
  DialogManager(Td *td, ActorShared<> parent);

  DialogId get_my_dialog_id() const;

  InputDialogId get_input_dialog_id(DialogId dialog_id) const;

  tl_object_ptr<telegram_api::InputPeer> get_input_peer(DialogId dialog_id, AccessRights access_rights) const;

  static tl_object_ptr<telegram_api::InputPeer> get_input_peer_force(DialogId dialog_id);

  vector<tl_object_ptr<telegram_api::InputPeer>> get_input_peers(const vector<DialogId> &dialog_ids,
                                                                 AccessRights access_rights) const;

  tl_object_ptr<telegram_api::InputDialogPeer> get_input_dialog_peer(DialogId dialog_id,
                                                                     AccessRights access_rights) const;

  vector<tl_object_ptr<telegram_api::InputDialogPeer>> get_input_dialog_peers(const vector<DialogId> &dialog_ids,
                                                                              AccessRights access_rights) const;

  tl_object_ptr<telegram_api::inputEncryptedChat> get_input_encrypted_chat(DialogId dialog_id,
                                                                           AccessRights access_rights) const;

  bool have_input_peer(DialogId dialog_id, AccessRights access_rights) const;

  bool have_dialog_force(DialogId dialog_id, const char *source) const;

  void force_create_dialog(DialogId dialog_id, const char *source, bool expect_no_access = false,
                           bool force_update_dialog_pos = false);

  vector<DialogId> get_peers_dialog_ids(vector<telegram_api::object_ptr<telegram_api::Peer>> &&peers,
                                        bool expect_no_access = false);

  bool have_dialog_info(DialogId dialog_id) const;

  bool have_dialog_info_force(DialogId dialog_id, const char *source) const;

  void get_dialog_info_full(DialogId dialog_id, Promise<Unit> &&promise, const char *source);

  void reload_dialog_info_full(DialogId dialog_id, const char *source);

  int64 get_chat_id_object(DialogId dialog_id, const char *source) const;

  vector<int64> get_chat_ids_object(const vector<DialogId> &dialog_ids, const char *source) const;

  td_api::object_ptr<td_api::chats> get_chats_object(int32 total_count, const vector<DialogId> &dialog_ids,
                                                     const char *source) const;

  td_api::object_ptr<td_api::chats> get_chats_object(const std::pair<int32, vector<DialogId>> &dialog_ids,
                                                     const char *source) const;

  td_api::object_ptr<td_api::ChatType> get_chat_type_object(DialogId dialog_id) const;

  NotificationSettingsScope get_dialog_notification_setting_scope(DialogId dialog_id) const;

  bool is_anonymous_administrator(DialogId dialog_id, string *author_signature) const;

  bool is_group_dialog(DialogId dialog_id) const;

  bool is_forum_channel(DialogId dialog_id) const;

  bool is_broadcast_channel(DialogId dialog_id) const;

  bool on_get_dialog_error(DialogId dialog_id, const Status &status, const char *source);

  string get_dialog_title(DialogId dialog_id) const;

  const DialogPhoto *get_dialog_photo(DialogId dialog_id) const;

  int32 get_dialog_accent_color_id_object(DialogId dialog_id) const;

  CustomEmojiId get_dialog_background_custom_emoji_id(DialogId dialog_id) const;

  int32 get_dialog_profile_accent_color_id_object(DialogId dialog_id) const;

  CustomEmojiId get_dialog_profile_background_custom_emoji_id(DialogId dialog_id) const;

  RestrictedRights get_dialog_default_permissions(DialogId dialog_id) const;

  td_api::object_ptr<td_api::emojiStatus> get_dialog_emoji_status_object(DialogId dialog_id) const;

  bool get_dialog_has_protected_content(DialogId dialog_id) const;

  bool is_dialog_action_unneeded(DialogId dialog_id) const;

  void set_dialog_accent_color(DialogId dialog_id, AccentColorId accent_color_id,
                               CustomEmojiId background_custom_emoji_id, Promise<Unit> &&promise);

  void set_dialog_profile_accent_color(DialogId dialog_id, AccentColorId profile_accent_color_id,
                                       CustomEmojiId profile_background_custom_emoji_id, Promise<Unit> &&promise);

  void set_dialog_emoji_status(DialogId dialog_id, const EmojiStatus &emoji_status, Promise<Unit> &&promise);

  void set_dialog_description(DialogId dialog_id, const string &description, Promise<Unit> &&promise);

  Status can_pin_messages(DialogId dialog_id) const;

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
