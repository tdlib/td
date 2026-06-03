//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/algorithm.h"
#include "td/utils/common.h"

#include <algorithm>
#include <utility>

namespace td {

template <class T>
class FastSetWithPosition {
 public:
  std::vector<T> get_some_elements() const {
    std::vector<T> res;
    res.reserve(4);
    if (!checked_.empty()) {
      res.push_back(*checked_.begin());
      res.push_back(*checked_.rbegin());
    }
    if (!not_checked_.empty()) {
      res.push_back(*not_checked_.begin());
      res.push_back(*not_checked_.rbegin());
    }
    td::unique(res);
    if (res.size() > 2) {
      res.erase(res.begin() + 1, res.end() - 1);
    }
    return res;
  }

  bool add(T x) {
    if (contains_sorted(checked_, x)) {
      return false;
    }
    return insert_sorted_unique(not_checked_, std::move(x));
  }

  bool remove(T x) {
    return erase_sorted(checked_, x) || erase_sorted(not_checked_, x);
  }

  bool has_next() const {
    return !not_checked_.empty();
  }

  void reset_position() {
    if (not_checked_.empty()) {
      not_checked_ = std::move(checked_);
    } else {
      merge_sorted_unique(not_checked_, checked_);
    }
    reset_to_empty(checked_);
  }

  T next() {
    CHECK(has_next());
    auto res = not_checked_.front();
    not_checked_.erase(not_checked_.begin());
    insert_sorted_unique(checked_, res);
    return res;
  }

  void merge(FastSetWithPosition &&other) {
    if (this == &other) {
      return;
    }

    if (size() < other.size()) {
      std::swap(*this, other);
    }
    for (auto x : other.checked_) {
      erase_sorted(not_checked_, x);
      insert_sorted_unique(checked_, x);
    }

    for (auto x : other.not_checked_) {
      if (contains_sorted(checked_, x)) {
        continue;
      }
      insert_sorted_unique(not_checked_, x);
    }
  }

  size_t size() const {
    return checked_.size() + not_checked_.size();
  }

  bool empty() const {
    return size() == 0;
  }

 private:
  static bool contains_sorted(const vector<T> &values, const T &value) {
    auto it = std::lower_bound(values.begin(), values.end(), value);
    return it != values.end() && *it == value;
  }

  static bool insert_sorted_unique(vector<T> &values, T value) {
    auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it != values.end() && *it == value) {
      return false;
    }
    values.insert(it, std::move(value));
    return true;
  }

  static bool erase_sorted(vector<T> &values, const T &value) {
    auto it = std::lower_bound(values.begin(), values.end(), value);
    if (it == values.end() || *it != value) {
      return false;
    }
    values.erase(it);
    return true;
  }

  static void merge_sorted_unique(vector<T> &target, const vector<T> &source) {
    if (source.empty()) {
      return;
    }
    if (target.empty()) {
      target = source;
      return;
    }
    vector<T> merged;
    merged.reserve(target.size() + source.size());
    auto it = target.begin();
    auto jt = source.begin();
    while (it != target.end() && jt != source.end()) {
      if (*it < *jt) {
        merged.push_back(*it++);
      } else if (*jt < *it) {
        merged.push_back(*jt++);
      } else {
        merged.push_back(*it++);
        ++jt;
      }
    }
    merged.insert(merged.end(), it, target.end());
    merged.insert(merged.end(), jt, source.end());
    target = std::move(merged);
  }

  vector<T> checked_;
  vector<T> not_checked_;
};

template <class T>
class SetWithPosition {
 public:
  std::vector<T> get_some_elements() const {
    if (fast_) {
      return fast_->get_some_elements();
    }
    if (has_value_) {
      return {value_};
    }
    return {};
  }

  bool add(T x) {
    if (fast_) {
      return fast_->add(x);
    }
    if (!has_value_) {
      value_ = x;
      has_value_ = true;
      is_checked_ = false;
      return true;
    }
    if (value_ == x) {
      return false;
    }
    make_fast();
    return fast_->add(x);
  }

  bool remove(T x) {
    if (fast_) {
      return fast_->remove(x);
    }
    if (has_value_ && value_ == x) {
      has_value_ = false;
      is_checked_ = false;
      return true;
    }
    return false;
  }

  bool has_next() const {
    if (fast_) {
      return fast_->has_next();
    }
    return has_value_ && !is_checked_;
  }

  void reset_position() {
    if (fast_) {
      fast_->reset_position();
      return;
    }
    is_checked_ = false;
  }

  T next() {
    CHECK(has_next());
    if (fast_) {
      return fast_->next();
    }
    is_checked_ = true;
    return value_;
  }

  void merge(SetWithPosition &&other) {
    if (this == &other) {
      return;
    }
    if (size() < other.size()) {
      std::swap(*this, other);
    }
    if (other.size() == 0) {
      return;
    }
    if (other.fast_ == nullptr && fast_ == nullptr && value_ == other.value_) {
      is_checked_ |= other.is_checked_;
      other.value_ = T();
      other.has_value_ = false;
      other.is_checked_ = false;
      return;
    }
    make_fast();
    other.make_fast();
    fast_->merge(std::move(*other.fast_));
    reset_to_empty(other);
  }

  size_t size() const {
    if (fast_) {
      return fast_->size();
    }
    return static_cast<size_t>(has_value_);
  }

  bool empty() const {
    if (fast_) {
      return false;
    }
    return !has_value_;
  }

 private:
  T value_{};
  bool has_value_{false};
  bool is_checked_{false};
  unique_ptr<FastSetWithPosition<T>> fast_;

  void make_fast() {
    if (fast_) {
      return;
    }
    fast_ = make_unique<FastSetWithPosition<T>>();
    CHECK(has_value_);
    fast_->add(value_);
    if (is_checked_) {
      fast_->next();
    }
  }
};

}  // namespace td
