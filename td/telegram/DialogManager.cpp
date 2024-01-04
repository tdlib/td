//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChannelType.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"

namespace td {

DialogManager::DialogManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void DialogManager::tear_down() {
  parent_.reset();
}

DialogId DialogManager::get_my_dialog_id() const {
  return DialogId(td_->contacts_manager_->get_my_id());
}

InputDialogId DialogManager::get_input_dialog_id(DialogId dialog_id) const {
  auto input_peer = get_input_peer(dialog_id, AccessRights::Read);
  if (input_peer == nullptr || input_peer->get_id() == telegram_api::inputPeerSelf::ID ||
      input_peer->get_id() == telegram_api::inputPeerEmpty::ID) {
    return InputDialogId(dialog_id);
  } else {
    return InputDialogId(input_peer);
  }
}

tl_object_ptr<telegram_api::InputPeer> DialogManager::get_input_peer(DialogId dialog_id,
                                                                     AccessRights access_rights) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_input_peer_user(dialog_id.get_user_id(), access_rights);
    case DialogType::Chat:
      return td_->contacts_manager_->get_input_peer_chat(dialog_id.get_chat_id(), access_rights);
    case DialogType::Channel:
      return td_->contacts_manager_->get_input_peer_channel(dialog_id.get_channel_id(), access_rights);
    case DialogType::SecretChat:
      return nullptr;
    case DialogType::None:
      return make_tl_object<telegram_api::inputPeerEmpty>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<telegram_api::InputPeer> DialogManager::get_input_peer_force(DialogId dialog_id) {
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      UserId user_id = dialog_id.get_user_id();
      return make_tl_object<telegram_api::inputPeerUser>(user_id.get(), 0);
    }
    case DialogType::Chat: {
      ChatId chat_id = dialog_id.get_chat_id();
      return make_tl_object<telegram_api::inputPeerChat>(chat_id.get());
    }
    case DialogType::Channel: {
      ChannelId channel_id = dialog_id.get_channel_id();
      return make_tl_object<telegram_api::inputPeerChannel>(channel_id.get(), 0);
    }
    case DialogType::SecretChat:
    case DialogType::None:
      return make_tl_object<telegram_api::inputPeerEmpty>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<tl_object_ptr<telegram_api::InputPeer>> DialogManager::get_input_peers(const vector<DialogId> &dialog_ids,
                                                                              AccessRights access_rights) const {
  vector<tl_object_ptr<telegram_api::InputPeer>> input_peers;
  input_peers.reserve(dialog_ids.size());
  for (auto &dialog_id : dialog_ids) {
    auto input_peer = get_input_peer(dialog_id, access_rights);
    if (input_peer == nullptr) {
      LOG(ERROR) << "Have no access to " << dialog_id;
      continue;
    }
    input_peers.push_back(std::move(input_peer));
  }
  return input_peers;
}

tl_object_ptr<telegram_api::InputDialogPeer> DialogManager::get_input_dialog_peer(DialogId dialog_id,
                                                                                  AccessRights access_rights) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
    case DialogType::Chat:
    case DialogType::Channel:
    case DialogType::None:
      return make_tl_object<telegram_api::inputDialogPeer>(get_input_peer(dialog_id, access_rights));
    case DialogType::SecretChat:
      return nullptr;
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<tl_object_ptr<telegram_api::InputDialogPeer>> DialogManager::get_input_dialog_peers(
    const vector<DialogId> &dialog_ids, AccessRights access_rights) const {
  vector<tl_object_ptr<telegram_api::InputDialogPeer>> input_dialog_peers;
  input_dialog_peers.reserve(dialog_ids.size());
  for (auto &dialog_id : dialog_ids) {
    auto input_dialog_peer = get_input_dialog_peer(dialog_id, access_rights);
    if (input_dialog_peer == nullptr) {
      LOG(ERROR) << "Have no access to " << dialog_id;
      continue;
    }
    input_dialog_peers.push_back(std::move(input_dialog_peer));
  }
  return input_dialog_peers;
}

tl_object_ptr<telegram_api::inputEncryptedChat> DialogManager::get_input_encrypted_chat(
    DialogId dialog_id, AccessRights access_rights) const {
  switch (dialog_id.get_type()) {
    case DialogType::SecretChat: {
      SecretChatId secret_chat_id = dialog_id.get_secret_chat_id();
      return td_->contacts_manager_->get_input_encrypted_chat(secret_chat_id, access_rights);
    }
    case DialogType::User:
    case DialogType::Chat:
    case DialogType::Channel:
    case DialogType::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool DialogManager::have_input_peer(DialogId dialog_id, AccessRights access_rights) const {
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      UserId user_id = dialog_id.get_user_id();
      return td_->contacts_manager_->have_input_peer_user(user_id, access_rights);
    }
    case DialogType::Chat: {
      ChatId chat_id = dialog_id.get_chat_id();
      return td_->contacts_manager_->have_input_peer_chat(chat_id, access_rights);
    }
    case DialogType::Channel: {
      ChannelId channel_id = dialog_id.get_channel_id();
      return td_->contacts_manager_->have_input_peer_channel(channel_id, access_rights);
    }
    case DialogType::SecretChat: {
      SecretChatId secret_chat_id = dialog_id.get_secret_chat_id();
      return td_->contacts_manager_->have_input_encrypted_peer(secret_chat_id, access_rights);
    }
    case DialogType::None:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

bool DialogManager::have_dialog_force(DialogId dialog_id, const char *source) const {
  return td_->messages_manager_->have_dialog_force(dialog_id, source);
}

void DialogManager::force_create_dialog(DialogId dialog_id, const char *source, bool expect_no_access,
                                        bool force_update_dialog_pos) {
  td_->messages_manager_->force_create_dialog(dialog_id, source, expect_no_access, force_update_dialog_pos);
}

vector<DialogId> DialogManager::get_peers_dialog_ids(vector<telegram_api::object_ptr<telegram_api::Peer>> &&peers,
                                                     bool expect_no_access) {
  vector<DialogId> result;
  result.reserve(peers.size());
  for (auto &peer : peers) {
    DialogId dialog_id(peer);
    if (dialog_id.is_valid()) {
      force_create_dialog(dialog_id, "get_peers_dialog_ids", expect_no_access);
      result.push_back(dialog_id);
    }
  }
  return result;
}

bool DialogManager::have_dialog_info(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      UserId user_id = dialog_id.get_user_id();
      return td_->contacts_manager_->have_user(user_id);
    }
    case DialogType::Chat: {
      ChatId chat_id = dialog_id.get_chat_id();
      return td_->contacts_manager_->have_chat(chat_id);
    }
    case DialogType::Channel: {
      ChannelId channel_id = dialog_id.get_channel_id();
      return td_->contacts_manager_->have_channel(channel_id);
    }
    case DialogType::SecretChat: {
      SecretChatId secret_chat_id = dialog_id.get_secret_chat_id();
      return td_->contacts_manager_->have_secret_chat(secret_chat_id);
    }
    case DialogType::None:
    default:
      return false;
  }
}

bool DialogManager::have_dialog_info_force(DialogId dialog_id, const char *source) const {
  switch (dialog_id.get_type()) {
    case DialogType::User: {
      UserId user_id = dialog_id.get_user_id();
      return td_->contacts_manager_->have_user_force(user_id, source);
    }
    case DialogType::Chat: {
      ChatId chat_id = dialog_id.get_chat_id();
      return td_->contacts_manager_->have_chat_force(chat_id, source);
    }
    case DialogType::Channel: {
      ChannelId channel_id = dialog_id.get_channel_id();
      return td_->contacts_manager_->have_channel_force(channel_id, source);
    }
    case DialogType::SecretChat: {
      SecretChatId secret_chat_id = dialog_id.get_secret_chat_id();
      return td_->contacts_manager_->have_secret_chat_force(secret_chat_id, source);
    }
    case DialogType::None:
    default:
      return false;
  }
}

void DialogManager::get_dialog_info_full(DialogId dialog_id, Promise<Unit> &&promise, const char *source) {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      send_closure_later(td_->contacts_manager_actor_, &ContactsManager::load_user_full, dialog_id.get_user_id(), false,
                         std::move(promise), source);
      return;
    case DialogType::Chat:
      send_closure_later(td_->contacts_manager_actor_, &ContactsManager::load_chat_full, dialog_id.get_chat_id(), false,
                         std::move(promise), source);
      return;
    case DialogType::Channel:
      send_closure_later(td_->contacts_manager_actor_, &ContactsManager::load_channel_full, dialog_id.get_channel_id(),
                         false, std::move(promise), source);
      return;
    case DialogType::SecretChat:
      return promise.set_value(Unit());
    case DialogType::None:
    default:
      UNREACHABLE();
      return promise.set_error(Status::Error(500, "Wrong chat type"));
  }
}

