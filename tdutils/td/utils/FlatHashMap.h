//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/bits.h"
#include "td/utils/common.h"

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <new>
#include <unordered_map>
#include <utility>

namespace td {

template <class T>
class fixed_vector {
 public:
  fixed_vector() = default;
  explicit fixed_vector(size_t size) : ptr_(new T[size]), size_(size) {
  }
  fixed_vector(fixed_vector &&other) noexcept {
    swap(other);
  }
  fixed_vector &operator=(fixed_vector &&other) noexcept {
    swap(other);
    return *this;
  }
  fixed_vector(const fixed_vector &) = delete;
  fixed_vector &operator=(const fixed_vector &) = delete;
  ~fixed_vector() {
    delete[] ptr_;
  }
  T &operator[](size_t i) {
    return ptr_[i];
  }
  const T &operator[](size_t i) const {
    return ptr_[i];
  }
  T *begin() {
    return ptr_;
  }
  const T *begin() const {
    return ptr_;
  }
  T *end() {
    return ptr_ + size_;
  }
  const T *end() const {
    return ptr_ + size_;
  }
  bool empty() const {
    return size() == 0;
  }
  size_t size() const {
    return size_;
  }
  using iterator = T *;
  using const_iterator = const T *;
  void swap(fixed_vector<T> &other) {
    std::swap(ptr_, other.ptr_);
    std::swap(size_, other.size_);
  }

 private:
  T *ptr_{};
  size_t size_{0};
};

// TODO: move
template <class KeyT>
bool is_key_empty(const KeyT &key) {
  return key == KeyT();
}

template <class KeyT, class ValueT>
struct MapNode {
  using first_type = KeyT;
  using second_type = ValueT;
  using key_type = KeyT;
  using public_type = MapNode<KeyT, ValueT>;
  using value_type = ValueT;
  KeyT first{};
  union {
    ValueT second;
  };
  const auto &key() const {
    return first;
  }
  auto &value() {
    return second;
  }
  auto &get_public() {
    return *this;
  }

  MapNode() {
  }
  MapNode(KeyT key, ValueT value) : first(std::move(key)) {
    new (&second) ValueT(std::move(value));
        DCHECK(!empty());
  }
  ~MapNode() {
    if (!empty()) {
      second.~ValueT();
    }
  }
  MapNode(MapNode &&other) noexcept {
    *this = std::move(other);
  }
  MapNode &operator=(MapNode &&other) noexcept {
        DCHECK(empty());
        DCHECK(!other.empty());
    first = std::move(other.first);
    other.first = KeyT{};
    new (&second) ValueT(std::move(other.second));
    other.second.~ValueT();
    return *this;
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
  using first_type = KeyT;
  using key_type = KeyT;
  using public_type = KeyT;
  using value_type = KeyT;
  KeyT first{};
  const auto &key() const {
    return first;
  }
  const auto &value() const {
    return first;
  }

  auto &get_public() {
    return first;
  }
  SetNode() = default;
  explicit SetNode(KeyT key) : first(std::move(key)) {
  }
  SetNode(SetNode &&other) noexcept {
    *this = std::move(other);
  }
  SetNode &operator=(SetNode &&other) noexcept {
        DCHECK(empty());
        DCHECK(!other.empty());
    first = std::move(other.first);
    other.first = KeyT{};
    return *this;
  }

  bool empty() const {
    return is_key_empty(key());
  }

  void clear() {
    first = KeyT();
    CHECK(empty());
  }

  void emplace(KeyT key) {
    first = std::move(key);
  }
};

template <class NodeT, class HashT, class EqT>
class FlatHashTable {
 public:
  using Self = FlatHashTable<NodeT, HashT, EqT>;
  using Node = NodeT;
  using NodeIterator = typename fixed_vector<Node>::iterator;
  using ConstNodeIterator = typename fixed_vector<Node>::const_iterator;

  using KeyT = typename Node::key_type;
  using public_type = typename Node::public_type;
  using value_type = typename Node::value_type;

  struct Iterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = public_type;
    using pointer = public_type *;
    using reference = public_type &;

    friend class FlatHashTable;
    Iterator &operator++() {
      do {
        ++it_;
      } while (it_ != map_->nodes_.end() && it_->empty());
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
          DCHECK(map_ == other.map_);
      return it_ == other.it_;
    }
    bool operator!=(const Iterator &other) const {
          DCHECK(map_ == other.map_);
      return it_ != other.it_;
    }

    Iterator() = default;
    Iterator(NodeIterator it, Self *map) : it_(std::move(it)), map_(map) {
    }

   private:
    NodeIterator it_;
    Self *map_;
  };

  struct ConstIterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = public_type;
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
  FlatHashTable(const FlatHashTable &other) : FlatHashTable(other.begin(), other.end()) {
  }
  FlatHashTable &operator=(const FlatHashTable &other) {
    assign(other.begin(), other.end());
    return *this;
  }

