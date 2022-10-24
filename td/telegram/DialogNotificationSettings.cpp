//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogNotificationSettings.h"

#include "td/telegram/Global.h"

#include <limits>

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, const DialogNotificationSettings &notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.silent_send_message
                        << ", " << notification_settings.disable_pinned_message_notifications << ", "
                        << notification_settings.disable_mention_notifications << ", "
                        << notification_settings.use_default_mute_until << ", "
                        << notification_settings.use_default_show_preview << ", "
                        << notification_settings.use_default_disable_pinned_message_notifications << ", "
                        << notification_settings.use_default_disable_mention_notifications << ", "
                        << notification_settings.is_synchronized << "]";
}

td_api::object_ptr<td_api::chatNotificationSettings> get_chat_notification_settings_object(
    const DialogNotificationSettings *notification_settings) {
  CHECK(notification_settings != nullptr);
  return td_api::make_object<td_api::chatNotificationSettings>(
      notification_settings->use_default_mute_until, max(0, notification_settings->mute_until - G()->unix_time()),
      is_notification_sound_default(notification_settings->sound),
      get_notification_sound_ringtone_id(notification_settings->sound), notification_settings->use_default_show_preview,
      notification_settings->show_preview, notification_settings->use_default_disable_pinned_message_notifications,
      notification_settings->disable_pinned_message_notifications,
      notification_settings->use_default_disable_mention_notifications,
      notification_settings->disable_mention_notifications);
}

static int32 get_mute_until(int32 mute_for) {
  if (mute_for <= 0) {
    return 0;
  }

  const int32 MAX_PRECISE_MUTE_FOR = 366 * 86400;
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

  int32 mute_until =
      notification_settings->use_default_mute_for_ ? 0 : get_mute_until(notification_settings->mute_for_);
  return DialogNotificationSettings(
      notification_settings->use_default_mute_for_, mute_until,
      get_notification_sound(notification_settings->use_default_sound_, notification_settings->sound_id_),
      notification_settings->use_default_show_preview_, notification_settings->show_preview_, old_silent_send_message,
      notification_settings->use_default_disable_pinned_message_notifications_,
      notification_settings->disable_pinned_message_notifications_,
      notification_settings->use_default_disable_mention_notifications_,
      notification_settings->disable_mention_notifications_);
}

DialogNotificationSettings get_dialog_notification_settings(tl_object_ptr<telegram_api::peerNotifySettings> &&settings,
                                                            bool old_use_default_disable_pinned_message_notifications,
                                                            bool old_disable_pinned_message_notifications,
                                                            bool old_use_default_disable_mention_notifications,
                                                            bool old_disable_mention_notifications) {
  if (settings == nullptr) {
    return DialogNotificationSettings();
  }
  bool use_default_mute_until = (settings->flags_ & telegram_api::peerNotifySettings::MUTE_UNTIL_MASK) == 0;
  bool use_default_show_preview = (settings->flags_ & telegram_api::peerNotifySettings::SHOW_PREVIEWS_MASK) == 0;
  auto mute_until = use_default_mute_until || settings->mute_until_ <= G()->unix_time() ? 0 : settings->mute_until_;
  bool silent_send_message =
      (settings->flags_ & telegram_api::peerNotifySettings::SILENT_MASK) == 0 ? false : settings->silent_;
  return {use_default_mute_until,
          mute_until,
          get_notification_sound(settings.get()),
          use_default_show_preview,
          settings->show_previews_,
          silent_send_message,
          old_use_default_disable_pinned_message_notifications,
          old_disable_pinned_message_notifications,
          old_use_default_disable_mention_notifications,
          old_disable_mention_notifications};
}

bool are_default_dialog_notification_settings(const DialogNotificationSettings &settings, bool compare_sound) {
  return settings.use_default_mute_until && (!compare_sound || is_notification_sound_default(settings.sound)) &&
         settings.use_default_show_preview && settings.use_default_disable_pinned_message_notifications &&
         settings.use_default_disable_mention_notifications;
}

}  // namespace td
