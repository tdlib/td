// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
// LLM-assisted: GPT-5 (Codex; exact runtime version unavailable).
#include "td/telegram/ForumNotificationLedger.h"
#include "td/telegram/ForumTopicLocalNotificationSettings.h"

#include "td/db/BinlogKeyValue.h"

#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace {

using Local = td::ForumTopicLocalNotificationSettings;
using Value = Local::Value;
using Ledger = td::ForumNotificationLedger;

// Real synchronous binlog and on-disk replay. This does not emulate crashes or
// the ConcurrentBinlog actor/API acknowledgement scheduling.
class DiskStore {
 public:
  td::BinlogKeyValue<td::Binlog> store;

  explicit DiskStore(bool encrypted = false)
      : directory_(td::mkdtemp({}, "td-forum-persistence-").move_as_ok())
      , path_(directory_ + "/settings.binlog")
      , encrypted_(encrypted) {
    open();
  }

  ~DiskStore() {
    store.close();
    td::Binlog::destroy(path_).ensure();
    td::rmdir(directory_).ensure();
  }

  void reopen() {
    bool acknowledged = false;
    store.force_sync(td::PromiseCreator::lambda([&](td::Result<td::Unit> result) {
                       result.ensure();
                       acknowledged = true;
                     }),
                     "forum persistence test");
    ASSERT_TRUE(acknowledged);
    store.close();
    open();
  }

  Local local(td::int64 dialog = -100, td::int32 topic = 7) {
    return Local::load(store, dialog, topic).move_as_ok();
  }

 private:
  td::string directory_;
  td::string path_;
  bool encrypted_;

  void open() {
    // Synthetic test key, never an application/user database credential.
    auto key = encrypted_ ? td::DbKey::password("forum-persistence-test") : td::DbKey::empty();
    store.init(path_, std::move(key)).ensure();
  }
};

}  // namespace

TEST(ForumNotificationPersistence, LocalPreferencesReplay) {
  for (bool encrypted : {false, true}) {
    DiskStore disk(encrypted);
    for (auto pinned : {Value::Inherit, Value::Enabled, Value::Disabled}) {
      for (auto mentions : {Value::Inherit, Value::Enabled, Value::Disabled}) {
        Local{pinned, mentions}.save(disk.store, -100, 7);
        disk.reopen();
        auto settings = disk.local();
        ASSERT_TRUE(settings.pinned == pinned);
        ASSERT_TRUE(settings.mentions == mentions);
      }
    }
    Local().save(disk.store, -100, 7);
    disk.reopen();
    ASSERT_TRUE(disk.local().pinned == Value::Inherit);
    ASSERT_TRUE(disk.local().mentions == Value::Inherit);
    ASSERT_TRUE(disk.store.prefix_get("forum_topic_local_notify#").empty());
  }
}

TEST(ForumNotificationPersistence, LocalCleanupReplay) {
  DiskStore disk;
  for (auto dialog : {-100, -1000}) {
    for (auto topic : {7, 70}) {
      Local{Value::Enabled, Value::Disabled}.save(disk.store, dialog, topic);
    }
  }
  disk.store.set("unrelated", "preserved");
  disk.reopen();
  Local::erase(disk.store, -100, 7);
  disk.reopen();
  ASSERT_TRUE(disk.local().mentions == Value::Inherit);
  ASSERT_TRUE(disk.local(-100, 70).mentions == Value::Disabled);
  Local::erase_dialog(disk.store, -100);
  disk.reopen();
  ASSERT_TRUE(disk.local(-100, 70).mentions == Value::Inherit);
  ASSERT_TRUE(disk.local(-1000, 7).mentions == Value::Disabled);
  ASSERT_TRUE(disk.local(-1000, 70).mentions == Value::Disabled);
  Local::reset(disk.store);
  disk.reopen();
  ASSERT_TRUE(disk.local(-1000, 7).mentions == Value::Inherit);
  ASSERT_TRUE(disk.local(-1000, 70).mentions == Value::Inherit);
  ASSERT_EQ("preserved", disk.store.get("unrelated"));
  ASSERT_EQ(1u, disk.store.get_all().size());
}

TEST(ForumNotificationPersistence, MalformedLocalRecordReplay) {
  DiskStore disk;
  for (auto value : {"1", "10", "200", "1x1", "130", "103"}) {
    disk.store.set("forum_topic_local_notify#-100#7", value);
    disk.reopen();
    ASSERT_TRUE(Local::load(disk.store, -100, 7).is_error());
    ASSERT_EQ(value, disk.store.get("forum_topic_local_notify#-100#7"));
  }
  Local{Value::Disabled, Value::Enabled}.save(disk.store, -100, 7);
  disk.reopen();
  ASSERT_TRUE(disk.local().pinned == Value::Disabled);
  ASSERT_TRUE(disk.local().mentions == Value::Enabled);
}

