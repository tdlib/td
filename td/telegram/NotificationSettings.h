//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
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
  bool is_synchronized = false;

  DialogNotificationSettings() = default;

  DialogNotificationSettings(bool use_default_mute_until, int32 mute_until, bool use_default_sound, string sound,
                             bool use_default_show_preview, bool show_preview, bool silent_send_message)
      : mute_until(mute_until)
      , sound(std::move(sound))
      , show_preview(show_preview)
      , silent_send_message(silent_send_message)
      , use_default_mute_until(use_default_mute_until)
      , use_default_sound(use_default_sound)
      , use_default_show_preview(use_default_show_preview)
      , is_synchronized(true) {
  }
};

enum class NotificationSettingsScope : int32 { Private, Group };

class ScopeNotificationSettings {
 public:
  int32 mute_until = 0;
  string sound = "default";
  bool show_preview = true;
  bool is_synchronized = false;

  ScopeNotificationSettings() = default;

  ScopeNotificationSettings(int32 mute_until, string sound, bool show_preview)
      : mute_until(mute_until), sound(std::move(sound)), show_preview(show_preview), is_synchronized(true) {
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

DialogNotificationSettings get_dialog_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings);

ScopeNotificationSettings get_scope_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings);

}  // namespace td
