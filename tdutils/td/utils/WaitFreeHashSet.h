//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/HashTableUtils.h"

#include <functional>

namespace td {

template <class KeyT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
class WaitFreeHashSet {
  using Storage = FlatHashSet<KeyT, HashT, EqT>;
  static constexpr size_t MAX_STORAGE_COUNT = 1 << 11;
  static_assert((MAX_STORAGE_COUNT & (MAX_STORAGE_COUNT - 1)) == 0, "");
  static constexpr size_t MAX_STORAGE_SIZE = 1 << 17;
  static_assert((MAX_STORAGE_SIZE & (MAX_STORAGE_SIZE - 1)) == 0, "");

  Storage default_set_;
  struct WaitFreeStorage {
    Storage sets_[MAX_STORAGE_COUNT];
  };
  unique_ptr<WaitFreeStorage> wait_free_storage_;

  Storage &get_wait_free_storage(const KeyT &key) {
    return wait_free_storage_->sets_[randomize_hash(HashT()(key)) & (MAX_STORAGE_COUNT - 1)];
  }

  Storage &get_storage(const KeyT &key) {
    if (wait_free_storage_ == nullptr) {
      return default_set_;
    }

    return get_wait_free_storage(key);
  }

  const Storage &get_storage(const KeyT &key) const {
    return const_cast<WaitFreeHashSet *>(this)->get_storage(key);
  }

  void split_storage() {
    CHECK(wait_free_storage_ == nullptr);
    wait_free_storage_ = make_unique<WaitFreeStorage>();
    for (auto &it : default_set_) {
      get_wait_free_storage(it).insert(it);
    }
    default_set_.clear();
  }

 public:
  void insert(const KeyT &key) {
    auto &storage = get_storage(key);
    storage.insert(key);
    if (default_set_.size() == MAX_STORAGE_SIZE) {
      split_storage();
    }
  }

  size_t count(const KeyT &key) const {
    const auto &storage = get_storage(key);
    return storage.count(key);
  }

  size_t erase(const KeyT &key) {
    return get_storage(key).erase(key);
  }

  void foreach(std::function<void(const KeyT &key)> callback) const {
    if (wait_free_storage_ == nullptr) {
      for (auto &it : default_set_) {
        callback(it);
      }
      return;
    }

    for (size_t i = 0; i < MAX_STORAGE_COUNT; i++) {
      for (auto &it : wait_free_storage_->sets_[i]) {
        callback(it);
      }
    }
  }

  size_t size() const {
    if (wait_free_storage_ == nullptr) {
      return default_set_.size();
    }

    size_t result = 0;
    for (size_t i = 0; i < MAX_STORAGE_COUNT; i++) {
      result += wait_free_storage_->sets_[i].size();
    }
    return result;
  }

  bool empty() const {
    if (wait_free_storage_ == nullptr) {
      return default_set_.empty();
    }

    for (size_t i = 0; i < MAX_STORAGE_COUNT; i++) {
      if (!wait_free_storage_->sets_[i].empty()) {
        return false;
      }
    }
    return true;
  }
};

}  // namespace td
