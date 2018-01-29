//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/actor/Timeout.h"

#include "td/utils/Time.h"

namespace td {

bool MultiTimeout::has_timeout(int64 key) const {
  return items_.find(Item(key)) != items_.end();
}

void MultiTimeout::set_timeout_at(int64 key, double timeout) {
  LOG(DEBUG) << "Set timeout for " << key << " in " << timeout - Time::now();
  auto item = items_.emplace(key);
  auto heap_node = static_cast<HeapNode *>(const_cast<Item *>(&*item.first));
  if (heap_node->in_heap()) {
    CHECK(!item.second);
    bool need_update_timeout = heap_node->is_top();
    timeout_queue_.fix(timeout, heap_node);
    if (need_update_timeout || heap_node->is_top()) {
      update_timeout();
    }
  } else {
    CHECK(item.second);
    timeout_queue_.insert(timeout, heap_node);
    if (heap_node->is_top()) {
      update_timeout();
    }
  }
}

void MultiTimeout::add_timeout_at(int64 key, double timeout) {
  LOG(DEBUG) << "Add timeout for " << key << " in " << timeout - Time::now();
  auto item = items_.emplace(key);
  auto heap_node = static_cast<HeapNode *>(const_cast<Item *>(&*item.first));
  if (heap_node->in_heap()) {
    CHECK(!item.second);
  } else {
    CHECK(item.second);
    timeout_queue_.insert(timeout, heap_node);
    if (heap_node->is_top()) {
      update_timeout();
    }
  }
}

void MultiTimeout::cancel_timeout(int64 key) {
  LOG(DEBUG) << "Cancel timeout for " << key;
  auto item = items_.find(Item(key));
  if (item != items_.end()) {
    auto heap_node = static_cast<HeapNode *>(const_cast<Item *>(&*item));
    CHECK(heap_node->in_heap());
    bool need_update_timeout = heap_node->is_top();
    timeout_queue_.erase(heap_node);
    items_.erase(item);

    if (need_update_timeout) {
      update_timeout();
    }
  }
}

void MultiTimeout::update_timeout() {
  if (items_.empty()) {
    LOG(DEBUG) << "Cancel timeout";
    CHECK(timeout_queue_.empty());
    CHECK(Actor::has_timeout());
    Actor::cancel_timeout();
  } else {
    LOG(DEBUG) << "Set timeout in " << timeout_queue_.top_key() - Time::now_cached();
    Actor::set_timeout_at(timeout_queue_.top_key());
  }
}

void MultiTimeout::timeout_expired() {
  double now = Time::now_cached();
  while (!timeout_queue_.empty() && timeout_queue_.top_key() < now) {
    int64 key = static_cast<Item *>(timeout_queue_.pop())->key;
    items_.erase(Item(key));
    expired_.push_back(key);
  }
  if (!items_.empty()) {
    update_timeout();
  }
  for (auto key : expired_) {
    callback_(data_, key);
  }
  expired_.clear();
}

}  // namespace td
