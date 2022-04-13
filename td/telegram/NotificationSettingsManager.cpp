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
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/logevent/LogEventHelper.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/NotificationSettings.hpp"
#include "td/telegram/NotificationSound.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/tl_helpers.h"

namespace td {

class GetSavedRingtonesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::account_SavedRingtones>> promise_;

 public:
  explicit GetSavedRingtonesQuery(Promise<telegram_api::object_ptr<telegram_api::account_SavedRingtones>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int64 hash) {
    send_query(G()->net_query_creator().create(telegram_api::account_getSavedRingtones(hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getSavedRingtones>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedRingtonesQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetDialogNotifySettingsQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_notify_peer = td_->notification_settings_manager_->get_input_notify_peer(dialog_id);
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
        telegram_api::account_getNotifyExceptions(flags, false /*ignored*/, std::move(input_notify_peer))));
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
    td_->notification_settings_manager_->on_update_scope_notify_settings(scope_, std::move(ptr));

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

    auto input_notify_peer = td_->notification_settings_manager_->get_input_notify_peer(dialog_id);
    if (input_notify_peer == nullptr) {
      return on_error(Status::Error(500, "Can't update chat notification settings"));
    }

    int32 flags = 0;
    if (!new_settings.use_default_mute_until) {
      flags |= telegram_api::inputPeerNotifySettings::MUTE_UNTIL_MASK;
    }
    if (new_settings.sound != nullptr) {
      flags |= telegram_api::inputPeerNotifySettings::SOUND_MASK;
    }
    if (!new_settings.use_default_show_preview) {
      flags |= telegram_api::inputPeerNotifySettings::SHOW_PREVIEWS_MASK;
    }
    if (new_settings.silent_send_message) {
      flags |= telegram_api::inputPeerNotifySettings::SILENT_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::account_updateNotifySettings(
        std::move(input_notify_peer), make_tl_object<telegram_api::inputPeerNotifySettings>(
                                          flags, new_settings.show_preview, new_settings.silent_send_message,
                                          new_settings.mute_until, get_input_notification_sound(new_settings.sound)))));
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

    if (!td_->auth_manager_->is_bot() &&
        td_->notification_settings_manager_->get_input_notify_peer(dialog_id_) != nullptr) {
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
                  telegram_api::inputPeerNotifySettings::SHOW_PREVIEWS_MASK;
    if (new_settings.sound != nullptr) {
      flags |= telegram_api::inputPeerNotifySettings::SOUND_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::account_updateNotifySettings(
        std::move(input_notify_peer), make_tl_object<telegram_api::inputPeerNotifySettings>(
                                          flags, new_settings.show_preview, false, new_settings.mute_until,
                                          get_input_notification_sound(new_settings.sound)))));
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
  scope_unmute_timeout_.set_callback(on_scope_unmute_timeout_callback);
  scope_unmute_timeout_.set_callback_data(static_cast<void *>(this));
}

NotificationSettingsManager::~NotificationSettingsManager() = default;

void NotificationSettingsManager::tear_down() {
  parent_.reset();
}

void NotificationSettingsManager::start_up() {
  init();
}

void NotificationSettingsManager::init() {
  if (is_inited_) {
    return;
  }
  is_inited_ = true;

  bool is_authorized = td_->auth_manager_->is_authorized();
  bool was_authorized_user = td_->auth_manager_->was_authorized() && !td_->auth_manager_->is_bot();
  if (was_authorized_user) {
    for (auto scope :
         {NotificationSettingsScope::Private, NotificationSettingsScope::Group, NotificationSettingsScope::Channel}) {
      auto notification_settings_string =
          G()->td_db()->get_binlog_pmc()->get(get_notification_settings_scope_database_key(scope));
      if (!notification_settings_string.empty()) {
        auto current_settings = get_scope_notification_settings(scope);
        CHECK(current_settings != nullptr);
        log_event_parse(*current_settings, notification_settings_string).ensure();

        VLOG(notifications) << "Loaded notification settings in " << scope << ": " << *current_settings;

        schedule_scope_unmute(scope, current_settings->mute_until);

        send_closure(G()->td(), &Td::send_update, get_update_scope_notification_settings_object(scope));
      }
    }
    if (!channels_notification_settings_.is_synchronized && is_authorized) {
      channels_notification_settings_ = ScopeNotificationSettings(
          chats_notification_settings_.mute_until, dup_notification_sound(chats_notification_settings_.sound),
          chats_notification_settings_.show_preview, false, false);
      channels_notification_settings_.is_synchronized = false;
      send_get_scope_notification_settings_query(NotificationSettingsScope::Channel, Promise<>());
    }
  }
  G()->td_db()->get_binlog_pmc()->erase("nsfac");
}

void NotificationSettingsManager::on_scope_unmute_timeout_callback(void *notification_settings_manager_ptr,
                                                                   int64 scope_int) {
  if (G()->close_flag()) {
    return;
  }

  CHECK(1 <= scope_int && scope_int <= 3);
  auto notification_settings_manager = static_cast<NotificationSettingsManager *>(notification_settings_manager_ptr);
  send_closure_later(notification_settings_manager->actor_id(notification_settings_manager),
                     &NotificationSettingsManager::on_scope_unmute,
                     static_cast<NotificationSettingsScope>(scope_int - 1));
}

void NotificationSettingsManager::timeout_expired() {
  reload_ringtones(Promise<Unit>());
}

int32 NotificationSettingsManager::get_scope_mute_until(NotificationSettingsScope scope) const {
  return get_scope_notification_settings(scope)->mute_until;
}

bool NotificationSettingsManager::get_scope_disable_pinned_message_notifications(
    NotificationSettingsScope scope) const {
  return get_scope_notification_settings(scope)->disable_pinned_message_notifications;
}

bool NotificationSettingsManager::get_scope_disable_mention_notifications(NotificationSettingsScope scope) const {
  return get_scope_notification_settings(scope)->disable_mention_notifications;
}

tl_object_ptr<telegram_api::InputNotifyPeer> NotificationSettingsManager::get_input_notify_peer(
    DialogId dialog_id) const {
  if (!td_->messages_manager_->have_dialog(dialog_id)) {
    return nullptr;
  }
  auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
  if (input_peer == nullptr) {
    return nullptr;
  }
  return make_tl_object<telegram_api::inputNotifyPeer>(std::move(input_peer));
}

ScopeNotificationSettings *NotificationSettingsManager::get_scope_notification_settings(
    NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return &users_notification_settings_;
    case NotificationSettingsScope::Group:
      return &chats_notification_settings_;
    case NotificationSettingsScope::Channel:
      return &channels_notification_settings_;
    default:
      UNREACHABLE();
      return nullptr;
  }
}

