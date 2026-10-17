//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/db/KeyValueSyncInterface.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"

#include <limits>
#include <map>
#include <set>

namespace td {

// LLM-assisted: GPT-5 (Codex; exact runtime version unavailable).
// Membership in a forum's ordinary message notification group, not chat unread.
// The owner must validate message policy before admission/restoration and sync
// the PMC before acknowledging a durable change. Temporary pushes never enter
// this ledger. A small record per member avoids an unbounded binlog event and
// rewriting the entire unread history on every arrival.
class ForumNotificationLedger {
 public:
  enum class State : int32 { Pending, Committed, Dismissed, Legacy, LegacyDismissed, LegacyRetired };

  struct Member {
    int32 id = 0;
    int64 message_id = 0;
    int32 topic_id = 0;
    State state = State::Pending;

    bool is_counted() const {
      return state == State::Committed || state == State::Dismissed;
    }

    template <class StorerT>
    void store(StorerT &storer) const {
      td::store(id, storer);
      td::store(message_id, storer);
      td::store(topic_id, storer);
      td::store(static_cast<int32>(state), storer);
    }

    template <class ParserT>
    void parse(ParserT &parser) {
      td::parse(id, parser);
      td::parse(message_id, parser);
      td::parse(topic_id, parser);
      int32 value;
      td::parse(value, parser);
      state = static_cast<State>(value);
      if (id <= 0 || message_id < 0 || topic_id < 0 || value < 0 || value > 5) {
        parser.set_error("Invalid forum notification member");
      }
    }
  };

  ForumNotificationLedger(KeyValueSyncInterface *store, int32 group_id)
      : store_(store), key_(PSTRING() << "forum_notification_ledger#" << group_id) {
    CHECK(store != nullptr && group_id > 0);
  }

  // An absent old ledger has unknown coverage: preserve its old aggregate once,
  // never reinterpret subsequent chat unread or current mute settings as history.
  // Fresh allocation is the only caller allowed to seed a known empty group.
  Status init(int64 owner, int32 cutoff, int32 legacy_count, int64 legacy_max_message_id, bool fresh = false) {
    if (initialized_) {
      return Status::Error("Forum notification ledger is already initialized");
    }
    initialized_ = true;
    CHECK(owner != 0 && cutoff >= 0 && legacy_count >= 0 && legacy_max_message_id >= 0);
    auto value = store_->get(key_);
    Meta old;
    if (!value.empty()) {
      TRY_STATUS(unserialize(old, value));
    }
    if (value.empty() || fresh || !old.active || old.owner != owner) {
      meta_.owner = owner;
      if (old.epoch == std::numeric_limits<int64>::max()) {
        return Status::Error("Forum notification epoch overflow");
      }
      meta_.epoch = old.epoch + 1;
      meta_.cutoff = meta_.high_water = cutoff;
      meta_.legacy_count = fresh ? 0 : legacy_count;
      meta_.legacy_max_message_id = legacy_max_message_id;
      save_meta();  // Publish the new incarnation before removing old records.
      if (!value.empty()) {
        store_->erase_by_prefix(PSTRING() << key_ << '/' << old.epoch << '/');
      }
    } else {
      meta_ = old;
    }
    prefix_ = PSTRING() << key_ << '/' << meta_.epoch << '/';
    // Resume interrupted cleanup of ANY inactive epoch, not only the previous one.
    for (auto &entry : store_->prefix_get(key_ + '/')) {
      auto key = key_ + '/' + entry.first;
      if (!begins_with(key, prefix_)) {
        store_->erase(key);
      }
    }
    for (auto &entry : store_->prefix_get(prefix_ + "r/")) {
      TRY_RESULT(topic, to_integer_safe<int32>(entry.first));
      if (topic < 0) {
        return Status::Error("Invalid forum notification frontier key");
      }
      Frontier frontier;
      TRY_STATUS(unserialize(frontier, entry.second));
      frontiers_.emplace(topic, frontier);
    }
    for (auto &entry : store_->prefix_get(prefix_ + "d/")) {
      TRY_RESULT(message_id, to_integer_safe<int64>(entry.first));
      if (message_id <= 0 || entry.second != "1") {
        return Status::Error("Invalid legacy notification retirement");
      }
      retired_legacy_messages_.insert(message_id);
    }
    for (auto &entry : store_->prefix_get(prefix_ + "n/")) {
      TRY_RESULT(id, to_integer_safe<int32>(entry.first));
      Member member;
      TRY_STATUS(unserialize(member, entry.second));
      if (id != member.id || (id > meta_.cutoff) != (member.state < State::Legacy)) {
        return Status::Error("Invalid forum notification membership");
      }
      meta_.high_water = max(meta_.high_water, id);
      members_.emplace(id, member);
      index_member(member);
      committed_count_ += member.is_counted();
    }
    // Admission writes the member first. Repair an interrupted high-water write
    // before deleting any rows, so a replay cannot re-admit a retired member.
    save_meta();
    for (auto &entry : members_) {
      if (entry.second.message_id > 0 && entry.second.topic_id > 0) {
        advance_admission_frontier(entry.second);
      }
    }
    auto shared = frontiers_.find(0);
    if (meta_.legacy_count == 0 || (shared != frontiers_.end() && covers_legacy(shared->second))) {
      clear_legacy_count();
    }
    vector<int32> retired;
    vector<int32> dismissed;
    for (auto &entry : members_) {
      if (is_retired(entry.second)) {
        retired.push_back(entry.first);
      } else if (entry.first <= meta_.dismissed_id) {
        dismissed.push_back(entry.first);
      }
    }
    for (auto id : retired) {
      retire(id);
    }
    for (auto id : dismissed) {
      dismiss(id);
    }
    return Status::OK();
  }

