//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationSettings.h"

#include "td/telegram/Global.h"
#include "td/telegram/misc.h"

#include "td/utils/common.h"

#include <limits>

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, const DialogNotificationSettings &notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.silent_send_message
                        << ", " << notification_settings.disable_pinned_message_notifications << ", "
                        << notification_settings.disable_mention_notifications << ", "
                        << notification_settings.use_default_mute_until << ", "
                        << notification_settings.use_default_sound << ", "
                        << notification_settings.use_default_show_preview << ", "
                        << notification_settings.use_default_disable_pinned_message_notifications << ", "
                        << notification_settings.use_default_disable_mention_notifications << ", "
                        << notification_settings.is_synchronized << "]";
}

StringBuilder &operator<<(StringBuilder &string_builder, NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return string_builder << "notification settings for private chats";
    case NotificationSettingsScope::Group:
      return string_builder << "notification settings for group chats";
    case NotificationSettingsScope::Channel:
      return string_builder << "notification settings for channel chats";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const ScopeNotificationSettings &notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.is_synchronized << ", "
                        << notification_settings.disable_pinned_message_notifications << ", "
                        << notification_settings.disable_mention_notifications << "]";
}

td_api::object_ptr<td_api::NotificationSettingsScope> get_notification_settings_scope_object(
    NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return td_api::make_object<td_api::notificationSettingsScopePrivateChats>();
    case NotificationSettingsScope::Group:
      return td_api::make_object<td_api::notificationSettingsScopeGroupChats>();
    case NotificationSettingsScope::Channel:
      return td_api::make_object<td_api::notificationSettingsScopeChannelChats>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::chatNotificationSettings> get_chat_notification_settings_object(
    const DialogNotificationSettings *notification_settings) {
  CHECK(notification_settings != nullptr);
  return td_api::make_object<td_api::chatNotificationSettings>(
      notification_settings->use_default_mute_until, max(0, notification_settings->mute_until - G()->unix_time()),
      notification_settings->use_default_sound, notification_settings->sound,
      notification_settings->use_default_show_preview, notification_settings->show_preview,
      notification_settings->use_default_disable_pinned_message_notifications,
      notification_settings->disable_pinned_message_notifications,
      notification_settings->use_default_disable_mention_notifications,
      notification_settings->disable_mention_notifications);
}

td_api::object_ptr<td_api::scopeNotificationSettings> get_scope_notification_settings_object(
    const ScopeNotificationSettings *notification_settings) {
  CHECK(notification_settings != nullptr);
  return td_api::make_object<td_api::scopeNotificationSettings>(
      max(0, notification_settings->mute_until - G()->unix_time()), notification_settings->sound,
      notification_settings->show_preview, notification_settings->disable_pinned_message_notifications,
      notification_settings->disable_mention_notifications);
}

telegram_api::object_ptr<telegram_api::InputNotifyPeer> get_input_notify_peer(NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return telegram_api::make_object<telegram_api::inputNotifyUsers>();
    case NotificationSettingsScope::Group:
      return telegram_api::make_object<telegram_api::inputNotifyChats>();
    case NotificationSettingsScope::Channel:
      return telegram_api::make_object<telegram_api::inputNotifyBroadcasts>();
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
    case td_api::notificationSettingsScopeChannelChats::ID:
      return NotificationSettingsScope::Channel;
    default:
      UNREACHABLE();
      return NotificationSettingsScope::Private;
  }
}

static int32 get_mute_until(int32 mute_for) {
  if (mute_for <= 0) {
    return 0;
  }

  const int32 MAX_PRECISE_MUTE_FOR = 7 * 86400;
  int32 current_time = G()->unix_time();
  if (mute_for > MAX_PRECISE_MUTE_FOR || mute_for >= std::numeric_limits<int32>::max() - current_time) {
    return std::numeric_limits<int32>::max();
  }
  return mute_for + current_time;
}

