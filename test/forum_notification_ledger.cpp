// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// LLM-assisted: GPT-5 (Codex; exact runtime version unavailable).
#include "td/telegram/ForumNotificationLedger.h"

#include "td/utils/tests.h"

#include <map>

namespace {

// Ordered durable-prefix double. Dropping the tail of writes models a crash;
// this does not simulate the filesystem or claim that set() implies fsync.
class LedgerStore final : public td::KeyValueSyncInterface {
 public:
  std::map<td::string, td::string> values;
  int writes_left = -1;
  int writes = 0;
  size_t largest_value = 0;

  SeqNo set(td::string key, td::string value) final {
    largest_value = td::max(largest_value, value.size());
    if (accept_write()) {
      values[std::move(key)] = std::move(value);
    }
    return writes;
  }
  bool isset(const td::string &key) final {
    return values.count(key) != 0;
  }
  td::string get(const td::string &key) final {
    auto it = values.find(key);
    return it == values.end() ? td::string() : it->second;
  }
  void for_each(std::function<void(td::Slice, td::Slice)> func) final {
    for (auto &entry : values) {
      func(entry.first, entry.second);
    }
  }
  std::unordered_map<td::string, td::string, td::Hash<td::string>> prefix_get(td::Slice prefix) final {
    std::unordered_map<td::string, td::string, td::Hash<td::string>> result;
    for (auto &entry : values) {
      if (td::begins_with(entry.first, prefix)) {
        result.emplace(entry.first.substr(prefix.size()), entry.second);
      }
    }
    return result;
  }
  td::FlatHashMap<td::string, td::string> get_all() final {
    td::FlatHashMap<td::string, td::string> result;
    for (auto &entry : values) {
      result.emplace(entry.first, entry.second);
    }
    return result;
  }
  SeqNo erase(const td::string &key) final {
    if (accept_write()) {
      values.erase(key);
    }
    return writes;
  }
  SeqNo erase_batch(td::vector<td::string> keys) final {
    for (auto &key : keys) {
      erase(key);
    }
    return writes;
  }
  void erase_by_prefix(td::Slice prefix) final {
    for (auto &entry : prefix_get(prefix)) {
      erase(prefix.str() + entry.first);
    }
  }
  void force_sync(td::Promise<> &&promise, const char *) final {
    promise.set_value(td::Unit());
  }
  void close(td::Promise<> promise) final {
    promise.set_value(td::Unit());
  }

 private:
  bool accept_write() {
    writes++;
    if (writes_left == 0) {
      return false;
    }
    if (writes_left > 0) {
      writes_left--;
    }
    return true;
  }
};

using Ledger = td::ForumNotificationLedger;

}  // namespace

TEST(ForumNotificationLedger, AdmissionNotChatUnread) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ASSERT_TRUE(ledger.admit(1, 100, 10));
  ASSERT_TRUE(ledger.admit(2, 200, 10));
  ASSERT_EQ(0, ledger.total_count());
  ASSERT_TRUE(ledger.commit(1));
  ASSERT_TRUE(ledger.commit(2));
  ASSERT_FALSE(ledger.commit(1));
  ASSERT_FALSE(ledger.admit(2, 200, 10));
  ASSERT_EQ(2, ledger.total_count());
  // Seven suppressed unread messages in another topic have no admission rows.
  Ledger cold(&store, 1);
  cold.init(-100, 9, 9, 900).ensure();
  ASSERT_EQ(2, cold.total_count());
  ASSERT_FALSE(cold.restore(3, 300, 20));
  ASSERT_EQ(2, cold.total_count());
}