const ScopeNotificationSettings *NotificationSettingsManager::get_scope_notification_settings(
    NotificationSettingsScope scope) const {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return &users_notification_settings_;
    case NotificationSettingsScope::Group:
      return &chats_notification_settings_;
    case NotificationSettingsScope::Channel:
      return &channels_notification_settings_;
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::updateScopeNotificationSettings>
NotificationSettingsManager::get_update_scope_notification_settings_object(NotificationSettingsScope scope) const {
  auto notification_settings = get_scope_notification_settings(scope);
  CHECK(notification_settings != nullptr);
  return td_api::make_object<td_api::updateScopeNotificationSettings>(
      get_notification_settings_scope_object(scope), get_scope_notification_settings_object(notification_settings));
}

void NotificationSettingsManager::on_scope_unmute(NotificationSettingsScope scope) {
  if (td_->auth_manager_->is_bot()) {
    // just in case
    return;
  }

  auto notification_settings = get_scope_notification_settings(scope);
  CHECK(notification_settings != nullptr);

  if (notification_settings->mute_until == 0) {
    return;
  }

  auto now = G()->unix_time();
  if (notification_settings->mute_until > now) {
    LOG(ERROR) << "Failed to unmute " << scope << " in " << now << ", will be unmuted in "
               << notification_settings->mute_until;
    schedule_scope_unmute(scope, notification_settings->mute_until);
    return;
  }

  LOG(INFO) << "Unmute " << scope;
  update_scope_unmute_timeout(scope, notification_settings->mute_until, 0);
  send_closure(G()->td(), &Td::send_update, get_update_scope_notification_settings_object(scope));
  save_scope_notification_settings(scope, *notification_settings);
}

string NotificationSettingsManager::get_notification_settings_scope_database_key(NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return "nsfpc";
    case NotificationSettingsScope::Group:
      return "nsfgc";
    case NotificationSettingsScope::Channel:
      return "nsfcc";
    default:
      UNREACHABLE();
      return "";
  }
}

