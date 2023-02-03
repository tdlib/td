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

class DeleteAutosaveExceptionsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteAutosaveExceptionsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
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
  autosave_photos_ = settings->photos_;
  autosave_videos_ = settings->videos_;
  max_video_file_size_ = settings->video_max_size_;
}

td_api::object_ptr<td_api::scopeAutosaveSettings>
AutosaveManager::DialogAutosaveSettings::get_scope_autosave_settings_object() const {
  return td_api::make_object<td_api::scopeAutosaveSettings>(autosave_photos_, autosave_videos_, max_video_file_size_);
}

td_api::object_ptr<td_api::autosaveSettingsException>
AutosaveManager::DialogAutosaveSettings::get_autosave_settings_exception_object(DialogId dialog_id) const {
  return td_api::make_object<td_api::autosaveSettingsException>(dialog_id.get(), get_scope_autosave_settings_object());
}

td_api::object_ptr<td_api::autosaveSettings> AutosaveManager::AutosaveSettings::get_autosave_settings_object() const {
  CHECK(are_inited_);
  auto exceptions = transform(exceptions_, [](const auto &exception) {
    return exception.second.get_autosave_settings_exception_object(exception.first);
  });
  return td_api::make_object<td_api::autosaveSettings>(
      user_settings_.get_scope_autosave_settings_object(), user_settings_.get_scope_autosave_settings_object(),
      user_settings_.get_scope_autosave_settings_object(), std::move(exceptions));
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

void AutosaveManager::clear_autosave_settings_excpetions(Promise<Unit> &&promise) {
  settings_.exceptions_.clear();
  td_->create_handler<DeleteAutosaveExceptionsQuery>(std::move(promise))->send();
}

}  // namespace td
