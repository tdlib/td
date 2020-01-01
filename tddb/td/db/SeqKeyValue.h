//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/Slice.h"

#include <unordered_map>

namespace td {

class SeqKeyValue {
 public:
  using SeqNo = uint64;
  SeqKeyValue() = default;
  SeqKeyValue(SeqKeyValue &&) = default;
  SeqKeyValue &operator=(SeqKeyValue &&) = default;
  SeqKeyValue(const SeqKeyValue &) = delete;
  SeqKeyValue &operator=(const SeqKeyValue &) = delete;
  ~SeqKeyValue() = default;

  SeqNo set(Slice key, Slice value) {
    auto it_ok = map_.insert({key.str(), value.str()});
    if (!it_ok.second) {
      if (it_ok.first->second == value) {
        return 0;
      }
      it_ok.first->second = value.str();
    }
    return next_seq_no();
  }
  SeqNo erase(const string &key) {
    auto it = map_.find(key);
    if (it == map_.end()) {
      return 0;
    }
    map_.erase(it);
    return next_seq_no();
  }
  SeqNo seq_no() const {
    return current_id_ + 1;
  }
  string get(const string &key) const {
    auto it = map_.find(key);
    if (it == map_.end()) {
      return string();
    }
    return it->second;
  }

  template <class F>
  void foreach(const F &f) {
    for (auto &it : map_) {
      f(it.first, it.second);
    }
  }

  size_t size() const {
    return map_.size();
  }

  void reset_seq_no() {
    current_id_ = 0;
  }
  std::unordered_map<string, string> get_all() const {
    return map_;
  }

 private:
  std::unordered_map<string, string> map_;
  SeqNo current_id_ = 0;
  SeqNo next_seq_no() {
    return ++current_id_;
  }
};

}  // namespace td
