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

  Storage &get_storage(const KeyT &key) {
    if (wait_free_storage_ == nullptr) {
      return default_map_;
    }

    return wait_free_storage_->maps_[randomize_hash(HashT()(key)) & (MAX_STORAGE_COUNT - 1)];
  }

 public:
  void set(const KeyT &key, ValueT value) {
    auto &storage = get_storage(key);
    storage[key] = std::move(value);
    if (default_map_.size() == MAX_STORAGE_SIZE) {
      CHECK(wait_free_storage_ == nullptr);
      wait_free_storage_ = make_unique<WaitFreeStorage>();
      for (auto &it : default_map_) {
        get_storage(it.first).emplace(it.first, std::move(it.second));
      }
      default_map_.clear();
    }
  }

  ValueT get(const KeyT &key) {
    auto &storage = get_storage(key);
    auto it = storage.find(key);
    if (it == storage.end()) {
      return {};
    }
    return it->second;
  }

  size_t erase(const KeyT &key) {
    return get_storage(key).erase(key);
  }
};

}  // namespace td