TEST(ForumNotificationLedger, OnlyRetirementsInvalidateDelivery) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  auto revision = ledger.retirement_revision();
  ASSERT_TRUE(ledger.admit(1, 100, 10));
  ASSERT_TRUE(ledger.commit(1));
  ASSERT_TRUE(ledger.admit(2, 200, 10));
  ASSERT_TRUE(ledger.commit(2));
  ASSERT_EQ(revision, ledger.retirement_revision());
  ledger.retire_through(10, 100, 0);
  ASSERT_TRUE(ledger.retirement_revision() > revision);
  revision = ledger.retirement_revision();
  ledger.retire_through(10, 100, 0);
  ledger.retire(1);
  ASSERT_EQ(revision, ledger.retirement_revision());
  ledger.dismiss(2);
  ASSERT_TRUE(ledger.retirement_revision() > revision);
  revision = ledger.retirement_revision();
  ledger.dismiss(2);
  ASSERT_TRUE(ledger.admit(3, 300, 10));
  ASSERT_TRUE(ledger.commit(3));
  ASSERT_EQ(revision, ledger.retirement_revision());
}

TEST(ForumNotificationLedger, DismissalEvictionAndRead) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  for (int id = 1; id <= 80; id++) {
    ASSERT_TRUE(ledger.admit(id, id * 100, id % 2 + 1));
    ASSERT_TRUE(ledger.commit(id));
  }
  ledger.dismiss(1);
  ledger.dismiss_through(70);
  ASSERT_EQ(80, ledger.total_count());
  Ledger cold(&store, 1);
  cold.init(-100, 80, 0, 8000).ensure();
  ASSERT_EQ(80, cold.total_count());
  ASSERT_FALSE(cold.restore(1, 100, 2));
  ASSERT_FALSE(cold.restore(70, 7000, 1));
  ASSERT_TRUE(cold.restore(80, 8000, 1));
  cold.retire_through(1, 8000, 0);
  ASSERT_EQ(40, cold.total_count());
  cold.retire_through(1, 8000, 0);
  cold.retire(80);
  ASSERT_EQ(40, cold.total_count());
  cold.retire_message(7900);
  cold.retire_message(7900);
  ASSERT_EQ(39, cold.total_count());
}

TEST(ForumNotificationLedger, PendingRecoveryCancellationAndReplay) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ASSERT_TRUE(ledger.admit(1, 100, 10));
  ASSERT_TRUE(ledger.admit(2, 200, 10));
  ledger.dismiss(1);
  Ledger cold(&store, 1);
  cold.init(-100, 2, 2, 200).ensure();
  ASSERT_EQ(0, cold.total_count());
  ASSERT_FALSE(cold.restore(1, 100, 10));
  ASSERT_FALSE(cold.admit(1, 100, 10));
  ASSERT_FALSE(cold.restore(2, 200, 20));
  ASSERT_TRUE(cold.restore(2, 200, 10));
  ASSERT_TRUE(cold.restore(2, 200, 10));
  ASSERT_EQ(1, cold.total_count());
}

TEST(ForumNotificationLedger, MuteUnmuteDoesNotBackfill) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  for (int id = 1; id <= 3; id++) {
    ASSERT_TRUE(ledger.admit(id, id * 100, 10));
    ledger.commit(id);
  }
  ledger.retire_through(10, 0, 5);  // Includes allocated, not yet admitted IDs.
  ASSERT_EQ(0, ledger.total_count());
  ASSERT_FALSE(ledger.admit(4, 400, 10));
  ASSERT_FALSE(ledger.restore(3, 300, 10));
  ASSERT_FALSE(ledger.restore(5, 500, 10));
  ASSERT_TRUE(ledger.admit(6, 600, 10));
  ledger.commit(6);
  ASSERT_EQ(1, ledger.total_count());
}

TEST(ForumNotificationLedger, OpaqueLegacyIsNotReconstructedFromCache) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 100, 9, 9000).ensure();
  ASSERT_TRUE(ledger.has_legacy_count());
  ASSERT_TRUE(ledger.restore(80, 8000, 10));
  ledger.dismiss(80);
  ledger.retire_through(10, 8500, 0);
  ASSERT_EQ(9, ledger.total_count());
  ASSERT_FALSE(ledger.restore(80, 8000, 10));
  ASSERT_TRUE(ledger.admit(101, 10000, 20));
  ledger.commit(101);
  Ledger cold(&store, 1);
  cold.init(-100, 101, 500, 50000).ensure();
  ASSERT_EQ(10, cold.total_count());  // Reseeding from unread=500 is forbidden.
  cold.retire_through(0, 9000, 0);
  ASSERT_FALSE(cold.has_legacy_count());
  ASSERT_EQ(1, cold.total_count());
  ASSERT_FALSE(cold.restore(90, 9500, 30));
  ASSERT_TRUE(cold.restore(101, 10000, 20));
}