  FlatHashTable(std::initializer_list<Node> nodes) {
    reserve(nodes.size());
    for (auto &node : nodes) {
      CHECK(!node.empty());
      auto bucket = calc_bucket(node.first);
      while (true) {
        if (nodes_[bucket].key() == node.first) {
          nodes_[bucket].second = node.second;
          break;
        }
        if (nodes_[bucket].empty()) {
          nodes_[bucket].emplace(node.first, node.second);
          used_nodes_++;
          break;
        }
        next_bucket(bucket);
      }
    }
  }

  FlatHashTable(FlatHashTable &&other) noexcept : nodes_(std::move(other.nodes_)), used_nodes_(other.used_nodes_) {
    other.used_nodes_ = 0;
  }
  FlatHashTable &operator=(FlatHashTable &&other) noexcept {
    nodes_ = std::move(other.nodes_);
    used_nodes_ = other.used_nodes_;
    other.used_nodes_ = 0;
    return *this;
  }
  void swap(FlatHashTable &other) noexcept {
    using std::swap;
    swap(nodes_, other.nodes_);
    swap(used_nodes_, other.used_nodes_);
  }
  ~FlatHashTable() = default;

  template <class ItT>
  FlatHashTable(ItT begin, ItT end) {
    assign(begin, end);
  }

  size_t bucket_count() const {
    return nodes_.size();
  }

  Iterator find(const KeyT &key) {
    if (empty() || is_key_empty(key)) {
      return end();
    }
    auto bucket = calc_bucket(key);
    while (true) {
      if (EqT()(nodes_[bucket].key(), key)) {
        return Iterator{nodes_.begin() + bucket, this};
      }
      if (nodes_[bucket].empty()) {
        return end();
      }
      next_bucket(bucket);
    }
  }

  ConstIterator find(const KeyT &key) const {
    return ConstIterator(const_cast<Self *>(this)->find(key));
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
    return ConstIterator(const_cast<Self *>(this)->begin());
  }
  ConstIterator end() const {
    return ConstIterator(const_cast<Self *>(this)->end());
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
      if (EqT()(nodes_[bucket].key(), key)) {
        return {Iterator{nodes_.begin() + bucket, this}, false};
      }
      if (nodes_[bucket].empty()) {
        nodes_[bucket].emplace(std::move(key), std::forward<ArgsT>(args)...);
        used_nodes_++;
        return {Iterator{nodes_.begin() + bucket, this}, true};
      }
      next_bucket(bucket);
    }
  }

  std::pair<Iterator, bool> insert(KeyT key) {
    return emplace(std::move(key));
  }

  template <class ItT>
  void insert(ItT begin, ItT end)  {
    for (; begin != end; ++begin) {
      emplace(*begin);
    }
  }

  value_type &operator[](const KeyT &key) {
    return emplace(key).first->value();
  }

  size_t erase(const KeyT &key) {
    auto it = find(key);
    if (it == end()) {
      return 0;
    }
    erase(it);
    try_shrink();
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
  }

  template <class F>
  void remove_if(F &&f) {
    auto it = nodes_.begin();
    while (it != nodes_.end() && !it->empty()) {
      ++it;
    }
    auto first_empty = it;
    for (; it != nodes_.end();) {
      if (!it->empty() && f(*it)) {
        erase_node(it);
      } else {
        ++it;
      }
    }
    for (it = nodes_.begin(); it != first_empty;) {
      if (!it->empty() && f(*it)) {
        erase_node(it);
      } else {
        ++it;
      }
    }
    try_shrink();
  }

 private:

  fixed_vector<Node> nodes_;
  size_t used_nodes_{};

  template <class ItT>
  void assign(ItT begin, ItT end) {
    resize(std::distance(begin, end));  // TODO: should be conditional
    for (; begin != end; ++begin) {
      emplace(begin->first, begin->second);
    }
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

  size_t calc_bucket(const KeyT &key) const {
    return HashT()(key) * 2 % nodes_.size();
  }

  void resize(size_t new_size) {
    fixed_vector<Node> old_nodes(new_size);
    std::swap(old_nodes, nodes_);

    for (auto &node : old_nodes) {
      if (node.empty()) {
        continue;
      }
      size_t bucket = calc_bucket(node.key());
      while (!nodes_[bucket].empty()) {
        next_bucket(bucket);
      }
      nodes_[bucket] = std::move(node);
    }
  }

  void next_bucket(size_t &bucket) const {
    bucket++;
    if (bucket == nodes_.size()) {
      bucket = 0;
    }
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
        want_i += nodes_.size();
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

template <class KeyT, class ValueT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
using FlatHashMap = FlatHashMapImpl<KeyT, ValueT, HashT, EqT>;
//using FlatHashMap = std::unordered_map<KeyT, ValueT, HashT, EqT>;

template <class KeyT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
using FlatHashSet = FlatHashSetImpl<KeyT, HashT, EqT>;
//using FlatHashSet = std::unordered_set<KeyT, HashT, EqT>;


}  // namespace td
