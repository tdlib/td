// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#include "td/actor/MultiTimeout.h"

#include "td/utils/logging.h"

#if (TD_HAS_FEATURE_MEMORY_SANITIZER || defined(__SANITIZE_MEMORY__)) && defined(__clang__)
#define TD_MULTITIMEOUT_MSAN_NO_SANITIZE __attribute__((no_sanitize("memory")))
#else
#define TD_MULTITIMEOUT_MSAN_NO_SANITIZE
#endif

namespace td {

bool MultiTimeout::has_timeout(int64 key) const {
  return items_.count(Item(key)) > 0;
}

TD_MULTITIMEOUT_MSAN_NO_SANITIZE
void MultiTimeout::set_timeout_at(int64 key, double timeout) {
  LOG(DEBUG) << "Set " << get_name() << " for " << key << " in " << timeout - Time::now();
  auto item = items_.emplace(key);
  auto heap_node = static_cast<HeapNode *>(const_cast<Item *>(&*item.first));
  if (heap_node->in_heap()) {
    CHECK(!item.second);
    bool need_update_timeout = heap_node->is_top();
    timeout_queue_.fix(timeout, heap_node);
    if (need_update_timeout || heap_node->is_top()) {
      update_timeout("set_timeout");
    }
  } else {
    CHECK(item.second);
    timeout_queue_.insert(timeout, heap_node);
    if (heap_node->is_top()) {
      update_timeout("set_timeout 2");
    }
  }
}

void MultiTimeout::add_timeout_at(int64 key, double timeout) {
  LOG(DEBUG) << "Add " << get_name() << " for " << key << " in " << timeout - Time::now();
  auto item = items_.emplace(key);
  auto heap_node = static_cast<HeapNode *>(const_cast<Item *>(&*item.first));
  if (heap_node->in_heap()) {
    CHECK(!item.second);
  } else {
    CHECK(item.second);
    timeout_queue_.insert(timeout, heap_node);
    if (heap_node->is_top()) {
      update_timeout("add_timeout");
    }
  }
}

void MultiTimeout::cancel_timeout(int64 key, const char *source) {
  LOG(DEBUG) << "Cancel " << get_name() << " for " << key;
  auto item = items_.find(Item(key));
  if (item != items_.end()) {
    auto heap_node = static_cast<HeapNode *>(const_cast<Item *>(&*item));
    CHECK(heap_node->in_heap());
    bool need_update_timeout = heap_node->is_top();
    timeout_queue_.erase(heap_node);
    items_.erase(item);

    if (need_update_timeout) {
      update_timeout(source);
    }
  }
}

void MultiTimeout::update_timeout(const char *source) {
  if (items_.empty()) {
    LOG(DEBUG) << "Cancel timeout of " << get_name();
    LOG_CHECK(timeout_queue_.empty()) << get_name() << ' ' << source;
    if (!Actor::has_timeout()) {
      bool has_pending_timeout = false;
      for (auto &event : get_info()->mailbox_) {
        if (event.type == Event::Type::Timeout) {
          has_pending_timeout = true;
        }
      }
      LOG_CHECK(has_pending_timeout) << get_name() << ' ' << get_info()->mailbox_.size() << ' ' << source;
    } else {
      Actor::cancel_timeout();
    }
  } else {
    LOG(DEBUG) << "Set timeout of " << get_name() << " in " << timeout_queue_.top_key() - Time::now_cached();
    Actor::set_timeout_at(timeout_queue_.top_key());
  }
}

vector<int64> MultiTimeout::get_expired_keys(double now) {
  vector<int64> expired_keys;
  while (!timeout_queue_.empty() && timeout_queue_.top_key() < now) {
    int64 key = static_cast<Item *>(timeout_queue_.pop())->key;
    items_.erase(Item(key));
    expired_keys.push_back(key);
  }
  return expired_keys;
}

void MultiTimeout::timeout_expired() {
  vector<int64> expired_keys = get_expired_keys(Time::now_cached());
  if (!items_.empty()) {
    update_timeout("timeout_expired");
  }
  for (auto key : expired_keys) {
    callback_(data_, key);
  }
}

void MultiTimeout::run_all() {
  vector<int64> expired_keys = get_expired_keys(Time::now_cached() + 1e10);
  if (!expired_keys.empty()) {
    update_timeout("run_all");
  }
  for (auto key : expired_keys) {
    callback_(data_, key);
  }
}

}  // namespace td
