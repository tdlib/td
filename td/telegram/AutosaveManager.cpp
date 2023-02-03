//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AutosaveManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"

namespace td {

class GetAutosaveSettingsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_autoSaveSettings>> promise_;

 public:
  explicit GetAutosaveSettingsQuery(Promise<telegram_api::object_ptr<telegram_api::account_autoSaveSettings>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_getAutoSaveSettings(), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getAutoSaveSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAutoSaveSettingsQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SaveAutoSaveSettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveAutoSaveSettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(bool users, bool chats, bool broadcasts, DialogId dialog_id,
            telegram_api::object_ptr<telegram_api::autoSaveSettings> settings) {
    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> input_peer;
    if (users) {
      flags |= telegram_api::account_saveAutoSaveSettings::USERS_MASK;
    } else if (chats) {
      flags |= telegram_api::account_saveAutoSaveSettings::CHATS_MASK;
    } else if (broadcasts) {
      flags |= telegram_api::account_saveAutoSaveSettings::BROADCASTS_MASK;
    } else {
      input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
      if (input_peer == nullptr) {
        if (dialog_id.get_type() == DialogType::SecretChat) {
          return on_error(Status::Error(400, "Can't set autosave settings for secret chats"));
        }
        return on_error(Status::Error(400, "Can't access the chat"));
      }
    }
    send_query(G()->net_query_creator().create(
        telegram_api::account_saveAutoSaveSettings(flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                                   std::move(input_peer), std::move(settings)),
        {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_saveAutoSaveSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
    td_->autosave_manager_->reload_autosave_settings(Auto());
  }
};

class DeleteAutoSaveExceptionsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteAutoSaveExceptionsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::account_deleteAutoSaveExceptions(), {{"me"}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_deleteAutoSaveExceptions>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
    td_->autosave_manager_->reload_autosave_settings(Auto());
  }
};

AutosaveManager::AutosaveManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void AutosaveManager::tear_down() {
  parent_.reset();
}

AutosaveManager::DialogAutosaveSettings::DialogAutosaveSettings(const telegram_api::autoSaveSettings *settings) {
  CHECK(settings != nullptr);
  are_inited_ = true;
  autosave_photos_ = settings->photos_;
  autosave_videos_ = settings->videos_;
  max_video_file_size_ = settings->video_max_size_;
}

AutosaveManager::DialogAutosaveSettings::DialogAutosaveSettings(const td_api::scopeAutosaveSettings *settings) {
  if (settings == nullptr) {
    return;
  }
  are_inited_ = true;
  autosave_photos_ = settings->autosave_photos_;
  autosave_videos_ = settings->autosave_videos_;
  max_video_file_size_ = settings->max_video_file_size_;
}

telegram_api::object_ptr<telegram_api::autoSaveSettings>
AutosaveManager::DialogAutosaveSettings::get_input_auto_save_settings() const {
  int32 flags = 0;
  if (autosave_photos_) {
    flags |= telegram_api::autoSaveSettings::PHOTOS_MASK;
  }
  if (autosave_videos_) {
    flags |= telegram_api::autoSaveSettings::VIDEOS_MASK;
  }
  if (are_inited_) {
    flags |= telegram_api::autoSaveSettings::VIDEO_MAX_SIZE_MASK;
  }
  return telegram_api::make_object<telegram_api::autoSaveSettings>(flags, false /*ignored*/, false /*ignored*/,
                                                                   max_video_file_size_);
}

td_api::object_ptr<td_api::scopeAutosaveSettings>
AutosaveManager::DialogAutosaveSettings::get_scope_autosave_settings_object() const {
  return td_api::make_object<td_api::scopeAutosaveSettings>(autosave_photos_, autosave_videos_, max_video_file_size_);
}

td_api::object_ptr<td_api::autosaveSettingsException>
AutosaveManager::DialogAutosaveSettings::get_autosave_settings_exception_object(DialogId dialog_id) const {
  return td_api::make_object<td_api::autosaveSettingsException>(dialog_id.get(), get_scope_autosave_settings_object());
}

bool AutosaveManager::DialogAutosaveSettings::operator==(const DialogAutosaveSettings &other) const {
  return are_inited_ == other.are_inited_ && autosave_photos_ == other.autosave_photos_ &&
         autosave_videos_ == other.autosave_videos_ && max_video_file_size_ == other.max_video_file_size_;
}

td_api::object_ptr<td_api::autosaveSettings> AutosaveManager::AutosaveSettings::get_autosave_settings_object() const {
  CHECK(are_inited_);
  auto exceptions = transform(exceptions_, [](const auto &exception) {
    return exception.second.get_autosave_settings_exception_object(exception.first);
  });
  return td_api::make_object<td_api::autosaveSettings>(
      user_settings_.get_scope_autosave_settings_object(), chat_settings_.get_scope_autosave_settings_object(),
      broadcast_settings_.get_scope_autosave_settings_object(), std::move(exceptions));
}

void AutosaveManager::get_autosave_settings(Promise<td_api::object_ptr<td_api::autosaveSettings>> &&promise) {
  if (settings_.are_inited_) {
    return promise.set_value(settings_.get_autosave_settings_object());
  }

  reload_autosave_settings(std::move(promise));
}

void AutosaveManager::reload_autosave_settings(Promise<td_api::object_ptr<td_api::autosaveSettings>> &&promise) {
  load_settings_queries_.push_back(std::move(promise));
  if (load_settings_queries_.size() != 1) {
    return;
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_autoSaveSettings>> r_settings) {
        send_closure(actor_id, &AutosaveManager::on_get_autosave_settings, std::move(r_settings));
      });
  td_->create_handler<GetAutosaveSettingsQuery>(std::move(query_promise))->send();
}

void AutosaveManager::on_get_autosave_settings(
    Result<telegram_api::object_ptr<telegram_api::account_autoSaveSettings>> r_settings) {
  CHECK(!settings_.are_inited_);
  if (G()->close_flag() && r_settings.is_ok()) {
    r_settings = Global::request_aborted_error();
  }
  if (r_settings.is_error()) {
    return fail_promises(load_settings_queries_, r_settings.move_as_error());
  }

  auto settings = r_settings.move_as_ok();
  settings_.are_inited_ = true;
  td_->contacts_manager_->on_get_users(std::move(settings->users_), "on_get_autosave_settings");
  td_->contacts_manager_->on_get_chats(std::move(settings->chats_), "on_get_autosave_settings");
  settings_.user_settings_ = DialogAutosaveSettings(settings->users_settings_.get());
  settings_.chat_settings_ = DialogAutosaveSettings(settings->chats_settings_.get());
  settings_.broadcast_settings_ = DialogAutosaveSettings(settings->broadcasts_settings_.get());
  settings_.exceptions_.clear();
  for (auto &exception : settings->exceptions_) {
    DialogId dialog_id(exception->peer_);
    if (!dialog_id.is_valid()) {
      continue;
    }
    td_->messages_manager_->force_create_dialog(dialog_id, "on_get_autosave_settings");
    settings_.exceptions_[dialog_id] = DialogAutosaveSettings(exception->settings_.get());
  }

  auto promises = std::move(load_settings_queries_);
  for (auto &promise : promises) {
    promise.set_value(settings_.get_autosave_settings_object());
  }
}

void AutosaveManager::set_autosave_settings(td_api::object_ptr<td_api::AutosaveSettingsScope> &&scope,
                                            td_api::object_ptr<td_api::scopeAutosaveSettings> &&settings,
                                            Promise<Unit> &&promise) {
  if (scope == nullptr) {
    return promise.set_error(Status::Error(400, "Scope must be non-empty"));
  }
  auto new_settings = DialogAutosaveSettings(settings.get());
  DialogAutosaveSettings *old_settings = nullptr;
  bool users = false;
  bool chats = false;
  bool broadcasts = false;
  DialogId dialog_id;
  switch (scope->get_id()) {
    case td_api::autosaveSettingsScopePrivateChats::ID:
      users = true;
      old_settings = &settings_.user_settings_;
      break;
    case td_api::autosaveSettingsScopeGroupChats::ID:
      chats = true;
      old_settings = &settings_.chat_settings_;
      break;
    case td_api::autosaveSettingsScopeChannelChats::ID:
      broadcasts = true;
      old_settings = &settings_.broadcast_settings_;
      break;
    case td_api::autosaveSettingsScopeChat::ID:
      dialog_id = DialogId(static_cast<const td_api::autosaveSettingsScopeChat *>(scope.get())->chat_id_);
      if (!td_->messages_manager_->have_dialog_force(dialog_id, "set_autosave_settings")) {
        return promise.set_error(Status::Error(400, "Chat not found"));
      }
      old_settings = &settings_.exceptions_[dialog_id];
      break;
    default:
      UNREACHABLE();
  }
  if (!dialog_id.is_valid()) {
    new_settings.are_inited_ = true;
  }
  if (*old_settings == new_settings) {
    return promise.set_value(Unit());
  }
  if (settings_.are_inited_) {
    if (new_settings.are_inited_) {
      *old_settings = new_settings;
    } else {
      CHECK(dialog_id.is_valid());
      settings_.exceptions_.erase(dialog_id);
    }
  }
  td_->create_handler<SaveAutoSaveSettingsQuery>(std::move(promise))
      ->send(users, chats, broadcasts, dialog_id, new_settings.get_input_auto_save_settings());
}

void AutosaveManager::clear_autosave_settings_excpetions(Promise<Unit> &&promise) {
  settings_.exceptions_.clear();
  td_->create_handler<DeleteAutoSaveExceptionsQuery>(std::move(promise))->send();
}

}  // namespace td
