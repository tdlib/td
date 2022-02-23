//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/bits.h"
#include "td/utils/common.h"
#include "td/utils/fixed_vector.h"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <new>
#include <utility>

namespace td {

template <class KeyT>
bool is_key_empty(const KeyT &key) {
  return key == KeyT();
}

inline uint32 randomize_hash(size_t h) {
  auto result = static_cast<uint32>(h & 0xFFFFFFFF);
  result ^= result >> 16;
  result *= 0x85ebca6b;
  result ^= result >> 13;
  result *= 0xc2b2ae35;
  result ^= result >> 16;
  return result;
}

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
    return is_key_empty(key());
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

template <class KeyT>
struct SetNode {
  using public_key_type = KeyT;
  using public_type = KeyT;
  using second_type = KeyT;  // TODO: remove second_type?

  KeyT first{};

  const KeyT &key() const {
    return first;
  }

  KeyT &get_public() {
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
    DCHECK(!other.empty());
    first = other.first;
  }

  bool empty() const {
    return is_key_empty(key());
  }

  void clear() {
    first = KeyT();
    DCHECK(empty());
  }

  void emplace(KeyT key) {
    first = std::move(key);
  }
};

template <class NodeT, class HashT, class EqT>
class FlatHashTable {
 public:
  using NodeIterator = NodeT *;

  using KeyT = typename NodeT::public_key_type;
  using key_type = typename NodeT::public_key_type;
  using value_type = typename NodeT::public_type;

  struct Iterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = FlatHashTable::value_type;
    using pointer = value_type *;
    using reference = value_type &;

    friend class FlatHashTable;
    Iterator &operator++() {
      do {
        ++it_;
      } while (it_ != end_ && it_->empty());
      return *this;
    }
    Iterator &operator--() {
      do {
        --it_;
      } while (it_->empty());
      return *this;
    }
    reference operator*() {
      return it_->get_public();
    }
    pointer operator->() {
      return &*it_;
    }
    bool operator==(const Iterator &other) const {
      DCHECK(end_ == other.end_);
      return it_ == other.it_;
    }
    bool operator!=(const Iterator &other) const {
      DCHECK(end_ == other.end_);
      return it_ != other.it_;
    }

    Iterator() = default;
    Iterator(NodeIterator it, FlatHashTable *map) : it_(std::move(it)), end_(map->nodes_.end()) {
    }

   private:
    NodeIterator it_;
    NodeT *end_;
  };

  struct ConstIterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = FlatHashTable::value_type;
    using pointer = const value_type *;
    using reference = const value_type &;

    friend class FlatHashTable;
    ConstIterator &operator++() {
      ++it_;
      return *this;
    }
    ConstIterator &operator--() {
      --it_;
      return *this;
    }
    reference operator*() {
      return *it_;
    }
    pointer operator->() {
      return &*it_;
    }
    bool operator==(const ConstIterator &other) const {
      return it_ == other.it_;
    }
    bool operator!=(const ConstIterator &other) const {
      return it_ != other.it_;
    }

    ConstIterator() = default;
    ConstIterator(Iterator it) : it_(std::move(it)) {
    }

