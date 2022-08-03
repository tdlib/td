//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/HashTableUtils.h"

#include <functional>

namespace td {

template <class KeyT, class ValueT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
class WaitFreeHashMap {
  using Storage = FlatHashMap<KeyT, ValueT, HashT, EqT>;
  static constexpr size_t MAX_STORAGE_COUNT = 256;
  static_assert((MAX_STORAGE_COUNT & (MAX_STORAGE_COUNT - 1)) == 0, "");
  static constexpr size_t MAX_STORAGE_SIZE = MAX_STORAGE_COUNT * MAX_STORAGE_COUNT / 2;

  Storage default_map_;
  struct WaitFreeStorage {
    Storage maps_[MAX_STORAGE_COUNT];
  };
  unique_ptr<WaitFreeStorage> wait_free_storage_;

  Storage &get_wait_free_storage(const KeyT &key) {
    return wait_free_storage_->maps_[randomize_hash(HashT()(key)) & (MAX_STORAGE_COUNT - 1)];
  }

  Storage &get_storage(const KeyT &key) {
    if (wait_free_storage_ == nullptr) {
      return default_map_;
    }

    return get_wait_free_storage(key);
  }

  const Storage &get_storage(const KeyT &key) const {
    return const_cast<WaitFreeHashMap *>(this)->get_storage(key);
  }

  void split() {
    CHECK(wait_free_storage_ == nullptr);
    wait_free_storage_ = make_unique<WaitFreeStorage>();
    for (auto &it : default_map_) {
      get_wait_free_storage(it.first).emplace(it.first, std::move(it.second));
    }
    default_map_.clear();
  }

 public:
  void set(const KeyT &key, ValueT value) {
    auto &storage = get_storage(key);
    storage[key] = std::move(value);
    if (default_map_.size() == MAX_STORAGE_SIZE) {
      split();
    }
  }

  ValueT get(const KeyT &key) const {
    const auto &storage = get_storage(key);
    auto it = storage.find(key);
    if (it == storage.end()) {
      return {};
    }
    return it->second;
  }

  // specialization for WaitFreeHashMap<..., unique_ptr<T>>
  template <typename ReturnT = decltype(ValueT().get())>
  ReturnT get_pointer(const KeyT &key) {
    auto &storage = get_storage(key);
    auto it = storage.find(key);
    if (it == storage.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  template <typename ReturnT = decltype(static_cast<const ValueT &>(ValueT()).get())>
  ReturnT get_pointer(const KeyT &key) const {
    auto &storage = get_storage(key);
    auto it = storage.find(key);
    if (it == storage.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  ValueT &operator[](const KeyT &key) {
    if (wait_free_storage_ == nullptr) {
      ValueT &result = default_map_[key];
      if (default_map_.size() != MAX_STORAGE_SIZE) {
        return result;
      }

      split();
    }

    return get_wait_free_storage(key)[key];
  }

  size_t erase(const KeyT &key) {
    return get_storage(key).erase(key);
  }

  size_t size() const {
    if (wait_free_storage_ == nullptr) {
      return default_map_.size();
    }

    size_t result = 0;
    for (size_t i = 0; i < MAX_STORAGE_COUNT; i++) {
      result += wait_free_storage_->maps_[i].size();
    }
    return result;
  }

  bool empty() const {
    if (wait_free_storage_ == nullptr) {
      return default_map_.empty();
    }

    for (size_t i = 0; i < MAX_STORAGE_COUNT; i++) {
      if (!wait_free_storage_->maps_[i].empty()) {
        return false;
      }
    }
    return true;
  }
};

}  // namespace td
