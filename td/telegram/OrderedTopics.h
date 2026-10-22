//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/algorithm.h"
#include "td/utils/common.h"

#include <limits>
#include <set>

namespace td {

template <class TopicIdT>
class OrderedTopics {
 public:
  class TopicDate {
    int64 order_;
    TopicIdT topic_id_;

   public:
    TopicDate(int64 order, TopicIdT topic_id) : order_(order), topic_id_(topic_id) {
    }

    bool operator<(const TopicDate &other) const {
      return order_ > other.order_ ||
             (order_ == other.order_ && topic_id_.get_unique_id() > other.topic_id_.get_unique_id());
    }

    bool operator<=(const TopicDate &other) const {
      return order_ > other.order_ ||
             (order_ == other.order_ && topic_id_.get_unique_id() >= other.topic_id_.get_unique_id());
    }

    bool operator==(const TopicDate &other) const {
      return order_ == other.order_ && topic_id_ == other.topic_id_;
    }

    bool operator!=(const TopicDate &other) const {
      return !(*this == other);
    }

    TopicIdT get_topic_id() const {
      return topic_id_;
    }
  };

  static TopicDate min_topic_date() {
    return TopicDate(std::numeric_limits<int64>::max(), TopicIdT());
  }

  static TopicDate max_topic_date() {
    return TopicDate(0, TopicIdT());
  }

  int64 get_public_order(int64 private_order, TopicIdT topic_id) const {
    if (TopicDate(private_order, topic_id) <= last_topic_date_) {
      return private_order;
    }
    return 0;
  }

  void update_order(TopicIdT topic_id, int64 old_order, int64 new_order) {
    if (old_order == new_order) {
      return;
    }
    if (old_order != 0) {
      bool is_deleted = ordered_topics_.erase({old_order, topic_id}) > 0;
      CHECK(is_deleted);
      if (server_total_count_ > 0) {
        server_total_count_--;
      }
    }
    if (new_order != 0) {
      bool is_inserted = ordered_topics_.insert({new_order, topic_id}).second;
      CHECK(is_inserted);
      if (server_total_count_ >= 0) {
        server_total_count_++;
      }
    }
  }

  // returns topics, which public order has changed
  vector<TopicIdT> set_last_topic_date(TopicDate topic_date) {
    vector<TopicIdT> result;
    if (topic_date <= last_topic_date_) {
      return result;
    }
    auto min_topic_date = last_topic_date_;
    last_topic_date_ = topic_date;
    for (auto it = ordered_topics_.upper_bound(min_topic_date); it != ordered_topics_.end() && *it <= topic_date;
         ++it) {
      result.push_back(it->get_topic_id());
    }
    return result;
  }

  bool is_fully_loaded() const {
    return last_topic_date_ == max_topic_date();
  }

  int32 get_server_total_count() const {
    return server_total_count_;
  }

  void set_server_total_count(int32 server_total_count) {
    server_total_count_ = server_total_count;
  }

  // returns -1 if the total count is unknown
  int32 update_total_count() {
    if (server_total_count_ == -1) {
      return -1;
    }
    auto total_count = static_cast<int32>(ordered_topics_.size());
    if (!is_fully_loaded()) {
      return max(total_count, server_total_count_);
    }
    server_total_count_ = total_count;
    return total_count;
  }

  static constexpr int64 MIN_PINNED_TOPIC_ORDER = static_cast<int64>(2147000000) << 32;

  bool are_pinned_topics_inited() const {
    return are_pinned_topics_inited_;
  }

  void set_pinned_topics_inited() {
    are_pinned_topics_inited_ = true;
  }

  const vector<TopicIdT> &get_pinned_topic_ids() const {
    return pinned_topic_ids_;
  }

  // returns the new pinned order of the topic, or 0 if the topic is already the first pinned topic
  int64 pin_topic(TopicIdT topic_id) {
    if (!pinned_topic_ids_.empty() && pinned_topic_ids_[0] == topic_id) {
      return 0;
    }
    add_to_top(pinned_topic_ids_, pinned_topic_ids_.size() + 1, topic_id);
    return ++current_pinned_topic_order_;
  }

  bool unpin_topic(TopicIdT topic_id) {
    return td::remove(pinned_topic_ids_, topic_id);
  }

  // returns topics, which must be pinned in the returned order, and topics, which must be unpinned,
  // for the list of pinned topics to become topic_ids; topics already in the right relative order are left alone
  void get_pinned_topic_changes(vector<TopicIdT> topic_ids, vector<TopicIdT> &added_topic_ids,
                                vector<TopicIdT> &removed_topic_ids) const {
    removed_topic_ids = pinned_topic_ids_;
    auto old_topic_ids = pinned_topic_ids_;
    td::reverse(old_topic_ids);
    auto old_it = old_topic_ids.begin();
    for (auto topic_id : reversed(topic_ids)) {
      td::remove(removed_topic_ids, topic_id);
      while (old_it < old_topic_ids.end()) {
        if (*old_it == topic_id) {
          break;
        }
        ++old_it;
      }
      if (old_it < old_topic_ids.end()) {
        // leave the topic where it is
        ++old_it;
        continue;
      }
      added_topic_ids.push_back(topic_id);
    }
  }

 private:
  std::set<TopicDate> ordered_topics_;

  TopicDate last_topic_date_ = min_topic_date();  // in memory

  int32 server_total_count_ = -1;

  vector<TopicIdT> pinned_topic_ids_;
  bool are_pinned_topics_inited_ = false;
  int64 current_pinned_topic_order_ = MIN_PINNED_TOPIC_ORDER;
};

}  // namespace td