TEST(ForumNotificationPersistence, LedgerReadAndDismissReplay) {
  DiskStore disk;
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 0, 0, 0, true).ensure();
    ASSERT_TRUE(ledger.admit(1, 100, 10));
    ASSERT_TRUE(ledger.admit(2, 200, 10));
    ASSERT_TRUE(ledger.admit(3, 300, 20));
    for (int id = 1; id <= 3; id++) {
      ASSERT_TRUE(ledger.commit(id));
    }
    ASSERT_EQ(3, ledger.total_count());
  }
  disk.reopen();
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 9, 9, 900).ensure();
    ASSERT_EQ(3, ledger.total_count());  // Extra unread was never admitted.
    ledger.retire_through(10, 200, 0);
    ledger.dismiss(3);
    ASSERT_EQ(1, ledger.total_count());
  }
  disk.reopen();
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 9, 9, 900).ensure();
    ASSERT_EQ(1, ledger.total_count());
    ASSERT_FALSE(ledger.restore(1, 100, 10));
    ASSERT_FALSE(ledger.restore(2, 200, 10));
    ASSERT_FALSE(ledger.restore(3, 300, 20));
    ledger.retire_message(300);
    ASSERT_EQ(0, ledger.total_count());
  }
  disk.reopen();
  Ledger ledger(&disk.store, 1);
  ledger.init(-100, 9, 9, 900).ensure();
  ASSERT_EQ(0, ledger.total_count());
  ASSERT_FALSE(ledger.admit(3, 300, 20));
  ASSERT_FALSE(ledger.restore(3, 300, 20));
}

TEST(ForumNotificationPersistence, PendingAdmissionReplay) {
  DiskStore disk;
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 0, 0, 0, true).ensure();
    ASSERT_TRUE(ledger.admit(1, 100, 10));
    ASSERT_TRUE(ledger.admit(2, 200, 10));
    ledger.dismiss(1);
  }
  disk.reopen();
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 2, 8, 800).ensure();
    ASSERT_EQ(0, ledger.total_count());
    ASSERT_FALSE(ledger.restore(1, 100, 10));
    ASSERT_FALSE(ledger.restore(2, 200, 20));
    ASSERT_TRUE(ledger.restore(2, 200, 10));
    ASSERT_TRUE(ledger.restore(2, 200, 10));
    ASSERT_EQ(1, ledger.total_count());
  }
  disk.reopen();
  Ledger ledger(&disk.store, 1);
  ledger.init(-100, 2, 8, 800).ensure();
  ASSERT_EQ(1, ledger.total_count());
  ASSERT_FALSE(ledger.admit(2, 200, 10));
}

TEST(ForumNotificationPersistence, LegacyAggregateReplay) {
  DiskStore disk;
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 100, 9, 9000).ensure();
    ASSERT_TRUE(ledger.restore(80, 8000, 10));
    ledger.dismiss(80);
    ledger.retire_through(10, 8500, 0);
    ASSERT_TRUE(ledger.admit(101, 10000, 20));
    ASSERT_TRUE(ledger.commit(101));
  }
  disk.reopen();
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 101, 500, 50000).ensure();
    ASSERT_EQ(10, ledger.total_count());
    ASSERT_FALSE(ledger.restore(80, 8000, 10));
    ledger.retire_through(0, 9000, 0);
    ASSERT_EQ(1, ledger.total_count());
  }
  disk.reopen();
  Ledger ledger(&disk.store, 1);
  ledger.init(-100, 101, 500, 50000).ensure();
  ASSERT_FALSE(ledger.has_legacy_count());
  ASSERT_EQ(1, ledger.total_count());
  ASSERT_TRUE(ledger.restore(101, 10000, 20));
  ASSERT_FALSE(ledger.restore(90, 9500, 30));
}

TEST(ForumNotificationPersistence, SeparateRecordNamespaces) {
  DiskStore disk;
  Local{Value::Enabled, Value::Disabled}.save(disk.store, -100, 7);
  {
    Ledger ledger(&disk.store, 1);
    ledger.init(-100, 0, 0, 0, true).ensure();
    ASSERT_TRUE(ledger.admit(1, 100, 7));
    ASSERT_TRUE(ledger.commit(1));
  }
  Local::reset(disk.store);
  disk.reopen();
  ASSERT_TRUE(disk.local().mentions == Value::Inherit);
  Ledger ledger(&disk.store, 1);
  ledger.init(-100, 1, 1, 100).ensure();
  ASSERT_EQ(1, ledger.total_count());
  ASSERT_TRUE(ledger.restore(1, 100, 7));
}
