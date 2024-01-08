//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogInviteLinkManager.h"

#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/ThemeManager.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

class CheckChatInviteQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  string invite_link_;

 public:
  explicit CheckChatInviteQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &invite_link) {
    invite_link_ = invite_link;
    send_query(G()->net_query_creator().create(
        telegram_api::messages_checkChatInvite(LinkManager::get_dialog_invite_link_hash(invite_link_))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_checkChatInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CheckChatInviteQuery: " << to_string(ptr);

    td_->dialog_invite_link_manager_->on_get_dialog_invite_link_info(invite_link_, std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ImportChatInviteQuery final : public Td::ResultHandler {
  Promise<DialogId> promise_;

  string invite_link_;

 public:
  explicit ImportChatInviteQuery(Promise<DialogId> &&promise) : promise_(std::move(promise)) {
  }

  void send(const string &invite_link) {
    invite_link_ = invite_link;
    send_query(G()->net_query_creator().create(
        telegram_api::messages_importChatInvite(LinkManager::get_dialog_invite_link_hash(invite_link_))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_importChatInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ImportChatInviteQuery: " << to_string(ptr);

    auto dialog_ids = UpdatesManager::get_chat_dialog_ids(ptr.get());
    if (dialog_ids.size() != 1u) {
      LOG(ERROR) << "Receive wrong result for ImportChatInviteQuery: " << to_string(ptr);
      return on_error(Status::Error(500, "Internal Server Error: failed to join chat via invite link"));
    }
    auto dialog_id = dialog_ids[0];

    td_->dialog_invite_link_manager_->invalidate_invite_link_info(invite_link_);
    td_->updates_manager_->on_get_updates(
        std::move(ptr), PromiseCreator::lambda([promise = std::move(promise_), dialog_id](Unit) mutable {
          promise.set_value(std::move(dialog_id));
        }));
  }

  void on_error(Status status) final {
    td_->dialog_invite_link_manager_->invalidate_invite_link_info(invite_link_);
    promise_.set_error(std::move(status));
  }
};

DialogInviteLinkManager::DialogInviteLinkManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void DialogInviteLinkManager::tear_down() {
  parent_.reset();
}

DialogInviteLinkManager::~DialogInviteLinkManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), invite_link_infos_);
}

void DialogInviteLinkManager::check_dialog_invite_link(const string &invite_link, bool force, Promise<Unit> &&promise) {
  auto it = invite_link_infos_.find(invite_link);
  if (it != invite_link_infos_.end()) {
    auto dialog_id = it->second->dialog_id;
    if (!force && dialog_id.get_type() == DialogType::Chat &&
        !td_->contacts_manager_->get_chat_is_active(dialog_id.get_chat_id())) {
      invite_link_infos_.erase(it);
    } else {
      return promise.set_value(Unit());
    }
  }

  if (!DialogInviteLink::is_valid_invite_link(invite_link)) {
    return promise.set_error(Status::Error(400, "Wrong invite link"));
  }

  CHECK(!invite_link.empty());
  td_->create_handler<CheckChatInviteQuery>(std::move(promise))->send(invite_link);
}

void DialogInviteLinkManager::import_dialog_invite_link(const string &invite_link, Promise<DialogId> &&promise) {
  if (!DialogInviteLink::is_valid_invite_link(invite_link)) {
    return promise.set_error(Status::Error(400, "Wrong invite link"));
  }

  td_->create_handler<ImportChatInviteQuery>(std::move(promise))->send(invite_link);
}

void DialogInviteLinkManager::on_get_dialog_invite_link_info(
    const string &invite_link, telegram_api::object_ptr<telegram_api::ChatInvite> &&chat_invite_ptr,
    Promise<Unit> &&promise) {
  CHECK(chat_invite_ptr != nullptr);
  switch (chat_invite_ptr->get_id()) {
    case telegram_api::chatInviteAlready::ID:
    case telegram_api::chatInvitePeek::ID: {
      telegram_api::object_ptr<telegram_api::Chat> chat = nullptr;
      int32 accessible_before_date = 0;
      if (chat_invite_ptr->get_id() == telegram_api::chatInviteAlready::ID) {
        auto chat_invite_already = telegram_api::move_object_as<telegram_api::chatInviteAlready>(chat_invite_ptr);
        chat = std::move(chat_invite_already->chat_);
      } else {
        auto chat_invite_peek = telegram_api::move_object_as<telegram_api::chatInvitePeek>(chat_invite_ptr);
        chat = std::move(chat_invite_peek->chat_);
        accessible_before_date = chat_invite_peek->expires_;
      }
      auto chat_id = ContactsManager::get_chat_id(chat);
      if (chat_id != ChatId() && !chat_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << chat_id;
        chat_id = ChatId();
      }
      auto channel_id = ContactsManager::get_channel_id(chat);
      if (channel_id != ChannelId() && !channel_id.is_valid()) {
        LOG(ERROR) << "Receive invalid " << channel_id;
        channel_id = ChannelId();
      }
      if (accessible_before_date != 0 && (!channel_id.is_valid() || accessible_before_date < 0)) {
        LOG(ERROR) << "Receive expires = " << accessible_before_date << " for invite link " << invite_link << " to "
                   << to_string(chat);
        accessible_before_date = 0;
      }
      td_->contacts_manager_->on_get_chat(std::move(chat), "chatInviteAlready");

      CHECK(chat_id == ChatId() || channel_id == ChannelId());

      // the access is already expired, reget the info
      if (accessible_before_date != 0 && accessible_before_date <= G()->unix_time() + 1) {
        td_->create_handler<CheckChatInviteQuery>(std::move(promise))->send(invite_link);
        return;
      }

      DialogId dialog_id = chat_id.is_valid() ? DialogId(chat_id) : DialogId(channel_id);
      auto &invite_link_info = invite_link_infos_[invite_link];
      if (invite_link_info == nullptr) {
        invite_link_info = make_unique<InviteLinkInfo>();
      }
      invite_link_info->dialog_id = dialog_id;
      if (accessible_before_date != 0 && dialog_id.is_valid()) {
        td_->contacts_manager_->add_dialog_access_by_invite_link(dialog_id, invite_link, accessible_before_date);
      }
      break;
    }
    case telegram_api::chatInvite::ID: {
      auto chat_invite = telegram_api::move_object_as<telegram_api::chatInvite>(chat_invite_ptr);
      vector<UserId> participant_user_ids;
      for (auto &user : chat_invite->participants_) {
        auto user_id = ContactsManager::get_user_id(user);
        if (!user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << user_id;
          continue;
        }

        td_->contacts_manager_->on_get_user(std::move(user), "chatInvite");
        participant_user_ids.push_back(user_id);
      }

      auto &invite_link_info = invite_link_infos_[invite_link];
      if (invite_link_info == nullptr) {
        invite_link_info = make_unique<InviteLinkInfo>();
      }
      invite_link_info->dialog_id = DialogId();
      invite_link_info->title = chat_invite->title_;
      invite_link_info->photo = get_photo(td_, std::move(chat_invite->photo_), DialogId());
      invite_link_info->accent_color_id = AccentColorId(chat_invite->color_);
      invite_link_info->description = std::move(chat_invite->about_);
      invite_link_info->participant_count = chat_invite->participants_count_;
      invite_link_info->participant_user_ids = std::move(participant_user_ids);
      invite_link_info->creates_join_request = std::move(chat_invite->request_needed_);
      invite_link_info->is_chat = !chat_invite->channel_;
      invite_link_info->is_channel = chat_invite->channel_;

      bool is_broadcast = chat_invite->broadcast_;
      bool is_public = chat_invite->public_;
      bool is_megagroup = chat_invite->megagroup_;

      if (!invite_link_info->is_channel) {
        if (is_broadcast || is_public || is_megagroup) {
          LOG(ERROR) << "Receive wrong chat invite: " << to_string(chat_invite);
          is_public = is_megagroup = false;
        }
      } else {
        LOG_IF(ERROR, is_broadcast == is_megagroup) << "Receive wrong chat invite: " << to_string(chat_invite);
      }

      invite_link_info->is_public = is_public;
      invite_link_info->is_megagroup = is_megagroup;
      invite_link_info->is_verified = chat_invite->verified_;
      invite_link_info->is_scam = chat_invite->scam_;
      invite_link_info->is_fake = chat_invite->fake_;
      break;
    }
    default:
      UNREACHABLE();
  }
  promise.set_value(Unit());
}

void DialogInviteLinkManager::invalidate_invite_link_info(const string &invite_link) {
  LOG(INFO) << "Invalidate info about invite link " << invite_link;
  invite_link_infos_.erase(invite_link);
}

td_api::object_ptr<td_api::chatInviteLinkInfo> DialogInviteLinkManager::get_chat_invite_link_info_object(
    const string &invite_link) {
  auto it = invite_link_infos_.find(invite_link);
  if (it == invite_link_infos_.end()) {
    return nullptr;
  }

  auto invite_link_info = it->second.get();
  CHECK(invite_link_info != nullptr);

  DialogId dialog_id = invite_link_info->dialog_id;
  bool is_chat = false;
  bool is_megagroup = false;
  string title;
  const DialogPhoto *photo = nullptr;
  DialogPhoto invite_link_photo;
  int32 accent_color_id_object;
  string description;
  int32 participant_count = 0;
  vector<int64> member_user_ids;
  bool creates_join_request = false;
  bool is_public = false;
  bool is_member = false;
  bool is_verified = false;
  bool is_scam = false;
  bool is_fake = false;

  if (dialog_id.is_valid()) {
    switch (dialog_id.get_type()) {
      case DialogType::Chat: {
        auto chat_id = dialog_id.get_chat_id();
        is_chat = true;

        title = td_->contacts_manager_->get_chat_title(chat_id);
        photo = td_->contacts_manager_->get_chat_dialog_photo(chat_id);
        participant_count = td_->contacts_manager_->get_chat_participant_count(chat_id);
        is_member = td_->contacts_manager_->get_chat_status(chat_id).is_member();
        accent_color_id_object = td_->contacts_manager_->get_chat_accent_color_id_object(chat_id);
        break;
      }
      case DialogType::Channel: {
        auto channel_id = dialog_id.get_channel_id();
        title = td_->contacts_manager_->get_channel_title(channel_id);
        photo = td_->contacts_manager_->get_channel_dialog_photo(channel_id);
        is_public = td_->contacts_manager_->is_channel_public(channel_id);
        is_megagroup = td_->contacts_manager_->is_megagroup_channel(channel_id);
        participant_count = td_->contacts_manager_->get_channel_participant_count(channel_id);
        is_member = td_->contacts_manager_->get_channel_status(channel_id).is_member();
        is_verified = td_->contacts_manager_->get_channel_is_verified(channel_id);
        is_scam = td_->contacts_manager_->get_channel_is_scam(channel_id);
        is_fake = td_->contacts_manager_->get_channel_is_fake(channel_id);
        accent_color_id_object = td_->contacts_manager_->get_channel_accent_color_id_object(channel_id);
        break;
      }
      default:
        UNREACHABLE();
    }
    description = td_->contacts_manager_->get_dialog_about(dialog_id);
  } else {
    is_chat = invite_link_info->is_chat;
    is_megagroup = invite_link_info->is_megagroup;
    title = invite_link_info->title;
    invite_link_photo = as_fake_dialog_photo(invite_link_info->photo, dialog_id, false);
    photo = &invite_link_photo;
    accent_color_id_object = td_->theme_manager_->get_accent_color_id_object(invite_link_info->accent_color_id);
    description = invite_link_info->description;
    participant_count = invite_link_info->participant_count;
    member_user_ids = td_->contacts_manager_->get_user_ids_object(invite_link_info->participant_user_ids,
                                                                  "get_chat_invite_link_info_object");
    creates_join_request = invite_link_info->creates_join_request;
    is_public = invite_link_info->is_public;
    is_verified = invite_link_info->is_verified;
    is_scam = invite_link_info->is_scam;
    is_fake = invite_link_info->is_fake;
  }

  td_api::object_ptr<td_api::InviteLinkChatType> chat_type;
  if (is_chat) {
    chat_type = td_api::make_object<td_api::inviteLinkChatTypeBasicGroup>();
  } else if (is_megagroup) {
    chat_type = td_api::make_object<td_api::inviteLinkChatTypeSupergroup>();
  } else {
    chat_type = td_api::make_object<td_api::inviteLinkChatTypeChannel>();
  }

  if (dialog_id.is_valid()) {
    td_->dialog_manager_->force_create_dialog(dialog_id, "get_chat_invite_link_info_object");
  }
  int32 accessible_for = 0;
  if (dialog_id.is_valid() && !is_member) {
    accessible_for = td_->contacts_manager_->get_dialog_accessible_by_invite_link_before_date(dialog_id);
  }

  return td_api::make_object<td_api::chatInviteLinkInfo>(
      td_->dialog_manager_->get_chat_id_object(dialog_id, "chatInviteLinkInfo"), accessible_for, std::move(chat_type),
      title, get_chat_photo_info_object(td_->file_manager_.get(), photo), accent_color_id_object, description,
      participant_count, std::move(member_user_ids), creates_join_request, is_public, is_verified, is_scam, is_fake);
}

}  // namespace td