void NotificationSettingsManager::save_scope_notification_settings(NotificationSettingsScope scope,
                                                                   const ScopeNotificationSettings &new_settings) {
  string key = get_notification_settings_scope_database_key(scope);
  G()->td_db()->get_binlog_pmc()->set(key, log_event_store(new_settings).as_slice().str());
}

void NotificationSettingsManager::on_update_scope_notify_settings(
    NotificationSettingsScope scope, tl_object_ptr<telegram_api::peerNotifySettings> &&peer_notify_settings) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto old_notification_settings = get_scope_notification_settings(scope);
  CHECK(old_notification_settings != nullptr);

  ScopeNotificationSettings notification_settings = ::td::get_scope_notification_settings(
      std::move(peer_notify_settings), old_notification_settings->disable_pinned_message_notifications,
      old_notification_settings->disable_mention_notifications);
  if (!notification_settings.is_synchronized) {
    return;
  }

  update_scope_notification_settings(scope, old_notification_settings, std::move(notification_settings));
}

bool NotificationSettingsManager::update_scope_notification_settings(NotificationSettingsScope scope,
                                                                     ScopeNotificationSettings *current_settings,
                                                                     ScopeNotificationSettings &&new_settings) {
  if (td_->auth_manager_->is_bot()) {
    // just in case
    return false;
  }

  bool need_update_server = current_settings->mute_until != new_settings.mute_until ||
                            !are_equivalent_notification_sounds(current_settings->sound, new_settings.sound) ||
                            current_settings->show_preview != new_settings.show_preview;
  bool need_update_local =
      current_settings->disable_pinned_message_notifications != new_settings.disable_pinned_message_notifications ||
      current_settings->disable_mention_notifications != new_settings.disable_mention_notifications;
  bool was_inited = current_settings->is_synchronized;
  bool is_inited = new_settings.is_synchronized;
  if (was_inited && !is_inited) {
    return false;  // just in case
  }
  bool is_changed = need_update_server || need_update_local || was_inited != is_inited ||
                    are_different_equivalent_notification_sounds(current_settings->sound, new_settings.sound);
  if (is_changed) {
    save_scope_notification_settings(scope, new_settings);

    VLOG(notifications) << "Update notification settings in " << scope << " from " << *current_settings << " to "
                        << new_settings;

    update_scope_unmute_timeout(scope, current_settings->mute_until, new_settings.mute_until);

    if (!current_settings->disable_pinned_message_notifications && new_settings.disable_pinned_message_notifications) {
      td_->messages_manager_->remove_scope_pinned_message_notifications(scope);
    }
    if (current_settings->disable_mention_notifications != new_settings.disable_mention_notifications) {
      td_->messages_manager_->on_update_scope_mention_notifications(scope, new_settings.disable_mention_notifications);
    }

    *current_settings = std::move(new_settings);

    send_closure(G()->td(), &Td::send_update, get_update_scope_notification_settings_object(scope));
  }
  return need_update_server;
}

void NotificationSettingsManager::schedule_scope_unmute(NotificationSettingsScope scope, int32 mute_until) {
  auto now = G()->unix_time_cached();
  if (mute_until >= now && mute_until < now + 366 * 86400) {
    scope_unmute_timeout_.set_timeout_in(static_cast<int64>(scope) + 1, mute_until - now + 1);
  } else {
    scope_unmute_timeout_.cancel_timeout(static_cast<int64>(scope) + 1);
  }
}

void NotificationSettingsManager::update_scope_unmute_timeout(NotificationSettingsScope scope, int32 &old_mute_until,
                                                              int32 new_mute_until) {
  if (td_->auth_manager_->is_bot()) {
    // just in case
    return;
  }

  LOG(INFO) << "Update " << scope << " unmute timeout from " << old_mute_until << " to " << new_mute_until;
  if (old_mute_until == new_mute_until) {
    return;
  }
  CHECK(old_mute_until >= 0);

  schedule_scope_unmute(scope, new_mute_until);

  auto was_muted = old_mute_until != 0;
  auto is_muted = new_mute_until != 0;

  old_mute_until = new_mute_until;

  if (was_muted != is_muted) {
    td_->messages_manager_->on_update_notification_scope_is_muted(scope, is_muted);
  }
}