  int64 owner() const {
    return meta_.owner;
  }
  int64 epoch() const {
    return meta_.epoch;
  }
  uint64 retirement_revision() const {
    return retirement_revision_;
  }
  int32 total_count() const {
    return static_cast<int32>(min(static_cast<int64>(meta_.legacy_count) + committed_count_,
                                  static_cast<int64>(std::numeric_limits<int32>::max())));
  }
  bool has_legacy_count() const {
    return meta_.legacy_count != 0;
  }
  static Status invalidate(KeyValueSyncInterface *store, int32 group_id, int64 expected_owner = 0) {
    auto key = PSTRING() << "forum_notification_ledger#" << group_id;
    auto value = store->get(key);
    if (!value.empty()) {
      Meta meta;
      TRY_STATUS(unserialize(meta, value));
      if (expected_owner != 0 && meta.owner != expected_owner) {
        return Status::Error("Forum notification owner mismatch");
      }
      meta.active = false;
      store->set(key, serialize(meta));
    }
    return Status::OK();
  }
  const std::map<int32, Member> &members() const {
    return members_;
  }
  bool is_visible(int32 id) const {
    auto it = members_.find(id);
    return it != members_.end() && !is_retired(it->second) && id > meta_.dismissed_id &&
           (it->second.state == State::Pending || it->second.state == State::Committed ||
            (it->second.state == State::Legacy && meta_.legacy_count != 0));
  }

  bool admit(int32 id, int64 message_id, int32 topic_id) {
    auto frontier = frontiers_.find(topic_id);
    if (id <= meta_.high_water || id <= meta_.dismissed_id || message_id <= 0 || topic_id <= 0 ||
        (frontier != frontiers_.end() && message_id <= frontier->second.admitted_message_id)) {
      return false;
    }
    Member member{id, message_id, topic_id, State::Pending};
    if (is_retired(member)) {
      return false;
    }
    members_.emplace(id, member);
    index_member(member);
    save_member(member);
    advance_admission_frontier(member);
    meta_.high_water = id;
    save_meta();
    return true;
  }

  bool commit(int32 id) {
    auto it = members_.find(id);
    if (it == members_.end() || it->second.state != State::Pending) {
      return false;
    }
    it->second.state = State::Committed;
    committed_count_++;
    save_member(it->second);
    return true;
  }

  // Call only AFTER validating the SQL notification against current policy.
  // A persisted pending entry is committed only when its body is recoverable;
  // missing bodies do not invent visible notifications or pending queue entries.
  bool restore(int32 id, int64 message_id, int32 topic_id) {
    Member candidate{id, message_id, topic_id, State::Legacy};
    if (id <= 0 || message_id <= 0 || topic_id <= 0 || is_retired(candidate) || id <= meta_.dismissed_id ||
        (id <= meta_.cutoff && meta_.legacy_count == 0)) {
      return false;
    }
    auto it = members_.find(id);
    if (it == members_.end()) {
      if (id > meta_.cutoff || meta_.legacy_count == 0) {
        return false;  // SQL replay is not admission, including after retirement.
      }
      members_.emplace(id, candidate);
      index_member(candidate);
      save_member(candidate);  // Legacy adoption is count-neutral.
      advance_admission_frontier(candidate);
      return true;
    }
    auto &member = it->second;
    if (member.message_id != message_id || member.topic_id != topic_id || member.state == State::Dismissed ||
        member.state == State::LegacyDismissed || member.state == State::LegacyRetired) {
      return false;
    }
    commit(id);
    return true;
  }

