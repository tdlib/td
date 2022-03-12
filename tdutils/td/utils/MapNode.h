//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"

#include <new>
#include <utility>

namespace td {

template <class KeyT, class ValueT>
struct MapNode {
  using first_type = KeyT;
  using second_type = ValueT;
  using public_key_type = KeyT;
  using public_type = MapNode;

  KeyT first{};
  union {
    ValueT second;
  };

  const KeyT &key() const {
    return first;
  }

  MapNode &get_public() {
    return *this;
  }

  const MapNode &get_public() const {
    return *this;
  }

  MapNode() {
  }
  MapNode(KeyT key, ValueT value) : first(std::move(key)) {
    new (&second) ValueT(std::move(value));
    DCHECK(!empty());
  }
  MapNode(const MapNode &other) = delete;
  MapNode &operator=(const MapNode &other) = delete;
  MapNode(MapNode &&other) noexcept {
    *this = std::move(other);
  }
  void operator=(MapNode &&other) noexcept {
    DCHECK(empty());
    DCHECK(!other.empty());
    first = std::move(other.first);
    other.first = KeyT{};
    new (&second) ValueT(std::move(other.second));
    other.second.~ValueT();
  }
  ~MapNode() {
    if (!empty()) {
      second.~ValueT();
    }
  }

  void copy_from(const MapNode &other) {
    DCHECK(empty());
    DCHECK(!other.empty());
    first = other.first;
    new (&second) ValueT(other.second);
  }

  bool empty() const {
    return is_hash_table_key_empty(first);
  }

  void clear() {
    DCHECK(!empty());
    first = KeyT();
    second.~ValueT();
    DCHECK(empty());
  }

  template <class... ArgsT>
  void emplace(KeyT key, ArgsT &&...args) {
    DCHECK(empty());
    first = std::move(key);
    new (&second) ValueT(std::forward<ArgsT>(args)...);
    DCHECK(!empty());
  }
};

}  // namespace td
