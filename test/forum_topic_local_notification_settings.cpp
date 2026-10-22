// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
// LLM-assisted: GPT-5 (Codex; exact runtime version unavailable).
#include "td/telegram/ForumTopicLocalNotificationSettings.h"

#include "td/utils/misc.h"
#include "td/utils/tests.h"

#include <map>

namespace {

// Explicit memory/durable-prefix double, not filesystem or SQLite emulation.
class LocalSettingsStore final : public td::KeyValueSyncInterface {
 public:
  std::map<td::string, td::string> values;
  std::map<td::string, td::string> durable;
  bool fail_sync = false;
  bool hold_sync = false;
  td::vector<td::Promise<>> pending;

  SeqNo set(td::string key, td::string value) final {
    values[std::move(key)] = std::move(value);
    return 0;
  }
  bool isset(const td::string &key) final {
    return values.count(key) != 0;
  }
  td::string get(const td::string &key) final {
    auto it = values.find(key);
    return it == values.end() ? td::string() : it->second;
  }
  void for_each(std::function<void(td::Slice, td::Slice)> action) final {
    for (auto &entry : values)
      action(entry.first, entry.second);
  }
  std::unordered_map<td::string, td::string, td::Hash<td::string>> prefix_get(td::Slice prefix) final {
    std::unordered_map<td::string, td::string, td::Hash<td::string>> result;
    for (auto &entry : values) {
      if (td::begins_with(entry.first, prefix))
        result.emplace(entry.first.substr(prefix.size()), entry.second);
    }
    return result;
  }
  td::FlatHashMap<td::string, td::string> get_all() final {
    td::FlatHashMap<td::string, td::string> result;
    for (auto &entry : values)
      result.emplace(entry.first, entry.second);
    return result;
  }
  SeqNo erase(const td::string &key) final {
    values.erase(key);
    return 0;
  }
  SeqNo erase_batch(td::vector<td::string> keys) final {
    for (auto &key : keys)
      erase(key);
    return 0;
  }
  void erase_by_prefix(td::Slice prefix) final {
    for (auto &entry : prefix_get(prefix))
      erase(prefix.str() + entry.first);
  }
  void force_sync(td::Promise<> &&promise, const char *) final {
    if (hold_sync) {
      pending.push_back(std::move(promise));
      return;
    }
    if (fail_sync) {
      promise.set_error(td::Status::Error("test sync failure"));
      return;
    }
    durable = values;
    promise.set_value(td::Unit());
  }
  void close(td::Promise<> promise) final {
    force_sync(std::move(promise), "test close");
  }
  void crash() {
    values = durable;
  }
  void complete_sync() {
    hold_sync = false;
    auto promises = std::move(pending);
    for (auto &promise : promises) {
      force_sync(std::move(promise), "test completion");
    }
  }
};

// Only the four local fields are production template inputs. Server-owned
// sentinels ensure applying a local record cannot synthesize a known policy.
struct Settings {
  bool use_default_disable_pinned_message_notifications = true;
  bool disable_pinned_message_notifications = false;
  bool use_default_disable_mention_notifications = true;
  bool disable_mention_notifications = false;
  int mute_until = 123;
  bool show_preview = false;
  bool is_synchronized = false;
  int read_watermark = 91;
};
using Local = td::ForumTopicLocalNotificationSettings;
using Value = Local::Value;
const Value states[] = {Value::Inherit, Value::Enabled, Value::Disabled};

Local load(LocalSettingsStore &store, td::int64 dialog = -100, td::int32 topic = 7) {
  auto result = Local::load(store, dialog, topic);
  ASSERT_TRUE(result.is_ok());
  return result.move_as_ok();
}

}  // namespace

TEST(ForumTopicLocalNotifications, TriStateRoundTrip) {
  for (auto pinned : states)
    for (auto mentions : states) {
      LocalSettingsStore store;
      Local original{pinned, mentions};
      Settings settings;
      original.apply(settings);
      Local::from(settings).save(store, -100, 7);
      auto loaded = load(store);
      ASSERT_TRUE(loaded.pinned == pinned);
      ASSERT_TRUE(loaded.mentions == mentions);
      ASSERT_EQ(store.values.size(), pinned == Value::Inherit && mentions == Value::Inherit ? 0u : 1u);
      ASSERT_EQ(settings.mute_until, 123);
      ASSERT_TRUE(!settings.show_preview && !settings.is_synchronized);
      ASSERT_EQ(settings.read_watermark, 91);
      for (auto &entry : store.values)
        ASSERT_EQ(entry.second.size(), 3u);
    }
}

