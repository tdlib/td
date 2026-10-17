//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/db/KeyValueSyncInterface.h"

#include "td/utils/common.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

namespace td {

// LLM-assisted: GPT-5 (Codex; exact runtime version unavailable).
// These two preferences aren't sent to Telegram. Keep them independently of the
// optional message-thread database. This record must never establish that the
// server policy (mute, preview, sound) is known or synchronized.
class ForumTopicLocalNotificationSettings {
 public:
  enum class Value : char { Inherit = '0', Enabled = '1', Disabled = '2' };

  Value pinned = Value::Inherit;
  Value mentions = Value::Inherit;

  template <class SettingsT>
  static ForumTopicLocalNotificationSettings from(const SettingsT &settings) {
    return {
        value(settings.use_default_disable_pinned_message_notifications, settings.disable_pinned_message_notifications),
        value(settings.use_default_disable_mention_notifications, settings.disable_mention_notifications)};
  }

  template <class SettingsT>
  void apply(SettingsT &settings) const {
    settings.use_default_disable_pinned_message_notifications = pinned == Value::Inherit;
    settings.disable_pinned_message_notifications = pinned == Value::Disabled;
    settings.use_default_disable_mention_notifications = mentions == Value::Inherit;
    settings.disable_mention_notifications = mentions == Value::Disabled;
  }

  static Result<ForumTopicLocalNotificationSettings> load(KeyValueSyncInterface &store, int64 dialog_id,
                                                          int32 topic_id) {
    auto data = store.get(key(dialog_id, topic_id));
    if (data.empty()) {
      return ForumTopicLocalNotificationSettings();
    }
    if (data.size() != 3 || data[0] != '1' || data[1] < '0' || data[1] > '2' || data[2] < '0' || data[2] > '2') {
      return Status::Error("Invalid local forum notification settings");
    }
    return ForumTopicLocalNotificationSettings{static_cast<Value>(data[1]), static_cast<Value>(data[2])};
  }

  void save(KeyValueSyncInterface &store, int64 dialog_id, int32 topic_id) const {
    auto record_key = key(dialog_id, topic_id);
    if (pinned == Value::Inherit && mentions == Value::Inherit) {
      store.erase(record_key);
    } else {
      string data = "100";
      data[1] = static_cast<char>(pinned);
      data[2] = static_cast<char>(mentions);
      store.set(std::move(record_key), std::move(data));
    }
    // The request owner synchronizes the binlog before returning API success.
  }

  static void erase(KeyValueSyncInterface &store, int64 dialog_id, int32 topic_id) {
    store.erase(key(dialog_id, topic_id));
  }

  static void erase_dialog(KeyValueSyncInterface &store, int64 dialog_id) {
    store.erase_by_prefix(dialog_prefix(dialog_id));
  }

  static void reset(KeyValueSyncInterface &store) {
    store.erase_by_prefix("forum_topic_local_notify#");
  }

 private:
  static Value value(bool inherit, bool disabled) {
    return inherit ? Value::Inherit : disabled ? Value::Disabled : Value::Enabled;
  }

  static string dialog_prefix(int64 dialog_id) {
    return PSTRING() << "forum_topic_local_notify#" << dialog_id << '#';
  }

  static string key(int64 dialog_id, int32 topic_id) {
    return PSTRING() << dialog_prefix(dialog_id) << topic_id;
  }
};

}  // namespace td
