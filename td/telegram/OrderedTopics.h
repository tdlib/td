//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

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

 private:
  std::set<TopicDate> ordered_topics_;

  TopicDate last_topic_date_ = min_topic_date();  // in memory

  int32 server_total_count_ = -1;
};

}  // namespace td
