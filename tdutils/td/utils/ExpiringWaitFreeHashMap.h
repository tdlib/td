//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/List.h"
#include "td/utils/Time.h"
#include "td/utils/WaitFreeHashMap.h"

#include <functional>

namespace td {

// WaitFreeHashMap, which can drop items that weren't accessed during the specified amount of time
// expired items are dropped lazily during calls to non-const methods; at most max_dropped_item_count items per call
template <class KeyT, class ValueT, class HashT = Hash<KeyT>, class EqT = std::equal_to<KeyT>,
          size_t max_dropped_item_count = 10>
class ExpiringWaitFreeHashMap {
  struct Node final : public ListNode {
    KeyT key_;
    ValueT value_;
    double access_time_ = 0.0;

    Node(const KeyT &key, ValueT &&value) : key_(key), value_(std::move(value)) {
    }
  };

  ListNode access_list_;  // ordered by access time; must be declared before map_
  WaitFreeHashMap<KeyT, unique_ptr<Node>, HashT, EqT> map_;
  double expire_time_ = 0.0;

  void on_access(Node *node, double now) {
    node->access_time_ = now;
    node->remove();
    access_list_.put_back(node);
  }

  void drop_expired_items(double now) {
    for (size_t i = 0; i < max_dropped_item_count && !access_list_.empty(); i++) {
      auto *node = static_cast<Node *>(access_list_.get_next());
      if (node->access_time_ + expire_time_ > now) {
        break;
      }
      auto key = std::move(node->key_);  // the node is destroyed during erase
      map_.erase(key);
    }
  }

 public:
  explicit ExpiringWaitFreeHashMap(double expire_time) : expire_time_(expire_time) {
  }

  // returns nullptr if the item isn't found or has expired; doesn't update access time of the item
  // the pointer is valid until the next call to a non-const method
  ValueT *get(const KeyT &key) {
    auto now = Time::now();
    drop_expired_items(now);
    auto *node = map_.get_pointer(key);
    if (node == nullptr) {
      return nullptr;
    }
    if (node->access_time_ + expire_time_ <= now) {
      return nullptr;
    }
    return &node->value_;
  }

  // returns nullptr if the item isn't found; updates access time of the item
  // the pointer is valid until the next call to a non-const method
  ValueT *access(const KeyT &key) {
    auto now = Time::now();
    drop_expired_items(now);
    auto *node = map_.get_pointer(key);
    if (node == nullptr) {
      return nullptr;
    }
    on_access(node, now);
    return &node->value_;
  }

  void set(const KeyT &key, ValueT value) {
    auto now = Time::now();
    drop_expired_items(now);
    auto &node = map_[key];
    if (node == nullptr) {
      node = td::make_unique<Node>(key, std::move(value));
    } else {
      node->value_ = std::move(value);
    }
    on_access(node.get(), now);
  }

  // the reference is valid until the next call to a non-const method
  ValueT &operator[](const KeyT &key) {
    auto now = Time::now();
    drop_expired_items(now);
    auto &node = map_[key];
    if (node == nullptr) {
      node = td::make_unique<Node>(key, ValueT());
    }
    on_access(node.get(), now);
    return node->value_;
  }

  size_t erase(const KeyT &key) {
    drop_expired_items(Time::now());
    return map_.erase(key);
  }

  void clear() {
    map_ = WaitFreeHashMap<KeyT, unique_ptr<Node>, HashT, EqT>();
  }

  size_t calc_size() const {
    return map_.calc_size();
  }

  bool empty() const {
    return map_.empty();
  }
};

}  // namespace td
