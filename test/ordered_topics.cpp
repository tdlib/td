//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/OrderedTopics.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include <algorithm>
#include <limits>

namespace {

class TestTopicId {
  td::int64 id_ = 0;

 public:
  TestTopicId() = default;

  explicit TestTopicId(td::int64 id) : id_(id) {
  }

  td::int64 get_unique_id() const {
    return id_;
  }

  bool operator==(const TestTopicId &other) const {
    return id_ == other.id_;
  }
};

// the list as it is documented: topics sorted by order, then by identifier, both descending;
// a topic is visible if it isn't after the last loaded one
class NaiveOrderedTopics {
 public:
  void update_order(td::int64 topic_id, td::int64 new_order) {
    if (new_order == 0) {
      orders_.erase(topic_id);
    } else {
      orders_[topic_id] = new_order;
    }
  }

  void set_last_topic_date(td::int64 order, td::int64 topic_id) {
    if (is_after(last_order_, last_topic_id_, order, topic_id)) {
      last_order_ = order;
      last_topic_id_ = topic_id;
    }
  }

  td::int64 get_public_order(td::int64 order, td::int64 topic_id) const {
    return order != 0 && !is_after(last_order_, last_topic_id_, order, topic_id) ? order : 0;
  }

  std::size_t size() const {
    return orders_.size();
  }

 private:
  // returns true, if the topic (order, topic_id) is after the topic (last_order, last_topic_id) in the list
  static bool is_after(td::int64 last_order, td::int64 last_topic_id, td::int64 order, td::int64 topic_id) {
    return order < last_order || (order == last_order && topic_id < last_topic_id);
  }

  td::FlatHashMap<td::int64, td::int64> orders_;
  td::int64 last_order_ = std::numeric_limits<td::int64>::max();
  td::int64 last_topic_id_ = 0;
};

}  // namespace

TEST(OrderedTopics, stress) {
  td::Random::Xorshift128plus rnd(123);
  for (int test = 0; test < 100; test++) {
    td::OrderedTopics<TestTopicId> topics;
    NaiveOrderedTopics naive;
    td::FlatHashMap<td::int64, td::int64> orders;
    auto max_topic_id = rnd.fast(1, 50);
    auto max_order = rnd.fast(1, 1000);

    auto check = [&] {
      for (td::int64 topic_id = 1; topic_id <= max_topic_id; topic_id++) {
        auto it = orders.find(topic_id);
        auto order = it == orders.end() ? 0 : it->second;
        ASSERT_EQ(naive.get_public_order(order, topic_id), topics.get_public_order(order, TestTopicId(topic_id)));
      }
    };

    for (int step = 0; step < 1000; step++) {
      td::int64 topic_id = rnd.fast(1, static_cast<int>(max_topic_id));
      if (rnd.fast(0, 9) == 0) {
        td::int64 order = rnd.fast(0, static_cast<int>(max_order));
        auto changed_topic_ids = topics.set_last_topic_date({order, TestTopicId(topic_id)});
        td::vector<td::int64> expected_topic_ids;
        for (auto &it : orders) {
          if (naive.get_public_order(it.second, it.first) == 0) {
            expected_topic_ids.push_back(it.first);
          }
        }
        naive.set_last_topic_date(order, topic_id);
        td::remove_if(expected_topic_ids, [&](td::int64 id) { return naive.get_public_order(orders[id], id) == 0; });
        auto changed = td::transform(changed_topic_ids, [](TestTopicId id) { return id.get_unique_id(); });
        std::sort(changed.begin(), changed.end());
        std::sort(expected_topic_ids.begin(), expected_topic_ids.end());
        ASSERT_TRUE(changed == expected_topic_ids);
      } else {
        td::int64 new_order = rnd.fast(0, 2) == 0 ? 0 : rnd.fast(1, static_cast<int>(max_order));
        auto &order = orders[topic_id];
        topics.update_order(TestTopicId(topic_id), order, new_order);
        naive.update_order(topic_id, new_order);
        order = new_order;
        if (order == 0) {
          orders.erase(topic_id);
        }
      }
      check();
    }
  }
}

TEST(OrderedTopics, total_count) {
  td::OrderedTopics<TestTopicId> topics;
  ASSERT_EQ(-1, topics.update_total_count());

  topics.update_order(TestTopicId(1), 0, 10);
  topics.update_order(TestTopicId(2), 0, 20);
  ASSERT_EQ(-1, topics.update_total_count());

  // the server knows about more topics than were loaded
  topics.set_server_total_count(5);
  topics.set_last_topic_date({10, TestTopicId(1)});
  ASSERT_EQ(5, topics.update_total_count());

  topics.update_order(TestTopicId(3), 0, 30);
  ASSERT_EQ(6, topics.get_server_total_count());
  ASSERT_EQ(6, topics.update_total_count());

  topics.update_order(TestTopicId(3), 30, 0);
  ASSERT_EQ(5, topics.get_server_total_count());

  // once the whole list is loaded, the local count wins
  topics.set_last_topic_date(td::OrderedTopics<TestTopicId>::max_topic_date());
  ASSERT_TRUE(topics.is_fully_loaded());
  ASSERT_EQ(2, topics.update_total_count());
  ASSERT_EQ(2, topics.get_server_total_count());
}
