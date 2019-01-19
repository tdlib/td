//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#include "td/utils/common.h"

#include <set>

namespace td {
template <class T>
class FastSetWithPosition {
 public:
  void add(int x) {
    if (checked_.count(x) != 0) {
      return;
    }
    not_checked_.insert(x);
  }
  void remove(int x) {
    checked_.erase(x);
    not_checked_.erase(x);
  }
  bool has_next() {
    return !not_checked_.empty();
  }
  void reset_position() {
    not_checked_.insert(checked_.begin(), checked_.end());
    checked_ = {};
  }

  T next() {
    CHECK(has_next());
    auto res = *not_checked_.begin();
    not_checked_.erase(not_checked_.begin());
    checked_.insert(res);
    return res;
  }

  void merge(FastSetWithPosition &&other) {
    if (size() < other.size()) {
      std::swap(*this, other);
    }
    for (auto x : other.checked_) {
      not_checked_.erase(x);
      checked_.insert(x);
    }
    for (auto x : other.not_checked_) {
      if (checked_.count(x) != 0) {
        continue;
      }
      not_checked_.insert(x);
    }
  }
  size_t size() const {
    return checked_.size() + not_checked_.size();
  }

 private:
  std::set<T> checked_;
  std::set<T> not_checked_;
};

template <class T>
class SetWithPosition {
 public:
  void add(int x) {
    if (fast_) {
      fast_->add(x);
      return;
    }
    if (!has_value_) {
      value_ = x;
      has_value_ = true;
      is_cheched_ = false;
      return;
    }
    if (value_ == x) {
      return;
    }
    make_fast();
    fast_->add(x);
  }
  void remove(int x) {
    if (fast_) {
      fast_->remove(x);
      return;
    }
    if (has_value_ && value_ == x) {
      has_value_ = false;
      is_cheched_ = false;
    }
  }
  bool has_next() {
    if (fast_) {
      return fast_->has_next();
    }
    return has_value_ && !is_cheched_;
  }
  void reset_position() {
    if (fast_) {
      fast_->reset_position();
      return;
    }
    is_cheched_ = false;
  }

  T next() {
    CHECK(has_next());
    if (fast_) {
      return fast_->next();
    }
    is_cheched_ = true;
    return value_;
  }

  void merge(SetWithPosition &&other) {
    if (size() < other.size()) {
      std::swap(*this, other);
    }
    if (other.size() == 0) {
      return;
    }
    make_fast();
    other.make_fast();
    fast_->merge(std::move(*other.fast_));
  }
  size_t size() const {
    if (fast_) {
      return fast_->size();
    }
    return has_value_;
  }

 private:
  T value_;
  bool has_value_{false};
  bool is_cheched_{false};
  unique_ptr<FastSetWithPosition<T>> fast_;
  void make_fast() {
    if (fast_) {
      return;
    }
    fast_ = make_unique<FastSetWithPosition<T>>();
    CHECK(has_value_);
    fast_->add(value_);
    if (is_cheched_) {
      fast_->next();
    }
  }
};
template <class T>
class OldSetWithPosition {
 public:
  void add(T value) {
    auto it = std::find(values_.begin(), values_.end(), value);
    if (it != end(values_)) {
      return;
    }
    values_.push_back(value);
  }
  void remove(T value) {
    auto it = std::find(values_.begin(), values_.end(), value);
    if (it == end(values_)) {
      return;
    }
    size_t i = it - values_.begin();
    values_.erase(it);
    if (pos_ > i) {
      pos_--;
    }
  }
  void reset_position() {
    pos_ = 0;
  }
  T next() {
    return values_[pos_++];
  }
  bool has_next() {
    return pos_ < values_.size();
  }
  void merge(OldSetWithPosition &&other) {
    OldSetWithPosition res;
    for (size_t i = 0; i < pos_; i++) {
      res.add(values_[i]);
    }
    for (size_t i = 0; i < other.pos_; i++) {
      res.add(other.values_[i]);
    }
    res.pos_ = res.values_.size();
    for (size_t i = pos_; i < values_.size(); i++) {
      res.add(values_[i]);
    }
    for (size_t i = other.pos_; i < other.values_.size(); i++) {
      res.add(other.values_[i]);
    }
    *this = std::move(res);
  }

 private:
  std::vector<T> values_;
  size_t pos_{0};
};
}  // namespace td