TEST(ForumTopicLocalNotifications, MissingRecordIsInheritance) {
  LocalSettingsStore store;
  Settings old_topic;
  Local{Value::Disabled, Value::Enabled}.apply(old_topic);
  load(store).apply(old_topic);
  ASSERT_TRUE(old_topic.use_default_disable_pinned_message_notifications);
  ASSERT_TRUE(old_topic.use_default_disable_mention_notifications);
  ASSERT_TRUE(!old_topic.disable_pinned_message_notifications && !old_topic.disable_mention_notifications);
  ASSERT_TRUE(!old_topic.is_synchronized);
}

TEST(ForumTopicLocalNotifications, DurableRestartWithoutTopicDatabase) {
  for (auto pinned : states)
    for (auto mentions : states) {
      LocalSettingsStore before;
      Local{pinned, mentions}.save(before, -100, 7);
      bool acknowledged = false;
      before.force_sync(td::PromiseCreator::lambda([&](td::Result<td::Unit> result) {
                          ASSERT_TRUE(result.is_ok());
                          acknowledged = true;
                        }),
                        "test");
      ASSERT_TRUE(acknowledged);
      LocalSettingsStore restarted;
      restarted.values = before.durable;
      auto loaded = load(restarted);
      ASSERT_TRUE(loaded.pinned == pinned && loaded.mentions == mentions);
    }
}

TEST(ForumTopicLocalNotifications, SyncFailureDoesNotAcknowledge) {
  LocalSettingsStore store;
  Local{Value::Disabled, Value::Disabled}.save(store, -100, 7);
  store.fail_sync = true;
  bool failed = false;
  store.force_sync(td::PromiseCreator::lambda([&](td::Result<td::Unit> result) { failed = result.is_error(); }),
                   "test");
  ASSERT_TRUE(failed);
  store.crash();
  ASSERT_TRUE(load(store).mentions == Value::Inherit);
}

TEST(ForumTopicLocalNotifications, IsolatedTopicAndDialogCleanup) {
  LocalSettingsStore store;
  for (auto dialog : {-100, -1000})
    for (auto topic : {7, 70}) {
      Local{Value::Enabled, Value::Disabled}.save(store, dialog, topic);
    }
  store.set("unrelated", "untouched");
  Local::erase(store, -100, 7);
  ASSERT_TRUE(load(store).mentions == Value::Inherit);
  ASSERT_TRUE(load(store, -100, 70).mentions == Value::Disabled);
  Local::erase_dialog(store, -100);
  ASSERT_TRUE(load(store, -100, 70).mentions == Value::Inherit);
  ASSERT_TRUE(load(store, -1000, 7).mentions == Value::Disabled);
  ASSERT_TRUE(load(store, -1000, 70).mentions == Value::Disabled);
  Local::reset(store);
  ASSERT_EQ(store.values.size(), 1u);
  ASSERT_EQ(store.get("unrelated"), "untouched");
}

TEST(ForumTopicLocalNotifications, DefaultErasesOnlyItsRecord) {
  LocalSettingsStore store;
  Local{Value::Disabled, Value::Enabled}.save(store, -100, 7);
  Local{Value::Enabled, Value::Disabled}.save(store, -100, 70);
  Local().save(store, -100, 7);
  ASSERT_TRUE(load(store).pinned == Value::Inherit);
  ASSERT_TRUE(load(store, -100, 70).pinned == Value::Enabled);
}

TEST(ForumTopicLocalNotifications, RejectMalformedOrFutureVersion) {
  for (auto data : {"1", "10", "1000", "200", "130", "103", "1x1", "-11"}) {
    LocalSettingsStore store;
    store.set("forum_topic_local_notify#-100#7", data);
    ASSERT_TRUE(Local::load(store, -100, 7).is_error());
    ASSERT_EQ(store.get("forum_topic_local_notify#-100#7"), data);
  }
}

TEST(ForumTopicLocalNotifications, InheritedInactiveValuesAreCanonical) {
  Settings settings;
  settings.disable_pinned_message_notifications = true;
  settings.disable_mention_notifications = true;
  LocalSettingsStore store;
  Local::from(settings).save(store, -100, 7);
  ASSERT_TRUE(store.values.empty());
  load(store).apply(settings);
  ASSERT_TRUE(!settings.disable_pinned_message_notifications && !settings.disable_mention_notifications);
}

