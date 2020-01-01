//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class DialogNotificationSettings {
 public:
  int32 mute_until = 0;
  string sound = "default";
  bool show_preview = true;
  bool silent_send_message = false;
  bool use_default_mute_until = true;
  bool use_default_sound = true;
  bool use_default_show_preview = true;
  bool is_use_default_fixed = true;
  bool is_secret_chat_show_preview_fixed = false;
  bool is_synchronized = false;

  // local settings
  bool use_default_disable_pinned_message_notifications = true;
  bool disable_pinned_message_notifications = false;
  bool use_default_disable_mention_notifications = true;
  bool disable_mention_notifications = false;

  DialogNotificationSettings() = default;

  DialogNotificationSettings(bool use_default_mute_until, int32 mute_until, bool use_default_sound, string sound,
                             bool use_default_show_preview, bool show_preview, bool silent_send_message,
                             bool use_default_disable_pinned_message_notifications,
                             bool disable_pinned_message_notifications, bool use_default_disable_mention_notifications,
                             bool disable_mention_notifications)
      : mute_until(mute_until)
      , sound(std::move(sound))
      , show_preview(show_preview)
      , silent_send_message(silent_send_message)
      , use_default_mute_until(use_default_mute_until)
      , use_default_sound(use_default_sound)
      , use_default_show_preview(use_default_show_preview)
      , is_synchronized(true)
      , use_default_disable_pinned_message_notifications(use_default_disable_pinned_message_notifications)
      , disable_pinned_message_notifications(disable_pinned_message_notifications)
      , use_default_disable_mention_notifications(use_default_disable_mention_notifications)
      , disable_mention_notifications(disable_mention_notifications) {
  }
};

enum class NotificationSettingsScope : int32 { Private, Group, Channel };

class ScopeNotificationSettings {
 public:
  int32 mute_until = 0;
  string sound = "default";
  bool show_preview = true;
  bool is_synchronized = false;

  // local settings
  bool disable_pinned_message_notifications = false;
  bool disable_mention_notifications = false;

  ScopeNotificationSettings() = default;

  ScopeNotificationSettings(int32 mute_until, string sound, bool show_preview,
                            bool disable_pinned_message_notifications, bool disable_mention_notifications)
      : mute_until(mute_until)
      , sound(std::move(sound))
      , show_preview(show_preview)
      , is_synchronized(true)
      , disable_pinned_message_notifications(disable_pinned_message_notifications)
      , disable_mention_notifications(disable_mention_notifications) {
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const DialogNotificationSettings &notification_settings);

StringBuilder &operator<<(StringBuilder &string_builder, NotificationSettingsScope scope);

StringBuilder &operator<<(StringBuilder &string_builder, const ScopeNotificationSettings &notification_settings);

td_api::object_ptr<td_api::NotificationSettingsScope> get_notification_settings_scope_object(
    NotificationSettingsScope scope);

td_api::object_ptr<td_api::chatNotificationSettings> get_chat_notification_settings_object(
    const DialogNotificationSettings *notification_settings);

td_api::object_ptr<td_api::scopeNotificationSettings> get_scope_notification_settings_object(
    const ScopeNotificationSettings *notification_settings);

telegram_api::object_ptr<telegram_api::InputNotifyPeer> get_input_notify_peer(NotificationSettingsScope scope);

NotificationSettingsScope get_notification_settings_scope(
    const td_api::object_ptr<td_api::NotificationSettingsScope> &scope);

Result<DialogNotificationSettings> get_dialog_notification_settings(
    td_api::object_ptr<td_api::chatNotificationSettings> &&notification_settings, bool old_silent_send_message);

Result<ScopeNotificationSettings> get_scope_notification_settings(
    td_api::object_ptr<td_api::scopeNotificationSettings> &&notification_settings);

DialogNotificationSettings get_dialog_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings,
                                                            bool old_use_default_disable_pinned_message_notifications,
                                                            bool old_disable_pinned_message_notifications,
                                                            bool old_use_default_disable_mention_notifications,
                                                            bool old_disable_mention_notifications);

ScopeNotificationSettings get_scope_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings,
                                                          bool old_disable_pinned_message_notifications,
                                                          bool old_disable_mention_notifications);

bool are_default_dialog_notification_settings(const DialogNotificationSettings &settings, bool compare_sound);

}  // namespace td