void DialogManager::reload_dialog_info_full(DialogId dialog_id, const char *source) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Reload full info about " << dialog_id << " from " << source;
  switch (dialog_id.get_type()) {
    case DialogType::User:
      send_closure_later(td_->contacts_manager_actor_, &ContactsManager::reload_user_full, dialog_id.get_user_id(),
                         Promise<Unit>(), source);
      return;
    case DialogType::Chat:
      send_closure_later(td_->contacts_manager_actor_, &ContactsManager::reload_chat_full, dialog_id.get_chat_id(),
                         Promise<Unit>(), source);
      return;
    case DialogType::Channel:
      send_closure_later(td_->contacts_manager_actor_, &ContactsManager::reload_channel_full,
                         dialog_id.get_channel_id(), Promise<Unit>(), source);
      return;
    case DialogType::SecretChat:
      return;
    case DialogType::None:
    default:
      UNREACHABLE();
      return;
  }
}

void DialogManager::on_dialog_info_full_invalidated(DialogId dialog_id) {
  if (td_->messages_manager_->is_dialog_opened(dialog_id)) {
    reload_dialog_info_full(dialog_id, "on_dialog_info_full_invalidated");
  }
}

int64 DialogManager::get_chat_id_object(DialogId dialog_id, const char *source) const {
  return td_->messages_manager_->get_chat_id_object(dialog_id, source);
}