TEST(ForumTopicLocalNotifications, DeferredSyncBoundary) {
  LocalSettingsStore store;
  store.hold_sync = true;
  Local{Value::Enabled, Value::Disabled}.save(store, -100, 7);
  bool acknowledged = false;
  store.force_sync(td::PromiseCreator::lambda([&](td::Result<td::Unit> result) {
                     ASSERT_TRUE(result.is_ok());
                     acknowledged = true;
                   }),
                   "test");
  ASSERT_TRUE(!acknowledged && store.durable.empty());
  store.complete_sync();
  ASSERT_TRUE(acknowledged);
  store.crash();
  ASSERT_TRUE(load(store).mentions == Value::Disabled);
}

TEST(ForumTopicLocalNotifications, RetryAfterSyncFailure) {
  LocalSettingsStore store;
  Local{Value::Inherit, Value::Enabled}.save(store, -100, 7);
  store.fail_sync = true;
  bool failed = false;
  store.force_sync(td::PromiseCreator::lambda([&](td::Result<td::Unit> result) { failed = result.is_error(); }),
                   "test");
  ASSERT_TRUE(failed);
  // A failed sync is not an in-memory rollback. A later setter may already differ.
  ASSERT_TRUE(load(store).mentions == Value::Enabled);
  Local{Value::Disabled, Value::Disabled}.save(store, -100, 7);
  store.fail_sync = false;
  // Repeating an unchanged setter still needs the pending write to be synchronized.
  Local{Value::Disabled, Value::Disabled}.save(store, -100, 7);
  store.force_sync(td::PromiseCreator::lambda([](td::Result<td::Unit> result) { ASSERT_TRUE(result.is_ok()); }),
                   "test");
  store.crash();
  ASSERT_TRUE(load(store).pinned == Value::Disabled && load(store).mentions == Value::Disabled);
}

TEST(ForumTopicLocalNotifications, LateServerPolicyAfterReset) {
  LocalSettingsStore store;
  Settings old_topic;
  Local{Value::Disabled, Value::Disabled}.apply(old_topic);
  Local::from(old_topic).save(store, -100, 7);
  Local::reset(store);
  // The server converter can carry old local flags from an earlier cached topic.
  // Every hydration must overlay the current record, including an absent one.
  load(store).apply(old_topic);
  ASSERT_TRUE(old_topic.use_default_disable_pinned_message_notifications);
  ASSERT_TRUE(old_topic.use_default_disable_mention_notifications);
  ASSERT_EQ(old_topic.mute_until, 123);
  ASSERT_EQ(old_topic.read_watermark, 91);
  ASSERT_TRUE(!old_topic.show_preview && !old_topic.is_synchronized);
}

TEST(ForumTopicLocalNotifications, DurableDeletionAndReset) {
  LocalSettingsStore store;
  Local{Value::Disabled, Value::Enabled}.save(store, -100, 7);
  Local{Value::Enabled, Value::Disabled}.save(store, -1000, 7);
  store.force_sync(td::Promise<>(), "test initial");
  store.hold_sync = true;
  Local::erase_dialog(store, -100);
  bool deleted = false;
  store.force_sync(td::PromiseCreator::lambda([&](td::Result<td::Unit> result) {
                     ASSERT_TRUE(result.is_ok());
                     deleted = true;
                   }),
                   "test delete");
  ASSERT_TRUE(!deleted);
  store.complete_sync();
  ASSERT_TRUE(deleted);
  store.crash();
  ASSERT_TRUE(load(store).mentions == Value::Inherit);
  ASSERT_TRUE(load(store, -1000, 7).mentions == Value::Disabled);
  Local::reset(store);
  store.force_sync(td::Promise<>(), "test reset");
  store.crash();
  ASSERT_TRUE(load(store, -1000, 7).mentions == Value::Inherit);
}

TEST(ForumTopicLocalNotifications, CacheReloadKeepsRecord) {
  LocalSettingsStore store;
  Local{Value::Disabled, Value::Enabled}.save(store, -100, 7);
  for (int reload = 0; reload < 3; reload++) {
    Settings full_topic;
    load(store).apply(full_topic);
    ASSERT_TRUE(full_topic.disable_pinned_message_notifications);
    ASSERT_TRUE(!full_topic.use_default_disable_mention_notifications);
    ASSERT_TRUE(!full_topic.disable_mention_notifications);
    ASSERT_TRUE(!full_topic.is_synchronized);
  }
}
