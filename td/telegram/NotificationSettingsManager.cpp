//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationSettingsManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

class GetDialogNotifySettingsQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_notify_peer = td_->messages_manager_->get_input_notify_peer(dialog_id);
    CHECK(input_notify_peer != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::account_getNotifySettings(std::move(input_notify_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getNotifySettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    td_->messages_manager_->on_update_dialog_notify_settings(dialog_id_, std::move(ptr),
                                                             "GetDialogNotifySettingsQuery");
    td_->notification_settings_manager_->on_get_dialog_notification_settings_query_finished(dialog_id_, Status::OK());
  }

  void on_error(Status status) final {
    td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetDialogNotifySettingsQuery");
    td_->notification_settings_manager_->on_get_dialog_notification_settings_query_finished(dialog_id_,
                                                                                            std::move(status));
  }
};

class GetNotifySettingsExceptionsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetNotifySettingsExceptionsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(NotificationSettingsScope scope, bool filter_scope, bool compare_sound) {
    int32 flags = 0;
    tl_object_ptr<telegram_api::InputNotifyPeer> input_notify_peer;
    if (filter_scope) {
      flags |= telegram_api::account_getNotifyExceptions::PEER_MASK;
      input_notify_peer = get_input_notify_peer(scope);
    }
    if (compare_sound) {
      flags |= telegram_api::account_getNotifyExceptions::COMPARE_SOUND_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_getNotifyExceptions(flags, false /* ignored */, std::move(input_notify_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getNotifyExceptions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto updates_ptr = result_ptr.move_as_ok();
    auto dialog_ids = UpdatesManager::get_update_notify_settings_dialog_ids(updates_ptr.get());
    vector<tl_object_ptr<telegram_api::User>> users;
    vector<tl_object_ptr<telegram_api::Chat>> chats;
    switch (updates_ptr->get_id()) {
      case telegram_api::updatesCombined::ID: {
        auto updates = static_cast<telegram_api::updatesCombined *>(updates_ptr.get());
        users = std::move(updates->users_);
        chats = std::move(updates->chats_);
        reset_to_empty(updates->users_);
        reset_to_empty(updates->chats_);
        break;
      }
      case telegram_api::updates::ID: {
        auto updates = static_cast<telegram_api::updates *>(updates_ptr.get());
        users = std::move(updates->users_);
        chats = std::move(updates->chats_);
        reset_to_empty(updates->users_);
        reset_to_empty(updates->chats_);
        break;
      }
    }
    td_->contacts_manager_->on_get_users(std::move(users), "GetNotifySettingsExceptionsQuery");
    td_->contacts_manager_->on_get_chats(std::move(chats), "GetNotifySettingsExceptionsQuery");
    for (auto &dialog_id : dialog_ids) {
      td_->messages_manager_->force_create_dialog(dialog_id, "GetNotifySettingsExceptionsQuery");
    }
    td_->updates_manager_->on_get_updates(std::move(updates_ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetScopeNotifySettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  NotificationSettingsScope scope_;

 public:
  explicit GetScopeNotifySettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(NotificationSettingsScope scope) {
    scope_ = scope;
    auto input_notify_peer = get_input_notify_peer(scope);
    CHECK(input_notify_peer != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::account_getNotifySettings(std::move(input_notify_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getNotifySettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    td_->messages_manager_->on_update_scope_notify_settings(scope_, std::move(ptr));

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class UpdateDialogNotifySettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit UpdateDialogNotifySettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const DialogNotificationSettings &new_settings) {
    dialog_id_ = dialog_id;

    auto input_notify_peer = td_->messages_manager_->get_input_notify_peer(dialog_id);
    if (input_notify_peer == nullptr) {
      return on_error(Status::Error(500, "Can't update chat notification settings"));
    }

    int32 flags = 0;
    if (!new_settings.use_default_mute_until) {
      flags |= telegram_api::inputPeerNotifySettings::MUTE_UNTIL_MASK;
    }
    if (!new_settings.use_default_sound) {
      flags |= telegram_api::inputPeerNotifySettings::SOUND_MASK;
    }
    if (!new_settings.use_default_show_preview) {
      flags |= telegram_api::inputPeerNotifySettings::SHOW_PREVIEWS_MASK;
    }
    if (new_settings.silent_send_message) {
      flags |= telegram_api::inputPeerNotifySettings::SILENT_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::account_updateNotifySettings(
        std::move(input_notify_peer),
        make_tl_object<telegram_api::inputPeerNotifySettings>(
            flags, new_settings.show_preview, new_settings.silent_send_message, new_settings.mute_until,
            make_tl_object<telegram_api::notificationSoundDefault>()))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateNotifySettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      return on_error(Status::Error(400, "Receive false as result"));
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (!td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "UpdateDialogNotifySettingsQuery")) {
      LOG(INFO) << "Receive error for set chat notification settings: " << status;
    }

    if (!td_->auth_manager_->is_bot() && td_->messages_manager_->get_input_notify_peer(dialog_id_) != nullptr) {
      // trying to repair notification settings for this dialog
      td_->notification_settings_manager_->send_get_dialog_notification_settings_query(dialog_id_, Promise<>());
    }

    promise_.set_error(std::move(status));
  }
};

class UpdateScopeNotifySettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  NotificationSettingsScope scope_;

 public:
  explicit UpdateScopeNotifySettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(NotificationSettingsScope scope, const ScopeNotificationSettings &new_settings) {
    auto input_notify_peer = get_input_notify_peer(scope);
    CHECK(input_notify_peer != nullptr);
    int32 flags = telegram_api::inputPeerNotifySettings::MUTE_UNTIL_MASK |
                  telegram_api::inputPeerNotifySettings::SOUND_MASK |
                  telegram_api::inputPeerNotifySettings::SHOW_PREVIEWS_MASK;
    send_query(G()->net_query_creator().create(telegram_api::account_updateNotifySettings(
        std::move(input_notify_peer), make_tl_object<telegram_api::inputPeerNotifySettings>(
                                          flags, new_settings.show_preview, false, new_settings.mute_until,
                                          make_tl_object<telegram_api::notificationSoundDefault>()))));
    scope_ = scope;
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_updateNotifySettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      return on_error(Status::Error(400, "Receive false as result"));
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for set notification settings: " << status;

    if (!td_->auth_manager_->is_bot()) {
      // trying to repair notification settings for this scope
      td_->notification_settings_manager_->send_get_scope_notification_settings_query(scope_, Promise<>());
    }

    promise_.set_error(std::move(status));
  }
};

class ResetNotifySettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ResetNotifySettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_resetNotifySettings()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_resetNotifySettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      return on_error(Status::Error(400, "Receive false as result"));
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for reset notification settings: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

NotificationSettingsManager::NotificationSettingsManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
}

NotificationSettingsManager::~NotificationSettingsManager() = default;

void NotificationSettingsManager::tear_down() {
  parent_.reset();
}

void NotificationSettingsManager::send_get_dialog_notification_settings_query(DialogId dialog_id,
                                                                              Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot() || dialog_id.get_type() == DialogType::SecretChat) {
    LOG(WARNING) << "Can't get notification settings for " << dialog_id;
    return promise.set_error(Status::Error(500, "Wrong getDialogNotificationSettings query"));
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    LOG(WARNING) << "Have no access to " << dialog_id << " to get notification settings";
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }

  auto &promises = get_dialog_notification_settings_queries_[dialog_id];
  promises.push_back(std::move(promise));
  if (promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  td_->create_handler<GetDialogNotifySettingsQuery>()->send(dialog_id);
}

void NotificationSettingsManager::send_get_scope_notification_settings_query(NotificationSettingsScope scope,
                                                                             Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Can't get notification settings for " << scope;
    return promise.set_error(Status::Error(500, "Wrong getScopeNotificationSettings query"));
  }

  td_->create_handler<GetScopeNotifySettingsQuery>(std::move(promise))->send(scope);
}

void NotificationSettingsManager::on_get_dialog_notification_settings_query_finished(DialogId dialog_id,
                                                                                     Status &&status) {
  CHECK(!td_->auth_manager_->is_bot());
  auto it = get_dialog_notification_settings_queries_.find(dialog_id);
  CHECK(it != get_dialog_notification_settings_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  get_dialog_notification_settings_queries_.erase(it);

  for (auto &promise : promises) {
    if (status.is_ok()) {
      promise.set_value(Unit());
    } else {
      promise.set_error(status.clone());
    }
  }
}

void NotificationSettingsManager::update_dialog_notify_settings(DialogId dialog_id,
                                                                const DialogNotificationSettings &new_settings,
                                                                Promise<Unit> &&promise) {
  td_->create_handler<UpdateDialogNotifySettingsQuery>(std::move(promise))->send(dialog_id, new_settings);
}

void NotificationSettingsManager::update_scope_notify_settings(NotificationSettingsScope scope,
                                                               const ScopeNotificationSettings &new_settings,
                                                               Promise<Unit> &&promise) {
  td_->create_handler<UpdateScopeNotifySettingsQuery>(std::move(promise))->send(scope, new_settings);
}

void NotificationSettingsManager::reset_notify_settings(Promise<Unit> &&promise) {
  td_->create_handler<ResetNotifySettingsQuery>(std::move(promise))->send();
}

void NotificationSettingsManager::get_notify_settings_exceptions(NotificationSettingsScope scope, bool filter_scope,
                                                                 bool compare_sound, Promise<Unit> &&promise) {
  td_->create_handler<GetNotifySettingsExceptionsQuery>(std::move(promise))->send(scope, filter_scope, compare_sound);
}

}  // namespace td
