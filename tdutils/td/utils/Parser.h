//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <cstring>
#include <utility>

namespace td {

class Parser {
 public:
  explicit Parser(MutableSlice data) : ptr_(data.begin()), end_(data.end()), status_() {
  }
  Parser(Parser &&other) : ptr_(other.ptr_), end_(other.end_), status_(std::move(other.status_)) {
    other.clear();
  }
  Parser &operator=(Parser &&other) {
    if (&other == this) {
      return *this;
    }
    ptr_ = other.ptr_;
    end_ = other.end_;
    status_ = std::move(other.status_);
    other.clear();
    return *this;
  }
  Parser(const Parser &) = delete;
  Parser &operator=(const Parser &) = delete;
  ~Parser() = default;

  bool empty() const {
    return ptr_ == end_;
  }
  void clear() {
    ptr_ = nullptr;
    end_ = ptr_;
    status_ = Status::OK();
  }

  MutableSlice read_till_nofail(char c) {
    if (status_.is_error()) {
      return MutableSlice();
    }
    char *till = reinterpret_cast<char *>(std::memchr(ptr_, c, end_ - ptr_));
    if (till == nullptr) {
      till = end_;
    }
    MutableSlice result(ptr_, till);
    ptr_ = till;
    return result;
  }

  MutableSlice read_till_nofail(Slice str) {
    if (status_.is_error()) {
      return MutableSlice();
    }
    char *best_till = end_;
    for (auto c : str) {
      char *till = reinterpret_cast<char *>(std::memchr(ptr_, c, end_ - ptr_));
      if (till != nullptr && till < best_till) {
        best_till = till;
      }
    }
    MutableSlice result(ptr_, best_till);
    ptr_ = best_till;
    return result;
  }

  template <class F>
  MutableSlice read_while(const F &f) {
    auto save_ptr = ptr_;
    while (ptr_ != end_ && f(*ptr_)) {
      ptr_++;
    }
    return MutableSlice(save_ptr, ptr_);
  }
  MutableSlice read_all() {
    auto save_ptr = ptr_;
    ptr_ = end_;
    return MutableSlice(save_ptr, ptr_);
  }

  MutableSlice read_till(char c) {
    if (status_.is_error()) {
      return MutableSlice();
    }
    MutableSlice res = read_till_nofail(c);
    if (ptr_ == end_ || ptr_[0] != c) {
      status_ = Status::Error(PSLICE() << "Read till " << tag("char", c) << " failed");
      return MutableSlice();
    }
    return res;
  }

  char peek_char() {
    if (ptr_ == end_) {
      return 0;
    }
    return *ptr_;
  }

  char *ptr() {
    return ptr_;
  }

  void skip_nofail(char c) {
    if (ptr_ != end_ && ptr_[0] == c) {
      ptr_++;
    }
  }
  void skip(char c) {
    if (status_.is_error()) {
      return;
    }
    if (ptr_ == end_ || ptr_[0] != c) {
      status_ = Status::Error(PSLICE() << "Skip " << tag("char", c) << " failed");
      return;
    }
    ptr_++;
  }
  bool try_skip(char c) {
    if (ptr_ != end_ && ptr_[0] == c) {
      ptr_++;
      return true;
    }
    return false;
  }

  void skip_till_not(Slice str) {
    while (ptr_ != end_) {
      if (std::memchr(str.data(), *ptr_, str.size()) == nullptr) {
        break;
      }
      ptr_++;
    }
  }
  void skip_whitespaces() {
    skip_till_not(" \t\r\n");
  }

  MutableSlice data() const {
    return MutableSlice(ptr_, end_);
  }

  Status &status() {
    return status_;
  }

  bool start_with(Slice prefix) {
    if (prefix.size() + ptr_ > end_) {
      return false;
    }
    return std::memcmp(prefix.begin(), ptr_, prefix.size()) == 0;
  }

  bool skip_start_with(Slice prefix) {
    if (start_with(prefix)) {
      advance(prefix.size());
      return true;
    }
    return false;
  }

  void advance(size_t diff) {
    ptr_ += diff;
    CHECK(ptr_ <= end_);
  }

 private:
  char *ptr_;
  char *end_;
  Status status_;
};
}  // namespace td