void NotificationSettingsManager::reset_scope_notification_settings() {
  CHECK(!td_->auth_manager_->is_bot());

  for (auto scope :
       {NotificationSettingsScope::Private, NotificationSettingsScope::Group, NotificationSettingsScope::Channel}) {
    auto current_settings = get_scope_notification_settings(scope);
    CHECK(current_settings != nullptr);
    ScopeNotificationSettings new_scope_settings;
    new_scope_settings.is_synchronized = true;
    update_scope_notification_settings(scope, current_settings, std::move(new_scope_settings));
  }
}

bool NotificationSettingsManager::is_active() const {
  return !G()->close_flag() && td_->auth_manager_->is_authorized() && !td_->auth_manager_->is_bot();
}

Result<FileId> NotificationSettingsManager::get_ringtone(
    telegram_api::object_ptr<telegram_api::Document> &&ringtone) const {
  int32 document_id = ringtone->get_id();
  if (document_id == telegram_api::documentEmpty::ID) {
    return Status::Error("Received an empty ringtone");
  }
  CHECK(document_id == telegram_api::document::ID);

  auto parsed_document =
      td_->documents_manager_->on_get_document(move_tl_object_as<telegram_api::document>(ringtone), DialogId(), nullptr,
                                               Document::Type::Audio, false, false, true);
  if (parsed_document.type != Document::Type::Audio) {
    return Status::Error("Receive ringtone of a wrong type");
  }
  return parsed_document.file_id;
}

void NotificationSettingsManager::reload_ringtones(Promise<Unit> &&promise) {
  if (!is_active()) {
    return;
  }
  reload_ringtone_queries_.push_back(std::move(promise));
  if (reload_ringtone_queries_.size() == 1) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this)](Result<telegram_api::object_ptr<telegram_api::account_SavedRingtones>> &&result) {
          send_closure(actor_id, &NotificationSettingsManager::on_reload_ringtones, std::move(result));
        });
    td_->create_handler<GetSavedRingtonesQuery>(std::move(query_promise))->send(ringtone_hash_);
  }
}

void NotificationSettingsManager::on_reload_ringtones(
    Result<telegram_api::object_ptr<telegram_api::account_SavedRingtones>> &&result) {
  if (!is_active()) {
    set_promises(reload_ringtone_queries_);
    return;
  }
  if (result.is_error()) {
    fail_promises(reload_ringtone_queries_, result.move_as_error());
    set_timeout_in(Random::fast(60, 120));
    return;
  }

  set_timeout_in(Random::fast(3600, 4800));

  auto ringtones_ptr = result.move_as_ok();
  auto constructor_id = ringtones_ptr->get_id();
  if (constructor_id == telegram_api::account_savedRingtonesNotModified::ID) {
    set_promises(reload_ringtone_queries_);
    return;
  }
  CHECK(constructor_id == telegram_api::account_savedRingtones::ID);
  auto ringtones = move_tl_object_as<telegram_api::account_savedRingtones>(ringtones_ptr);

  auto new_hash = ringtones->hash_;
  vector<FileId> new_ringtone_file_ids;

  for (auto &ringtone : ringtones->ringtones_) {
    auto r_ringtone = get_ringtone(std::move(ringtone));
    if (r_ringtone.is_error()) {
      LOG(ERROR) << r_ringtone.error().message();
      new_hash = 0;
      continue;
    }

    new_ringtone_file_ids.push_back(r_ringtone.move_as_ok());
  }

  bool need_update = new_ringtone_file_ids != ringtone_file_ids_;
  if (need_update || ringtone_hash_ != new_hash) {
    ringtone_hash_ = new_hash;
    ringtone_file_ids_ = std::move(new_ringtone_file_ids);
  }
  set_promises(reload_ringtone_queries_);
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

const ScopeNotificationSettings *NotificationSettingsManager::get_scope_notification_settings(
    NotificationSettingsScope scope, Promise<Unit> &&promise) {
  const ScopeNotificationSettings *notification_settings = get_scope_notification_settings(scope);
  CHECK(notification_settings != nullptr);
  if (!notification_settings->is_synchronized && !td_->auth_manager_->is_bot()) {
    send_get_scope_notification_settings_query(scope, std::move(promise));
    return nullptr;
  }

  promise.set_value(Unit());
  return notification_settings;
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

  if (status.is_ok()) {
    set_promises(promises);
  } else {
    fail_promises(promises, std::move(status));
  }
}

void NotificationSettingsManager::update_dialog_notify_settings(DialogId dialog_id,
                                                                const DialogNotificationSettings &new_settings,
                                                                Promise<Unit> &&promise) {
  td_->create_handler<UpdateDialogNotifySettingsQuery>(std::move(promise))->send(dialog_id, new_settings);
}

Status NotificationSettingsManager::set_scope_notification_settings(
    NotificationSettingsScope scope, td_api::object_ptr<td_api::scopeNotificationSettings> &&notification_settings) {
  CHECK(!td_->auth_manager_->is_bot());
  auto *current_settings = get_scope_notification_settings(scope);
  CHECK(current_settings != nullptr);
  TRY_RESULT(new_settings, ::td::get_scope_notification_settings(std::move(notification_settings)));
  if (is_notification_sound_default(current_settings->sound) && is_notification_sound_default(new_settings.sound)) {
    new_settings.sound = dup_notification_sound(current_settings->sound);
  }
  if (update_scope_notification_settings(scope, current_settings, std::move(new_settings))) {
    update_scope_notification_settings_on_server(scope, 0);
  }
  return Status::OK();
}

class NotificationSettingsManager::UpdateScopeNotificationSettingsOnServerLogEvent {
 public:
  NotificationSettingsScope scope_;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(scope_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(scope_, parser);
  }
};

uint64 NotificationSettingsManager::save_update_scope_notification_settings_on_server_log_event(
    NotificationSettingsScope scope) {
  UpdateScopeNotificationSettingsOnServerLogEvent log_event{scope};
  return binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::UpdateScopeNotificationSettingsOnServer,
                    get_log_event_storer(log_event));
}