  void dismiss(int32 id) {
    auto it = members_.find(id);
    if (it == members_.end()) {
      if (id > 0 && id <= meta_.cutoff && meta_.legacy_count != 0) {
        auto member = Member{id, 0, 0, State::LegacyDismissed};
        members_.emplace(id, member);
        save_member(member);
        retirement_revision_++;
      }
    } else if (it->second.state == State::Pending) {
      retire(id);  // It was never counted.
    } else if (it->second.state == State::Committed || it->second.state == State::Legacy) {
      it->second.state = id > meta_.cutoff ? State::Dismissed : State::LegacyDismissed;
      save_member(it->second);  // A user dismissal is not a read.
      retirement_revision_++;
    }
  }

  void retire(int32 id) {
    auto it = members_.find(id);
    if (it == members_.end()) {
      if (id > 0 && id <= meta_.cutoff && meta_.legacy_count != 0) {
        auto member = Member{id, 0, 0, State::LegacyRetired};
        members_.emplace(id, member);
        save_member(member);
        retirement_revision_++;
      }
      return;
    }
    if (id <= meta_.cutoff) {
      if (it->second.state == State::LegacyRetired) {
        return;
      }
      it->second.state = State::LegacyRetired;
      save_member(it->second);
      retirement_revision_++;
    } else {
      committed_count_ -= it->second.is_counted();
      store_->erase(member_key(id));
      retirement_revision_++;
      unindex_member(it->second);
      members_.erase(it);
    }
  }

  void dismiss_through(int32 id) {
    if (meta_.dismissed_id < id) {
      meta_.dismissed_id = id;
      save_meta();
      retirement_revision_++;
    }
    vector<int32> ids;
    for (auto &entry : members_) {
      if (entry.first <= id) {
        ids.push_back(entry.first);
      }
    }
    for (auto value : ids) {
      dismiss(value);
    }
  }

  // Topic zero is a shared read frontier. A mute retires only admissions up to
  // the captured ID, so unmute/expiry never manufactures historical arrivals.
  void retire_through(int32 topic_id, int64 message_id, int32 notification_id) {
    auto &frontier = frontiers_[topic_id];
    if (frontier.message_id >= message_id && frontier.notification_id >= notification_id) {
      return;
    }
    frontier.message_id = max(frontier.message_id, message_id);
    frontier.notification_id = max(frontier.notification_id, notification_id);
    save_frontier(topic_id, frontier);
    retirement_revision_++;
    if (topic_id == 0 && covers_legacy(frontier)) {
      clear_legacy_count();
    }
    vector<int32> ids;
    if (topic_id == 0) {
      for (auto &entry : members_) {
        if (is_retired(entry.second)) {
          ids.push_back(entry.first);
        }
      }
    } else {
      auto topic = topic_members_.find(topic_id);
      if (topic != topic_members_.end()) {
        for (auto id : topic->second) {
          if (is_retired(members_.at(id))) {
            ids.push_back(id);
          }
        }
      }
    }
    for (auto id : ids) {
      retire(id);
    }
  }

  void retire_message(int64 message_id) {
    if (message_id <= 0) {
      return;
    }
    if (meta_.legacy_count != 0 && message_id <= meta_.legacy_max_message_id &&
        retired_legacy_messages_.insert(message_id).second) {
      store_->set(PSTRING() << prefix_ << "d/" << message_id, "1");
      retirement_revision_++;
    }
    auto it = message_members_.find(message_id);
    vector<int32> ids =
        it == message_members_.end() ? vector<int32>() : vector<int32>(it->second.begin(), it->second.end());
    for (auto id : ids) {
      retire(id);
    }
  }

  void clear_legacy_count() {
    if (meta_.legacy_count != 0) {
      meta_.legacy_count = 0;
      save_meta();
      retirement_revision_++;
    }
    while (!members_.empty() && members_.begin()->first <= meta_.cutoff) {
      auto it = members_.begin();
      unindex_member(it->second);
      store_->erase(member_key(it->first));
      retirement_revision_++;
      members_.erase(it);
    }
    for (auto message_id : retired_legacy_messages_) {
      store_->erase(PSTRING() << prefix_ << "d/" << message_id);
      retirement_revision_++;
    }
    retired_legacy_messages_.clear();
  }

