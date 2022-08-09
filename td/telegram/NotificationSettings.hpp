//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/NotificationSettings.h"
#include "td/telegram/NotificationSound.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const DialogNotificationSettings &notification_settings, StorerT &storer) {
  bool is_muted = !notification_settings.use_default_mute_until && notification_settings.mute_until != 0 &&
                  notification_settings.mute_until > G()->unix_time();
  bool has_sound = notification_settings.sound != nullptr;
  bool has_ringtone_support = true;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_muted);
  STORE_FLAG(has_sound);
  STORE_FLAG(notification_settings.show_preview);
  STORE_FLAG(notification_settings.silent_send_message);
  STORE_FLAG(notification_settings.is_synchronized);
  STORE_FLAG(notification_settings.use_default_mute_until);
  STORE_FLAG(false);  // use_default_sound
  STORE_FLAG(notification_settings.use_default_show_preview);
  STORE_FLAG(notification_settings.is_use_default_fixed);
  STORE_FLAG(!notification_settings.use_default_disable_pinned_message_notifications);
  STORE_FLAG(notification_settings.disable_pinned_message_notifications);
  STORE_FLAG(!notification_settings.use_default_disable_mention_notifications);
  STORE_FLAG(notification_settings.disable_mention_notifications);
  STORE_FLAG(notification_settings.is_secret_chat_show_preview_fixed);
  STORE_FLAG(has_ringtone_support);
  END_STORE_FLAGS();
  if (is_muted) {
    store(notification_settings.mute_until, storer);
  }
  if (has_sound) {
    store(notification_settings.sound, storer);
  }
}

template <class ParserT>
void parse(DialogNotificationSettings &notification_settings, ParserT &parser) {
  bool is_muted;
  bool has_sound;
  bool use_default_sound;
  bool use_disable_pinned_message_notifications;
  bool use_disable_mention_notifications;
  bool has_ringtone_support;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_muted);
  PARSE_FLAG(has_sound);
  PARSE_FLAG(notification_settings.show_preview);
  PARSE_FLAG(notification_settings.silent_send_message);
  PARSE_FLAG(notification_settings.is_synchronized);
  PARSE_FLAG(notification_settings.use_default_mute_until);
  PARSE_FLAG(use_default_sound);
  PARSE_FLAG(notification_settings.use_default_show_preview);
  PARSE_FLAG(notification_settings.is_use_default_fixed);
  PARSE_FLAG(use_disable_pinned_message_notifications);
  PARSE_FLAG(notification_settings.disable_pinned_message_notifications);
  PARSE_FLAG(use_disable_mention_notifications);
  PARSE_FLAG(notification_settings.disable_mention_notifications);
  PARSE_FLAG(notification_settings.is_secret_chat_show_preview_fixed);
  PARSE_FLAG(has_ringtone_support);
  END_PARSE_FLAGS();
  notification_settings.use_default_disable_pinned_message_notifications = !use_disable_pinned_message_notifications;
  notification_settings.use_default_disable_mention_notifications = !use_disable_mention_notifications;
  if (is_muted) {
    parse(notification_settings.mute_until, parser);
  }
  if (has_sound) {
    if (has_ringtone_support) {
      parse_notification_sound(notification_settings.sound, parser);
    } else {
      string sound;
      parse(sound, parser);
      notification_settings.sound = use_default_sound ? nullptr : get_legacy_notification_sound(sound);
    }
  }
}

template <class StorerT>
void store(const ScopeNotificationSettings &notification_settings, StorerT &storer) {
  bool is_muted = notification_settings.mute_until != 0 && notification_settings.mute_until > G()->unix_time();
  bool has_sound = notification_settings.sound != nullptr;
  bool has_ringtone_support = true;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_muted);
  STORE_FLAG(has_sound);
  STORE_FLAG(notification_settings.show_preview);
  STORE_FLAG(false);
  STORE_FLAG(notification_settings.is_synchronized);
  STORE_FLAG(notification_settings.disable_pinned_message_notifications);
  STORE_FLAG(notification_settings.disable_mention_notifications);
  STORE_FLAG(has_ringtone_support);
  END_STORE_FLAGS();
  if (is_muted) {
    store(notification_settings.mute_until, storer);
  }
  if (has_sound) {
    store(notification_settings.sound, storer);
  }
}

template <class ParserT>
void parse(ScopeNotificationSettings &notification_settings, ParserT &parser) {
  bool is_muted;
  bool has_sound;
  bool silent_send_message_ignored;
  bool has_ringtone_support;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_muted);
  PARSE_FLAG(has_sound);
  PARSE_FLAG(notification_settings.show_preview);
  PARSE_FLAG(silent_send_message_ignored);
  PARSE_FLAG(notification_settings.is_synchronized);
  PARSE_FLAG(notification_settings.disable_pinned_message_notifications);
  PARSE_FLAG(notification_settings.disable_mention_notifications);
  PARSE_FLAG(has_ringtone_support);
  END_PARSE_FLAGS();
  (void)silent_send_message_ignored;
  if (is_muted) {
    parse(notification_settings.mute_until, parser);
  }
  if (has_sound) {
    if (has_ringtone_support) {
      parse_notification_sound(notification_settings.sound, parser);
    } else {
      string sound;
      parse(sound, parser);
      notification_settings.sound = get_legacy_notification_sound(sound);
    }
  }
}

}  // namespace td