TEST(ForumNotificationLedger, InterruptedAdmissionHighWater) {
  for (int writes = 0; writes <= 3; writes++) {
    LedgerStore store;
    Ledger ledger(&store, 1);
    ledger.init(-100, 0, 0, 0, true).ensure();
    store.writes_left = writes;
    ledger.admit(1, 100, 10);
    store.writes_left = -1;
    Ledger cold(&store, 1);
    cold.init(-100, 1, 99, 100).ensure();
    ASSERT_EQ(0, cold.total_count());
    ASSERT_EQ(writes != 0, cold.restore(1, 100, 10));
    ASSERT_EQ(writes != 0 ? 1 : 0, cold.total_count());
    cold.retire(1);
    ASSERT_FALSE(cold.restore(1, 100, 10));
    if (writes != 0) {
      ASSERT_FALSE(cold.admit(1, 100, 10));
    }
  }
}

TEST(ForumNotificationLedger, InterruptedReadAndDismissal) {
  for (bool read : {false, true}) {
    for (int writes = 0; writes <= 5; writes++) {
      LedgerStore store;
      Ledger ledger(&store, 1);
      ledger.init(-100, 0, 0, 0, true).ensure();
      for (int id = 1; id <= 4; id++) {
        ledger.admit(id, id * 100, 10);
        ledger.commit(id);
      }
      store.writes_left = writes;
      if (read) {
        ledger.retire_through(10, 400, 0);
      } else {
        ledger.dismiss_through(4);
      }
      store.writes_left = -1;
      Ledger cold(&store, 1);
      cold.init(-100, 4, 4, 400).ensure();
      ASSERT_EQ(read && writes > 0 ? 0 : 4, cold.total_count());
      for (int id = 1; id <= 4; id++) {
        ASSERT_EQ(writes == 0, cold.restore(id, id * 100, 10));
      }
    }
  }
}

TEST(ForumNotificationLedger, IncarnationAndZeroSurviveRestart) {
  for (int writes = 1; writes <= 3; writes++) {
    LedgerStore store;
    Ledger ledger(&store, 1);
    ledger.init(-100, 0, 0, 0, true).ensure();
    ledger.admit(1, 100, 10);
    ledger.commit(1);
    auto epoch = ledger.epoch();
    store.writes_left = writes;
    Ledger reused(&store, 1);
    reused.init(-200, 1, 0, 0, true).ensure();
    store.writes_left = -1;
    Ledger cold(&store, 1);
    cold.init(-200, 1, 99, 100).ensure();
    ASSERT_EQ(epoch + 1, cold.epoch());
    ASSERT_EQ(0, cold.total_count());
    ASSERT_FALSE(cold.restore(1, 100, 10));
    ASSERT_TRUE(cold.admit(2, 200, 10));
    cold.commit(2);
    ASSERT_EQ(1, cold.total_count());
  }
}

TEST(ForumNotificationLedger, RejectMalformedPersistentData) {
  LedgerStore store;
  store.values["forum_notification_ledger#1"] = "invalid";
  auto before = store.values;
  Ledger ledger(&store, 1);
  ASSERT_TRUE(ledger.init(-100, 0, 0, 0).is_error());
  ASSERT_EQ(before.size(), store.values.size());
  ASSERT_EQ(before.begin()->second, store.values.begin()->second);
}

