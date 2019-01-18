//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

namespace td {
template <class T>
class SetWithPosition {
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
  void merge(SetWithPosition &&other) {
    SetWithPosition res;
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
