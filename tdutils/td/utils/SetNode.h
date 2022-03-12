//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"

namespace td {

template <class KeyT>
struct SetNode {
  using public_key_type = KeyT;
  using public_type = const KeyT;
  using second_type = KeyT;  // TODO: remove second_type?

  KeyT first{};

  const KeyT &key() const {
    return first;
  }

  const KeyT &get_public() {
    return first;
  }

  SetNode() = default;
  explicit SetNode(KeyT key) : first(std::move(key)) {
  }
  SetNode(const SetNode &other) = delete;
  SetNode &operator=(const SetNode &other) = delete;
  SetNode(SetNode &&other) noexcept {
    *this = std::move(other);
  }
  void operator=(SetNode &&other) noexcept {
    DCHECK(empty());
    DCHECK(!other.empty());
    first = std::move(other.first);
    other.first = KeyT{};
  }
  ~SetNode() = default;

  void copy_from(const SetNode &other) {
    DCHECK(empty());
    first = other.first;
    DCHECK(!empty());
  }

  bool empty() const {
    return is_hash_table_key_empty(first);
  }

  void clear() {
    first = KeyT();
    DCHECK(empty());
  }

  void emplace(KeyT key) {
    first = std::move(key);
  }
};

}  // namespace td