 private:
  struct Meta {
    int32 version = 1;
    int64 owner = 0;
    int64 epoch = 0;
    int32 cutoff = 0;
    int32 high_water = 0;
    int32 legacy_count = 0;
    int64 legacy_max_message_id = 0;
    int32 dismissed_id = 0;
    bool active = true;

    template <class StorerT>
    void store(StorerT &storer) const {
      td::store(version, storer);
      td::store(owner, storer);
      td::store(epoch, storer);
      td::store(cutoff, storer);
      td::store(high_water, storer);
      td::store(legacy_count, storer);
      td::store(legacy_max_message_id, storer);
      td::store(dismissed_id, storer);
      td::store(active, storer);
    }
    template <class ParserT>
    void parse(ParserT &parser) {
      td::parse(version, parser);
      td::parse(owner, parser);
      td::parse(epoch, parser);
      td::parse(cutoff, parser);
      td::parse(high_water, parser);
      td::parse(legacy_count, parser);
      td::parse(legacy_max_message_id, parser);
      td::parse(dismissed_id, parser);
      td::parse(active, parser);
      if (version != 1 || owner == 0 || epoch <= 0 || cutoff < 0 || high_water < cutoff || legacy_count < 0 ||
          legacy_max_message_id < 0 || dismissed_id < 0) {
        parser.set_error("Invalid forum notification ledger");
      }
    }
  };

  struct Frontier {
    int64 message_id = 0;
    int32 notification_id = 0;
    int64 admitted_message_id = 0;
    template <class StorerT>
    void store(StorerT &storer) const {
      td::store(message_id, storer);
      td::store(notification_id, storer);
      td::store(admitted_message_id, storer);
    }
    template <class ParserT>
    void parse(ParserT &parser) {
      td::parse(message_id, parser);
      td::parse(notification_id, parser);
      td::parse(admitted_message_id, parser);
      if (message_id < 0 || notification_id < 0 || admitted_message_id < 0) {
        parser.set_error("Invalid forum notification frontier");
      }
    }
  };

  bool covers_legacy(const Frontier &frontier) const {
    return (frontier.notification_id > 0 && frontier.notification_id >= meta_.cutoff) ||
           (meta_.legacy_max_message_id > 0 && frontier.message_id >= meta_.legacy_max_message_id);
  }
  void save_frontier(int32 topic_id, const Frontier &frontier) {
    store_->set(PSTRING() << prefix_ << "r/" << topic_id, serialize(frontier));
  }
  void advance_admission_frontier(const Member &member) {
    auto &frontier = frontiers_[member.topic_id];
    if (frontier.admitted_message_id < member.message_id) {
      frontier.admitted_message_id = member.message_id;
      save_frontier(member.topic_id, frontier);
    }
  }
  void index_member(const Member &member) {
    topic_members_[member.topic_id].insert(member.id);
    message_members_[member.message_id].insert(member.id);
  }
  void unindex_member(const Member &member) {
    auto topic = topic_members_.find(member.topic_id);
    if (topic != topic_members_.end()) {
      topic->second.erase(member.id);
      if (topic->second.empty()) {
        topic_members_.erase(topic);
      }
    }
    auto message = message_members_.find(member.message_id);
    if (message != message_members_.end()) {
      message->second.erase(member.id);
      if (message->second.empty()) {
        message_members_.erase(message);
      }
    }
  }

  bool is_retired(const Member &member) const {
    if (retired_legacy_messages_.count(member.message_id) != 0) {
      return true;
    }
    for (auto topic : {0, member.topic_id}) {
      auto it = frontiers_.find(topic);
      if (it != frontiers_.end() && (member.id <= it->second.notification_id ||
                                     (member.message_id > 0 && member.message_id <= it->second.message_id))) {
        return true;
      }
    }
    return false;
  }
  string member_key(int32 id) const {
    return PSTRING() << prefix_ << "n/" << id;
  }
  void save_member(const Member &member) {
    store_->set(member_key(member.id), serialize(member));
  }
  void save_meta() {
    store_->set(key_, serialize(meta_));
  }

  KeyValueSyncInterface *store_;
  string key_;
  string prefix_;
  Meta meta_;
  std::map<int32, Member> members_;
  std::map<int32, Frontier> frontiers_;
  std::map<int32, std::set<int32>> topic_members_;
  std::map<int64, std::set<int32>> message_members_;
  std::set<int64> retired_legacy_messages_;
  int64 committed_count_ = 0;
  uint64 retirement_revision_ = 0;
  bool initialized_ = false;
};

}  // namespace td
