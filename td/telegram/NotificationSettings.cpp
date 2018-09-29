//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationSettings.h"

#include "td/telegram/Global.h"

#include "td/utils/logging.h"

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, const DialogNotificationSettings &notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.silent_send_message
                        << ", " << notification_settings.use_default_mute_until << ", "
                        << notification_settings.use_default_sound << ", "
                        << notification_settings.use_default_show_preview << ", "
                        << notification_settings.is_synchronized << "]";
}

StringBuilder &operator<<(StringBuilder &string_builder, NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return string_builder << "notification settings for private chats";
    case NotificationSettingsScope::Group:
      return string_builder << "notification settings for group chats";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const ScopeNotificationSettings &notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.is_synchronized << "]";
}

td_api::object_ptr<td_api::NotificationSettingsScope> get_notification_settings_scope_object(
    NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return td_api::make_object<td_api::notificationSettingsScopePrivateChats>();
    case NotificationSettingsScope::Group:
      return td_api::make_object<td_api::notificationSettingsScopeGroupChats>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::chatNotificationSettings> get_chat_notification_settings_object(
    const DialogNotificationSettings *notification_settings) {
  return td_api::make_object<td_api::chatNotificationSettings>(
      notification_settings->use_default_mute_until, max(0, notification_settings->mute_until - G()->unix_time()),
      notification_settings->use_default_sound, notification_settings->sound,
      notification_settings->use_default_show_preview, notification_settings->show_preview);
}

td_api::object_ptr<td_api::scopeNotificationSettings> get_scope_notification_settings_object(
    const ScopeNotificationSettings *notification_settings) {
  return td_api::make_object<td_api::scopeNotificationSettings>(
      max(0, notification_settings->mute_until - G()->unix_time()), notification_settings->sound,
      notification_settings->show_preview);
}

telegram_api::object_ptr<telegram_api::InputNotifyPeer> get_input_notify_peer(NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return telegram_api::make_object<telegram_api::inputNotifyUsers>();
    case NotificationSettingsScope::Group:
      return telegram_api::make_object<telegram_api::inputNotifyChats>();
    default:
      return nullptr;
  }
}

NotificationSettingsScope get_notification_settings_scope(
    const td_api::object_ptr<td_api::NotificationSettingsScope> &scope) {
  CHECK(scope != nullptr);
  switch (scope->get_id()) {
    case td_api::notificationSettingsScopePrivateChats::ID:
      return NotificationSettingsScope::Private;
    case td_api::notificationSettingsScopeGroupChats::ID:
      return NotificationSettingsScope::Group;
    default:
      UNREACHABLE();
      return NotificationSettingsScope::Private;
  }
}

DialogNotificationSettings get_dialog_notification_settings(
    tl_object_ptr<telegram_api::peerNotifySettings> &&settings) {
  bool use_default_mute_until = (settings->flags_ & telegram_api::peerNotifySettings::MUTE_UNTIL_MASK) == 0;
  bool use_default_sound = (settings->flags_ & telegram_api::peerNotifySettings::SOUND_MASK) == 0;
  bool use_default_show_preview = (settings->flags_ & telegram_api::peerNotifySettings::SHOW_PREVIEWS_MASK) == 0;
  auto mute_until = use_default_mute_until || settings->mute_until_ <= G()->unix_time() ? 0 : settings->mute_until_;
  bool silent_send_message =
      (settings->flags_ & telegram_api::peerNotifySettings::SILENT_MASK) == 0 ? false : settings->silent_;
  return {use_default_mute_until,   mute_until,
          use_default_sound,        std::move(settings->sound_),
          use_default_show_preview, settings->show_previews_,
          silent_send_message};
}

ScopeNotificationSettings get_scope_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings) {
  auto mute_until = (settings->flags_ & telegram_api::peerNotifySettings::MUTE_UNTIL_MASK) == 0 ||
                            settings->mute_until_ <= G()->unix_time()
                        ? 0
                        : settings->mute_until_;
  auto sound =
      (settings->flags_ & telegram_api::peerNotifySettings::SOUND_MASK) == 0 ? "default" : std::move(settings->sound_);
  auto show_preview =
      (settings->flags_ & telegram_api::peerNotifySettings::SHOW_PREVIEWS_MASK) == 0 ? false : settings->show_previews_;
  return {mute_until, std::move(sound), show_preview};
}

}  // namespace td
