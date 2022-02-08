//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/bits.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <cstddef>
#include <functional>
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

template <class KeyT, class ValueT, class HashT = std::hash<KeyT>>
class FlatHashMapImpl {
  struct Node {
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

    Node() {
    }
    ~Node() {
      if (!empty()) {
        second.~ValueT();
      }
    }
    Node(Node &&other) noexcept {
      *this = std::move(other);
    }
    Node &operator=(Node &&other) noexcept {
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
      CHECK(!empty());
    }
  };
  using Self = FlatHashMapImpl<KeyT, ValueT, HashT>;
  using NodeIterator = typename fixed_vector<Node>::iterator;
  using ConstNodeIterator = typename fixed_vector<Node>::const_iterator;

 public:
  // define key_type and value_type for benchmarks
  using key_type = KeyT;
  using value_type = std::pair<const KeyT, ValueT>;

  struct Iterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Node;
    using pointer = Node *;
    using reference = Node &;

    friend class FlatHashMapImpl;
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
    Node &operator*() {
      return *it_;
    }
    Node *operator->() {
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

    Iterator(NodeIterator it, Self *map) : it_(std::move(it)), map_(map) {
    }

   private:
    NodeIterator it_;
    Self *map_;
  };

  struct ConstIterator {
    using iterator_category = std::bidirectional_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Node;
    using pointer = const Node *;
    using reference = const Node &;

    friend class FlatHashMapImpl;
    ConstIterator &operator++() {
      ++it_;
      return *this;
    }
    ConstIterator &operator--() {
      --it_;
      return *this;
    }
    const Node &operator*() {
      return *it_;
    }
    const Node *operator->() {
      return &*it_;
    }
    bool operator==(const ConstIterator &other) const {
      return it_ == other.it_;
    }
    bool operator!=(const ConstIterator &other) const {
      return it_ != other.it_;
    }

    explicit ConstIterator(Iterator it) : it_(std::move(it)) {
    }

   private:
    Iterator it_;
  };

  FlatHashMapImpl() = default;
  FlatHashMapImpl(const FlatHashMapImpl &other) : FlatHashMapImpl(other.begin(), other.end()) {
  }
  FlatHashMapImpl &operator=(const FlatHashMapImpl &other) {
    assign(other.begin(), other.end());
    return *this;
  }
  FlatHashMapImpl(FlatHashMapImpl &&other) noexcept : nodes_(std::move(other.nodes_)), used_nodes_(other.used_nodes_) {
    other.used_nodes_ = 0;
  }
  FlatHashMapImpl &operator=(FlatHashMapImpl &&other) noexcept {
    nodes_ = std::move(other.nodes_);
    used_nodes_ = other.used_nodes_;
    other.used_nodes_ = 0;
    return *this;
  }
  ~FlatHashMapImpl() = default;

  template <class ItT>
  FlatHashMapImpl(ItT begin, ItT end) {
    assign(begin, end);
  }

  Iterator find(const KeyT &key) {
    if (empty()) {
      return end();
    }
    size_t bucket = calc_bucket(key);
    while (true) {
      if (nodes_[bucket].key() == key) {
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
    size_t want_size = normalize(size * 10 / 6 + 1);
    // size_t want_size = size * 2;
    if (want_size > nodes_.size()) {
      resize(want_size);
    }
  }

  template <class... ArgsT>
  std::pair<Iterator, bool> emplace(KeyT key, ArgsT &&...args) {
    if (unlikely(should_resize())) {
      grow();
    }
    size_t bucket = calc_bucket(key);
    while (true) {
      if (nodes_[bucket].key() == key) {
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

  ValueT &operator[](const KeyT &key) {
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
    DCHECK(!is_key_empty(it->key()));
    size_t empty_i = it.it_ - nodes_.begin();
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

 private:
  static bool is_key_empty(const KeyT &key) {
    return key == KeyT();
  }

  fixed_vector<Node> nodes_;
  size_t used_nodes_{};

  template <class ItT>
  void assign(ItT begin, ItT end) {
    resize(std::distance(begin, end));  // TODO: should be conditional
    for (; begin != end; ++begin) {
      emplace(begin->first, begin->second);
    }
  }

  bool should_resize() const {
    return should_resize(used_nodes_ + 1, nodes_.size());
  }
  static bool should_resize(size_t used_count, size_t buckets_count) {
    return used_count * 10 > buckets_count * 6;
  }

  size_t calc_bucket(const KeyT &key) const {
    return HashT()(key) * 2 % nodes_.size();
  }

  static size_t normalize(size_t size) {
    return size_t(1) << (64 - count_leading_zeroes64(size));
  }
  void grow() {
    size_t want_size = normalize(td::max(nodes_.size() * 2 - 1, (used_nodes_ + 1) * 10 / 6 + 1));
    // size_t want_size = td::max(nodes_.size(), (used_nodes_ + 1)) * 2;
    resize(want_size);
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
};

template <class KeyT, class ValueT, class HashT = std::hash<KeyT>>
using FlatHashMap = FlatHashMapImpl<KeyT, ValueT, HashT>;
//using FlatHashMap = std::unordered_map<KeyT, ValueT, HashT>;
//using FlatHashMap = absl::flat_hash_map<KeyT, ValueT, HashT>;

}  // namespace td