TEST(ForumNotificationLedger, InterruptedLegacyClear) {
  for (int writes = 0; writes <= 2; writes++) {
    LedgerStore store;
    Ledger ledger(&store, 1);
    ledger.init(-100, 100, 9, 9000).ensure();
    store.writes_left = writes;
    ledger.retire_through(0, 9000, 0);
    store.writes_left = -1;
    Ledger cold(&store, 1);
    cold.init(-100, 100, 100, 10000).ensure();
    ASSERT_EQ(writes == 0 ? 9 : 0, cold.total_count());
  }
}

TEST(ForumNotificationLedger, DismissalBeforeAdmission) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ledger.dismiss_through(5);
  ASSERT_FALSE(ledger.admit(4, 400, 10));
  ASSERT_EQ(0, ledger.total_count());
  ASSERT_TRUE(ledger.admit(6, 600, 10));
}

TEST(ForumNotificationLedger, SameObjectCannotBeReinitialized) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ledger.admit(1, 100, 10);
  ledger.commit(1);
  ASSERT_TRUE(ledger.init(-100, 1, 1, 100).is_error());
  ASSERT_EQ(1, ledger.total_count());
}

TEST(ForumNotificationLedger, SameMessageCannotBeReAdmittedWithNewId) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ledger.admit(1, 100, 10);
  ledger.commit(1);
  ledger.retire(1);
  Ledger cold(&store, 1);
  cold.init(-100, 1, 0, 100).ensure();
  ASSERT_FALSE(cold.admit(2, 100, 10));
  ASSERT_TRUE(cold.admit(2, 200, 10));
}

TEST(ForumNotificationLedger, InactiveEpochGarbageCollectionResumes) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ledger.admit(1, 100, 10);
  ledger.commit(1);
  store.writes_left = 1;
  Ledger reset(&store, 1);
  reset.init(-200, 1, 0, 0, true).ensure();
  store.writes_left = -1;
  Ledger cold(&store, 1);
  cold.init(-200, 1, 99, 100).ensure();
  ASSERT_TRUE(store.prefix_get("forum_notification_ledger#1/1/").empty());
  ASSERT_EQ(0, cold.total_count());
}

TEST(ForumNotificationLedger, LegacyDeletionAndCleanupSurviveRestart) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 100, 9, 9000).ensure();
  ledger.retire_message(8000);  // No cached ID, and SQL can still return an old row.
  Ledger cold(&store, 1);
  cold.init(-100, 100, 9, 9000).ensure();
  ASSERT_FALSE(cold.restore(80, 8000, 10));
  ASSERT_EQ(9, cold.total_count());  // Cannot attribute the opaque historical share.
  cold.clear_legacy_count();
  Ledger again(&store, 1);
  again.init(-100, 100, 99, 9000).ensure();
  ASSERT_EQ(0, again.total_count());
  ASSERT_TRUE(store.prefix_get("forum_notification_ledger#1/1/d/").empty());
}

TEST(ForumNotificationLedger, ModeChangeDoesNotRevivePreviousEpoch) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  ledger.admit(1, 100, 10);
  ledger.commit(1);
  ASSERT_TRUE(Ledger::invalidate(&store, 1, -200).is_error());
  Ledger::invalidate(&store, 1, -100).ensure();
  Ledger next(&store, 1);
  next.init(-100, 10, 3, 1000).ensure();
  ASSERT_EQ(ledger.epoch() + 1, next.epoch());
  ASSERT_EQ(3, next.total_count());
  ASSERT_TRUE(store.prefix_get("forum_notification_ledger#1/1/").empty());
  next.admit(11, 1100, 10);
  next.commit(11);
  ASSERT_EQ(4, next.total_count());
}

TEST(ForumNotificationLedger, BoundedRecordAndAdmissionWriteCost) {
  LedgerStore store;
  Ledger ledger(&store, 1);
  ledger.init(-100, 0, 0, 0, true).ensure();
  auto before = store.writes;
  for (int id = 1; id <= 10000; id++) {
    ASSERT_TRUE(ledger.admit(id, id * 100, 10));
    ledger.commit(id);
  }
  ASSERT_EQ(40000, store.writes - before);
  ASSERT_TRUE(store.largest_value <= 64);
  ASSERT_EQ(10000, ledger.total_count());
}