void NotificationSettingsManager::update_scope_notification_settings_on_server(NotificationSettingsScope scope,
                                                                               uint64 log_event_id) {
  CHECK(!td_->auth_manager_->is_bot());
  if (log_event_id == 0) {
    log_event_id = save_update_scope_notification_settings_on_server_log_event(scope);
  }

  LOG(INFO) << "Update " << scope << " notification settings on server with log_event " << log_event_id;
  td_->create_handler<UpdateScopeNotifySettingsQuery>(get_erase_log_event_promise(log_event_id))
      ->send(scope, *get_scope_notification_settings(scope));
}

void NotificationSettingsManager::reset_notify_settings(Promise<Unit> &&promise) {
  td_->create_handler<ResetNotifySettingsQuery>(std::move(promise))->send();
}

void NotificationSettingsManager::get_notify_settings_exceptions(NotificationSettingsScope scope, bool filter_scope,
                                                                 bool compare_sound, Promise<Unit> &&promise) {
  td_->create_handler<GetNotifySettingsExceptionsQuery>(std::move(promise))->send(scope, filter_scope, compare_sound);
}

void NotificationSettingsManager::after_get_difference() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!users_notification_settings_.is_synchronized) {
    send_get_scope_notification_settings_query(NotificationSettingsScope::Private, Promise<>());
  }
  if (!chats_notification_settings_.is_synchronized) {
    send_get_scope_notification_settings_query(NotificationSettingsScope::Group, Promise<>());
  }
  if (!channels_notification_settings_.is_synchronized) {
    send_get_scope_notification_settings_query(NotificationSettingsScope::Channel, Promise<>());
  }

  if (td_->is_online() && !are_ringtones_reloaded_) {
    are_ringtones_reloaded_ = true;
    reload_ringtones(Auto());
  }
}

void NotificationSettingsManager::on_binlog_events(vector<BinlogEvent> &&events) {
  if (G()->close_flag()) {
    return;
  }
  for (auto &event : events) {
    CHECK(event.id_ != 0);
    switch (event.type_) {
      case LogEvent::HandlerType::UpdateScopeNotificationSettingsOnServer: {
        UpdateScopeNotificationSettingsOnServerLogEvent log_event;
        log_event_parse(log_event, event.data_).ensure();

        update_scope_notification_settings_on_server(log_event.scope_, event.id_);
        break;
      }
      default:
        LOG(FATAL) << "Unsupported log event type " << event.type_;
    }
  }
}

void NotificationSettingsManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  for (auto scope :
       {NotificationSettingsScope::Private, NotificationSettingsScope::Group, NotificationSettingsScope::Channel}) {
    auto current_settings = get_scope_notification_settings(scope);
    CHECK(current_settings != nullptr);
    if (current_settings->is_synchronized) {
      updates.push_back(get_update_scope_notification_settings_object(scope));
    }
  }
}

}  // namespace td