vector<int64> DialogManager::get_chat_ids_object(const vector<DialogId> &dialog_ids, const char *source) const {
  return transform(dialog_ids, [this, source](DialogId dialog_id) { return get_chat_id_object(dialog_id, source); });
}

td_api::object_ptr<td_api::chats> DialogManager::get_chats_object(int32 total_count, const vector<DialogId> &dialog_ids,
                                                                  const char *source) const {
  if (total_count == -1) {
    total_count = narrow_cast<int32>(dialog_ids.size());
  }
  return td_api::make_object<td_api::chats>(total_count, get_chat_ids_object(dialog_ids, source));
}

td_api::object_ptr<td_api::chats> DialogManager::get_chats_object(const std::pair<int32, vector<DialogId>> &dialog_ids,
                                                                  const char *source) const {
  return get_chats_object(dialog_ids.first, dialog_ids.second, source);
}

td_api::object_ptr<td_api::ChatType> DialogManager::get_chat_type_object(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_api::make_object<td_api::chatTypePrivate>(
          td_->contacts_manager_->get_user_id_object(dialog_id.get_user_id(), "chatTypePrivate"));
    case DialogType::Chat:
      return td_api::make_object<td_api::chatTypeBasicGroup>(
          td_->contacts_manager_->get_basic_group_id_object(dialog_id.get_chat_id(), "chatTypeBasicGroup"));
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      return td_api::make_object<td_api::chatTypeSupergroup>(
          td_->contacts_manager_->get_supergroup_id_object(channel_id, "chatTypeSupergroup"),
          !td_->contacts_manager_->is_megagroup_channel(channel_id));
    }
    case DialogType::SecretChat: {
      auto secret_chat_id = dialog_id.get_secret_chat_id();
      auto user_id = td_->contacts_manager_->get_secret_chat_user_id(secret_chat_id);
      return td_api::make_object<td_api::chatTypeSecret>(
          td_->contacts_manager_->get_secret_chat_id_object(secret_chat_id, "chatTypeSecret"),
          td_->contacts_manager_->get_user_id_object(user_id, "chatTypeSecret"));
    }
    case DialogType::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

