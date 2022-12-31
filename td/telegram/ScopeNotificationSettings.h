//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/NotificationSound.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class ScopeNotificationSettings {
 public:
  int32 mute_until = 0;
  unique_ptr<NotificationSound> sound;
  bool show_preview = true;
  bool is_synchronized = false;

  // local settings
  bool disable_pinned_message_notifications = false;
  bool disable_mention_notifications = false;

  ScopeNotificationSettings() = default;

  ScopeNotificationSettings(int32 mute_until, unique_ptr<NotificationSound> &&sound, bool show_preview,
                            bool disable_pinned_message_notifications, bool disable_mention_notifications)
      : mute_until(mute_until)
      , sound(std::move(sound))
      , show_preview(show_preview)
      , is_synchronized(true)
      , disable_pinned_message_notifications(disable_pinned_message_notifications)
      , disable_mention_notifications(disable_mention_notifications) {
  }
};

StringBuilder &operator<<(StringBuilder &string_builder, const ScopeNotificationSettings &notification_settings);

td_api::object_ptr<td_api::scopeNotificationSettings> get_scope_notification_settings_object(
    const ScopeNotificationSettings *notification_settings);

Result<ScopeNotificationSettings> get_scope_notification_settings(
    td_api::object_ptr<td_api::scopeNotificationSettings> &&notification_settings);

ScopeNotificationSettings get_scope_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings,
                                                          bool old_disable_pinned_message_notifications,
                                                          bool old_disable_mention_notifications);

}  // namespace td
