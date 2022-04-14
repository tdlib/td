//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/NotificationSettings.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Status.h"

namespace td {

struct BinlogEvent;

class Td;

class NotificationSettingsManager final : public Actor {
 public:
  NotificationSettingsManager(Td *td, ActorShared<> parent);
  NotificationSettingsManager(const NotificationSettingsManager &) = delete;
  NotificationSettingsManager &operator=(const NotificationSettingsManager &) = delete;
  NotificationSettingsManager(NotificationSettingsManager &&) = delete;
  NotificationSettingsManager &operator=(NotificationSettingsManager &&) = delete;
  ~NotificationSettingsManager() final;

  int32 get_scope_mute_until(NotificationSettingsScope scope) const;

  bool get_scope_disable_pinned_message_notifications(NotificationSettingsScope scope) const;

  bool get_scope_disable_mention_notifications(NotificationSettingsScope scope) const;

  tl_object_ptr<telegram_api::InputNotifyPeer> get_input_notify_peer(DialogId dialog_id) const;

  void on_update_scope_notify_settings(NotificationSettingsScope scope,
                                       tl_object_ptr<telegram_api::peerNotifySettings> &&peer_notify_settings);

  FileId get_saved_ringtone(int64 ringtone_id, Promise<Unit> &&promise);

  vector<FileId> get_saved_ringtones(Promise<Unit> &&promise);

  void reload_saved_ringtones(Promise<Unit> &&promise);

  void send_get_dialog_notification_settings_query(DialogId dialog_id, Promise<Unit> &&promise);

  const ScopeNotificationSettings *get_scope_notification_settings(NotificationSettingsScope scope,
                                                                   Promise<Unit> &&promise);
  void send_get_scope_notification_settings_query(NotificationSettingsScope scope, Promise<Unit> &&promise);

  void on_get_dialog_notification_settings_query_finished(DialogId dialog_id, Status &&status);

  void update_dialog_notify_settings(DialogId dialog_id, const DialogNotificationSettings &new_settings,
                                     Promise<Unit> &&promise);

  Status set_scope_notification_settings(NotificationSettingsScope scope,
                                         td_api::object_ptr<td_api::scopeNotificationSettings> &&notification_settings)
      TD_WARN_UNUSED_RESULT;

  void reset_scope_notification_settings();

  void reset_notify_settings(Promise<Unit> &&promise);

  void get_notify_settings_exceptions(NotificationSettingsScope scope, bool filter_scope, bool compare_sound,
                                      Promise<Unit> &&promise);

  void init();

  void after_get_difference();

  void on_binlog_events(vector<BinlogEvent> &&events);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  class UpdateScopeNotificationSettingsOnServerLogEvent;

  void start_up() final;

  void tear_down() final;

  void timeout_expired() final;

  bool is_active() const;

  static void on_scope_unmute_timeout_callback(void *notification_settings_manager_ptr, int64 scope_int);

  Result<FileId> get_ringtone(telegram_api::object_ptr<telegram_api::Document> &&ringtone) const;

  void on_reload_saved_ringtones(Result<telegram_api::object_ptr<telegram_api::account_SavedRingtones>> &&result);

  ScopeNotificationSettings *get_scope_notification_settings(NotificationSettingsScope scope);

  const ScopeNotificationSettings *get_scope_notification_settings(NotificationSettingsScope scope) const;

  td_api::object_ptr<td_api::updateScopeNotificationSettings> get_update_scope_notification_settings_object(
      NotificationSettingsScope scope) const;

  void on_scope_unmute(NotificationSettingsScope scope);

  static string get_notification_settings_scope_database_key(NotificationSettingsScope scope);

  static void save_scope_notification_settings(NotificationSettingsScope scope,
                                               const ScopeNotificationSettings &new_settings);

  bool update_scope_notification_settings(NotificationSettingsScope scope, ScopeNotificationSettings *current_settings,
                                          ScopeNotificationSettings &&new_settings);

  static uint64 save_update_scope_notification_settings_on_server_log_event(NotificationSettingsScope scope);

  void update_scope_notification_settings_on_server(NotificationSettingsScope scope, uint64 log_event_id);

  void schedule_scope_unmute(NotificationSettingsScope scope, int32 mute_until);

  void update_scope_unmute_timeout(NotificationSettingsScope scope, int32 &old_mute_until, int32 new_mute_until);

  Td *td_;
  ActorShared<> parent_;

  bool is_inited_ = false;
  bool are_saved_ringtones_loaded_ = false;
  bool are_saved_ringtones_reloaded_ = false;

  ScopeNotificationSettings users_notification_settings_;
  ScopeNotificationSettings chats_notification_settings_;
  ScopeNotificationSettings channels_notification_settings_;

  MultiTimeout scope_unmute_timeout_{"ScopeUnmuteTimeout"};

  int64 saved_ringtone_hash_ = 0;
  vector<FileId> saved_ringtone_file_ids_;

  vector<Promise<Unit>> reload_saved_ringtone_queries_;

  FlatHashMap<DialogId, vector<Promise<Unit>>, DialogIdHash> get_dialog_notification_settings_queries_;
};

}  // namespace td