   private:
    Iterator it_;
  };
  using iterator = Iterator;
  using const_iterator = ConstIterator;

  FlatHashTable() = default;
  FlatHashTable(const FlatHashTable &other) {
    assign(other);
  }
  void operator=(const FlatHashTable &other) {
    clear();
    assign(other);
  }

  FlatHashTable(std::initializer_list<NodeT> nodes) {
    if (nodes.size() == 0) {
      return;
    }
    reserve(nodes.size());
    for (auto &new_node : nodes) {
      CHECK(!new_node.empty());
      auto bucket = calc_bucket(new_node.key());
      while (true) {
        auto &node = nodes_[bucket];
        if (node.empty()) {
          node.copy_from(new_node);
          used_nodes_++;
          break;
        }
        if (EqT()(node.key(), new_node.key())) {
          break;
        }
        next_bucket(bucket);
      }
    }
  }

  FlatHashTable(FlatHashTable &&other) noexcept : nodes_(std::move(other.nodes_)), used_nodes_(other.used_nodes_) {
    other.clear();
  }
  void operator=(FlatHashTable &&other) noexcept {
    nodes_ = std::move(other.nodes_);
    used_nodes_ = other.used_nodes_;
    other.clear();
  }
  void swap(FlatHashTable &other) noexcept {
    nodes_.swap(other.nodes_);
    std::swap(used_nodes_, other.used_nodes_);
  }
  ~FlatHashTable() = default;

  size_t bucket_count() const {
    return nodes_.size();
  }

  Iterator find(const KeyT &key) {
    if (empty() || is_key_empty(key)) {
      return end();
    }
    auto bucket = calc_bucket(key);
    while (true) {
      auto &node = nodes_[bucket];
      if (EqT()(node.key(), key)) {
        return Iterator{&node, this};
      }
      if (node.empty()) {
        return end();
      }
      next_bucket(bucket);
    }
  }

  ConstIterator find(const KeyT &key) const {
    return ConstIterator(const_cast<FlatHashTable *>(this)->find(key));
  }

  size_t size() const {
    return used_nodes_;
  }

  bool empty() const {
    return size() == 0;
  }

  Iterator begin() {
    if (empty()) {
      return end();
    }
    auto it = nodes_.begin();
    while (it->empty()) {
      ++it;
    }
    return Iterator(it, this);
  }
  Iterator end() {
    return Iterator(nodes_.end(), this);
  }

  ConstIterator begin() const {
    return ConstIterator(const_cast<FlatHashTable *>(this)->begin());
  }
  ConstIterator end() const {
    return ConstIterator(const_cast<FlatHashTable *>(this)->end());
  }

  void reserve(size_t size) {
    size_t want_size = normalize(size * 5 / 3 + 1);
    // size_t want_size = size * 2;
    if (want_size > nodes_.size()) {
      resize(want_size);
    }
  }

  template <class... ArgsT>
  std::pair<Iterator, bool> emplace(KeyT key, ArgsT &&...args) {
    try_grow();
    CHECK(!is_key_empty(key));
    auto bucket = calc_bucket(key);
    while (true) {
      auto &node = nodes_[bucket];
      if (EqT()(node.key(), key)) {
        return {Iterator{&node, this}, false};
      }
      if (node.empty()) {
        node.emplace(std::move(key), std::forward<ArgsT>(args)...);
        used_nodes_++;
        return {Iterator{&node, this}, true};
      }
      next_bucket(bucket);
    }
  }

  std::pair<Iterator, bool> insert(KeyT key) {
    return emplace(std::move(key));
  }

  template <class ItT>
  void insert(ItT begin, ItT end) {
    for (; begin != end; ++begin) {
      emplace(*begin);
    }
  }

  template <class T = typename NodeT::second_type>
  T &operator[](const KeyT &key) {
    return emplace(key).first->second;
  }

  size_t erase(const KeyT &key) {
    auto it = find(key);
    if (it == end()) {
      return 0;
    }
    erase(it);
    return 1;
  }

  size_t count(const KeyT &key) const {
    return find(key) != end();
  }

  void clear() {
    used_nodes_ = 0;
    nodes_ = {};
  }

  void erase(Iterator it) {
    DCHECK(it != end());
    DCHECK(!it.it_->empty());
    erase_node(it.it_);
    try_shrink();
  }

  template <class F>
  void remove_if(F &&f) {
    auto it = begin().it_;
    while (it != nodes_.end() && !it->empty()) {
      ++it;
    }
    auto first_empty = it;
    for (; it != nodes_.end();) {
      if (!it->empty() && f(it->get_public())) {
        erase_node(it);
      } else {
        ++it;
      }
    }
    for (it = nodes_.begin(); it != first_empty;) {
      if (!it->empty() && f(it->get_public())) {
        erase_node(it);
      } else {
        ++it;
      }
    }
    try_shrink();
  }

 private:
  fixed_vector<NodeT> nodes_;
  size_t used_nodes_{};

  void assign(const FlatHashTable &other) {
    resize(other.size());
    for (const auto &new_node : other) {
      auto bucket = calc_bucket(new_node.key());
      while (true) {
        auto &node = nodes_[bucket];
        if (node.empty()) {
          node.copy_from(new_node);
          break;
        }
        next_bucket(bucket);
      }
    }
    used_nodes_ = other.size();
  }

  void try_grow() {
    if (should_grow(used_nodes_ + 1, nodes_.size())) {
      grow();
    }
  }
  static bool should_grow(size_t used_count, size_t bucket_count) {
    return used_count * 5 > bucket_count * 3;
  }
  void try_shrink() {
    if (should_shrink(used_nodes_, nodes_.size())) {
      shrink();
    }
  }
  static bool should_shrink(size_t used_count, size_t bucket_count) {
    return used_count * 10 < bucket_count;
  }

  static size_t normalize(size_t size) {
    return static_cast<size_t>(1) << (64 - count_leading_zeroes64(size | 7));
  }

  void shrink() {
    size_t want_size = normalize((used_nodes_ + 1) * 5 / 3 + 1);
    resize(want_size);
  }

  void grow() {
    size_t want_size = normalize(2 * nodes_.size() - !nodes_.empty());
    resize(want_size);
  }

  uint32 calc_bucket(const KeyT &key) const {
    return randomize_hash(HashT()(key)) & static_cast<uint32>(nodes_.size() - 1);
  }

  void resize(size_t new_size) {
    fixed_vector<NodeT> old_nodes(new_size);
    old_nodes.swap(nodes_);

    for (auto &old_node : old_nodes) {
      if (old_node.empty()) {
        continue;
      }
      auto bucket = calc_bucket(old_node.key());
      while (!nodes_[bucket].empty()) {
        next_bucket(bucket);
      }
      nodes_[bucket] = std::move(old_node);
    }
  }

  void next_bucket(uint32 &bucket) const {
    bucket = (bucket + 1) & static_cast<uint32>(nodes_.size() - 1);
  }

  void erase_node(NodeIterator it) {
    size_t empty_i = it - nodes_.begin();
    auto empty_bucket = empty_i;
    DCHECK(0 <= empty_i && empty_i < nodes_.size());
    nodes_[empty_bucket].clear();
    used_nodes_--;

    for (size_t test_i = empty_i + 1;; test_i++) {
      auto test_bucket = test_i;
      if (test_bucket >= nodes_.size()) {
        test_bucket -= nodes_.size();
      }

      if (nodes_[test_bucket].empty()) {
        break;
      }

      auto want_i = calc_bucket(nodes_[test_bucket].key());
      if (want_i < empty_i) {
        want_i += static_cast<uint32>(nodes_.size());
      }

      if (want_i <= empty_i || want_i > test_i) {
        nodes_[empty_bucket] = std::move(nodes_[test_bucket]);
        empty_i = test_i;
        empty_bucket = test_bucket;
      }
    }
  }
};

template <class KeyT, class ValueT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
using FlatHashMapImpl = FlatHashTable<MapNode<KeyT, ValueT>, HashT, EqT>;
template <class KeyT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
using FlatHashSetImpl = FlatHashTable<SetNode<KeyT>, HashT, EqT>;

}  // namespace td