NotificationSettingsScope DialogManager::get_dialog_notification_setting_scope(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
    case DialogType::SecretChat:
      return NotificationSettingsScope::Private;
    case DialogType::Chat:
      return NotificationSettingsScope::Group;
    case DialogType::Channel:
      return is_broadcast_channel(dialog_id) ? NotificationSettingsScope::Channel : NotificationSettingsScope::Group;
    case DialogType::None:
    default:
      UNREACHABLE();
      return NotificationSettingsScope::Private;
  }
}

bool DialogManager::is_anonymous_administrator(DialogId dialog_id, string *author_signature) const {
  CHECK(dialog_id.is_valid());

  if (is_broadcast_channel(dialog_id)) {
    return true;
  }

  if (td_->auth_manager_->is_bot()) {
    return false;
  }

  if (dialog_id.get_type() != DialogType::Channel) {
    return false;
  }

  auto status = td_->contacts_manager_->get_channel_status(dialog_id.get_channel_id());
  if (!status.is_anonymous()) {
    return false;
  }

  if (author_signature != nullptr) {
    *author_signature = status.get_rank();
  }
  return true;
}

bool DialogManager::is_group_dialog(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::Chat:
      return true;
    case DialogType::Channel:
      return td_->contacts_manager_->is_megagroup_channel(dialog_id.get_channel_id());
    default:
      return false;
  }
}

bool DialogManager::is_forum_channel(DialogId dialog_id) const {
  return dialog_id.get_type() == DialogType::Channel &&
         td_->contacts_manager_->is_forum_channel(dialog_id.get_channel_id());
}

bool DialogManager::is_broadcast_channel(DialogId dialog_id) const {
  if (dialog_id.get_type() != DialogType::Channel) {
    return false;
  }

  return td_->contacts_manager_->is_broadcast_channel(dialog_id.get_channel_id());
}

bool DialogManager::on_get_dialog_error(DialogId dialog_id, const Status &status, const char *source) {
  if (status.message() == CSlice("BOT_METHOD_INVALID")) {
    LOG(ERROR) << "Receive BOT_METHOD_INVALID from " << source;
    return true;
  }
  if (G()->is_expected_error(status)) {
    return true;
  }
  if (status.message() == CSlice("SEND_AS_PEER_INVALID")) {
    reload_dialog_info_full(dialog_id, "SEND_AS_PEER_INVALID");
    return true;
  }
  if (status.message() == CSlice("QUOTE_TEXT_INVALID") || status.message() == CSlice("REPLY_MESSAGE_ID_INVALID")) {
    return true;
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
    case DialogType::Chat:
    case DialogType::SecretChat:
      // to be implemented if necessary
      break;
    case DialogType::Channel:
      return td_->contacts_manager_->on_get_channel_error(dialog_id.get_channel_id(), status, source);
    case DialogType::None:
      // to be implemented if necessary
      break;
    default:
      UNREACHABLE();
  }
  return false;
}