Result<DialogNotificationSettings> get_dialog_notification_settings(
    td_api::object_ptr<td_api::chatNotificationSettings> &&notification_settings, bool old_silent_send_message) {
  if (notification_settings == nullptr) {
    return Status::Error(400, "New notification settings must be non-empty");
  }
  if (!clean_input_string(notification_settings->sound_)) {
    return Status::Error(400, "Notification settings sound must be encoded in UTF-8");
  }
  if (notification_settings->sound_.empty()) {
    notification_settings->sound_ = "default";
  }

  int32 mute_until =
      notification_settings->use_default_mute_for_ ? 0 : get_mute_until(notification_settings->mute_for_);
  return DialogNotificationSettings(notification_settings->use_default_mute_for_, mute_until,
                                    notification_settings->use_default_sound_, std::move(notification_settings->sound_),
                                    notification_settings->use_default_show_preview_,
                                    notification_settings->show_preview_, old_silent_send_message,
                                    notification_settings->use_default_disable_pinned_message_notifications_,
                                    notification_settings->disable_pinned_message_notifications_,
                                    notification_settings->use_default_disable_mention_notifications_,
                                    notification_settings->disable_mention_notifications_);
}

Result<ScopeNotificationSettings> get_scope_notification_settings(
    td_api::object_ptr<td_api::scopeNotificationSettings> &&notification_settings) {
  if (notification_settings == nullptr) {
    return Status::Error(400, "New notification settings must be non-empty");
  }
  if (!clean_input_string(notification_settings->sound_)) {
    return Status::Error(400, "Notification settings sound must be encoded in UTF-8");
  }
  if (notification_settings->sound_.empty()) {
    notification_settings->sound_ = "default";
  }

  auto mute_until = get_mute_until(notification_settings->mute_for_);
  return ScopeNotificationSettings(mute_until, std::move(notification_settings->sound_),
                                   notification_settings->show_preview_,
                                   notification_settings->disable_pinned_message_notifications_,
                                   notification_settings->disable_mention_notifications_);
}

DialogNotificationSettings get_dialog_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings,
                                                            bool old_use_default_disable_pinned_message_notifications,
                                                            bool old_disable_pinned_message_notifications,
                                                            bool old_use_default_disable_mention_notifications,
                                                            bool old_disable_mention_notifications) {
  bool use_default_mute_until = (settings->flags_ & telegram_api::peerNotifySettings::MUTE_UNTIL_MASK) == 0;
  bool use_default_sound = (settings->flags_ & telegram_api::peerNotifySettings::SOUND_MASK) == 0;
  bool use_default_show_preview = (settings->flags_ & telegram_api::peerNotifySettings::SHOW_PREVIEWS_MASK) == 0;
  auto mute_until = use_default_mute_until || settings->mute_until_ <= G()->unix_time() ? 0 : settings->mute_until_;
  auto sound = std::move(settings->sound_);
  if (sound.empty()) {
    sound = "default";
  }
  bool silent_send_message =
      (settings->flags_ & telegram_api::peerNotifySettings::SILENT_MASK) == 0 ? false : settings->silent_;
  return {use_default_mute_until,
          mute_until,
          use_default_sound,
          std::move(sound),
          use_default_show_preview,
          settings->show_previews_,
          silent_send_message,
          old_use_default_disable_pinned_message_notifications,
          old_disable_pinned_message_notifications,
          old_use_default_disable_mention_notifications,
          old_disable_mention_notifications};
}

ScopeNotificationSettings get_scope_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings,
                                                          bool old_disable_pinned_message_notifications,
                                                          bool old_disable_mention_notifications) {
  auto mute_until = (settings->flags_ & telegram_api::peerNotifySettings::MUTE_UNTIL_MASK) == 0 ||
                            settings->mute_until_ <= G()->unix_time()
                        ? 0
                        : settings->mute_until_;
  auto sound = std::move(settings->sound_);
  if (sound.empty()) {
    sound = "default";
  }
  auto show_preview =
      (settings->flags_ & telegram_api::peerNotifySettings::SHOW_PREVIEWS_MASK) == 0 ? false : settings->show_previews_;
  return {mute_until, std::move(sound), show_preview, old_disable_pinned_message_notifications,
          old_disable_mention_notifications};
}

bool are_default_dialog_notification_settings(const DialogNotificationSettings &settings, bool compare_sound) {
  return settings.use_default_mute_until && (!compare_sound || settings.use_default_sound) &&
         settings.use_default_show_preview && settings.use_default_disable_pinned_message_notifications &&
         settings.use_default_disable_mention_notifications;
}

}  // namespace td