string DialogManager::get_dialog_title(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_title(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_title(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_title(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_title(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return string();
  }
}

const DialogPhoto *DialogManager::get_dialog_photo(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_dialog_photo(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_dialog_photo(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_dialog_photo(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_dialog_photo(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

int32 DialogManager::get_dialog_accent_color_id_object(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_accent_color_id_object(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_accent_color_id_object(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_accent_color_id_object(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_accent_color_id_object(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return 0;
  }
}

CustomEmojiId DialogManager::get_dialog_background_custom_emoji_id(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_background_custom_emoji_id(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_background_custom_emoji_id(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_background_custom_emoji_id(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_background_custom_emoji_id(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return CustomEmojiId();
  }
}

int32 DialogManager::get_dialog_profile_accent_color_id_object(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_profile_accent_color_id_object(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_profile_accent_color_id_object(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_profile_accent_color_id_object(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_profile_accent_color_id_object(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return 0;
  }
}

CustomEmojiId DialogManager::get_dialog_profile_background_custom_emoji_id(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_profile_background_custom_emoji_id(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_profile_background_custom_emoji_id(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_profile_background_custom_emoji_id(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_profile_background_custom_emoji_id(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return CustomEmojiId();
  }
}

RestrictedRights DialogManager::get_dialog_default_permissions(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_default_permissions(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_default_permissions(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_default_permissions(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_default_permissions(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return RestrictedRights(false, false, false, false, false, false, false, false, false, false, false, false, false,
                              false, false, false, false, ChannelType::Unknown);
  }
}

td_api::object_ptr<td_api::emojiStatus> DialogManager::get_dialog_emoji_status_object(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return td_->contacts_manager_->get_user_emoji_status_object(dialog_id.get_user_id());
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_emoji_status_object(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_emoji_status_object(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return td_->contacts_manager_->get_secret_chat_emoji_status_object(dialog_id.get_secret_chat_id());
    case DialogType::None:
    default:
      UNREACHABLE();
      return 0;
  }
}

bool DialogManager::get_dialog_has_protected_content(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return false;
    case DialogType::Chat:
      return td_->contacts_manager_->get_chat_has_protected_content(dialog_id.get_chat_id());
    case DialogType::Channel:
      return td_->contacts_manager_->get_channel_has_protected_content(dialog_id.get_channel_id());
    case DialogType::SecretChat:
      return false;
    case DialogType::None:
    default:
      UNREACHABLE();
      return true;
  }
}

bool DialogManager::is_dialog_action_unneeded(DialogId dialog_id) const {
  if (is_anonymous_administrator(dialog_id, nullptr)) {
    return true;
  }

  auto dialog_type = dialog_id.get_type();
  if (dialog_type == DialogType::User || dialog_type == DialogType::SecretChat) {
    UserId user_id = dialog_type == DialogType::User
                         ? dialog_id.get_user_id()
                         : td_->contacts_manager_->get_secret_chat_user_id(dialog_id.get_secret_chat_id());
    if (td_->contacts_manager_->is_user_deleted(user_id)) {
      return true;
    }
    if (td_->contacts_manager_->is_user_bot(user_id) && !td_->contacts_manager_->is_user_support(user_id)) {
      return true;
    }
    if (user_id == td_->contacts_manager_->get_my_id()) {
      return true;
    }

    if (!td_->auth_manager_->is_bot()) {
      if (td_->contacts_manager_->is_user_status_exact(user_id)) {
        if (!td_->contacts_manager_->is_user_online(user_id, 30)) {
          return true;
        }
      } else {
        // return true;
      }
    }
  }
  return false;
}

void DialogManager::set_dialog_accent_color(DialogId dialog_id, AccentColorId accent_color_id,
                                            CustomEmojiId background_custom_emoji_id, Promise<Unit> &&promise) {
  if (!have_dialog_force(dialog_id, "set_dialog_accent_color")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      if (dialog_id == get_my_dialog_id()) {
        return td_->contacts_manager_->set_accent_color(accent_color_id, background_custom_emoji_id,
                                                        std::move(promise));
      }
      break;
    case DialogType::Chat:
      break;
    case DialogType::Channel:
      return td_->contacts_manager_->set_channel_accent_color(dialog_id.get_channel_id(), accent_color_id,
                                                              background_custom_emoji_id, std::move(promise));
    case DialogType::SecretChat:
      break;
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  promise.set_error(Status::Error(400, "Can't change accent color in the chat"));
}

void DialogManager::set_dialog_profile_accent_color(DialogId dialog_id, AccentColorId profile_accent_color_id,
                                                    CustomEmojiId profile_background_custom_emoji_id,
                                                    Promise<Unit> &&promise) {
  if (!have_dialog_force(dialog_id, "set_dialog_profile_accent_color")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      if (dialog_id == get_my_dialog_id()) {
        return td_->contacts_manager_->set_profile_accent_color(profile_accent_color_id,
                                                                profile_background_custom_emoji_id, std::move(promise));
      }
      break;
    case DialogType::Chat:
      break;
    case DialogType::Channel:
      return td_->contacts_manager_->set_channel_profile_accent_color(
          dialog_id.get_channel_id(), profile_accent_color_id, profile_background_custom_emoji_id, std::move(promise));
    case DialogType::SecretChat:
      break;
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  promise.set_error(Status::Error(400, "Can't change profile accent color in the chat"));
}

void DialogManager::set_dialog_emoji_status(DialogId dialog_id, const EmojiStatus &emoji_status,
                                            Promise<Unit> &&promise) {
  if (!have_dialog_force(dialog_id, "set_dialog_emoji_status")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      if (dialog_id == get_my_dialog_id()) {
        return td_->contacts_manager_->set_emoji_status(emoji_status, std::move(promise));
      }
      break;
    case DialogType::Chat:
      break;
    case DialogType::Channel:
      return td_->contacts_manager_->set_channel_emoji_status(dialog_id.get_channel_id(), emoji_status,
                                                              std::move(promise));
    case DialogType::SecretChat:
      break;
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  promise.set_error(Status::Error(400, "Can't change emoji status in the chat"));
}

void DialogManager::set_dialog_description(DialogId dialog_id, const string &description, Promise<Unit> &&promise) {
  if (!have_dialog_force(dialog_id, "set_dialog_description")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return promise.set_error(Status::Error(400, "Can't change private chat description"));
    case DialogType::Chat:
      return td_->contacts_manager_->set_chat_description(dialog_id.get_chat_id(), description, std::move(promise));
    case DialogType::Channel:
      return td_->contacts_manager_->set_channel_description(dialog_id.get_channel_id(), description,
                                                             std::move(promise));
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Can't change secret chat description"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }
}

Status DialogManager::can_pin_messages(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      break;
    case DialogType::Chat: {
      auto chat_id = dialog_id.get_chat_id();
      auto status = td_->contacts_manager_->get_chat_permissions(chat_id);
      if (!status.can_pin_messages() ||
          (td_->auth_manager_->is_bot() && !td_->contacts_manager_->is_appointed_chat_administrator(chat_id))) {
        return Status::Error(400, "Not enough rights to manage pinned messages in the chat");
      }
      break;
    }
    case DialogType::Channel: {
      auto status = td_->contacts_manager_->get_channel_permissions(dialog_id.get_channel_id());
      bool can_pin = is_broadcast_channel(dialog_id) ? status.can_edit_messages() : status.can_pin_messages();
      if (!can_pin) {
        return Status::Error(400, "Not enough rights to manage pinned messages in the chat");
      }
      break;
    }
    case DialogType::SecretChat:
      return Status::Error(400, "Secret chats can't have pinned messages");
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  if (!have_input_peer(dialog_id, AccessRights::Write)) {
    return Status::Error(400, "Not enough rights");
  }

  return Status::OK();
}

bool DialogManager::is_dialog_removed_from_dialog_list(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      break;
    case DialogType::Chat:
      return !td_->contacts_manager_->get_chat_is_active(dialog_id.get_chat_id());
    case DialogType::Channel:
      return !td_->contacts_manager_->get_channel_status(dialog_id.get_channel_id()).is_member();
    case DialogType::SecretChat:
      break;
    case DialogType::None:
    default:
      UNREACHABLE();
      break;
  }
  return false;
}

}  // namespace td
