//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

#include "td/utils/common.h"
#include "td/utils/Slice-decl.h"
#include "td/utils/StackAllocator.h"

namespace td {

class StringBuilder {
 public:
  explicit StringBuilder(MutableSlice slice)
      : begin_ptr_(slice.begin()), current_ptr_(begin_ptr_), end_ptr_(slice.end() - reserved_size) {
    if (slice.size() <= reserved_size) {
      std::abort();  // shouldn't happen
    }
  }

  void clear() {
    current_ptr_ = begin_ptr_;
    error_flag_ = false;
  }
  MutableCSlice as_cslice() {
    if (current_ptr_ >= end_ptr_ + reserved_size) {
      std::abort();  // shouldn't happen
    }
    *current_ptr_ = 0;
    return MutableCSlice(begin_ptr_, current_ptr_);
  }

  bool is_error() const {
    return error_flag_;
  }

  StringBuilder &operator<<(const char *str) {
    return *this << Slice(str);
  }

  StringBuilder &operator<<(Slice slice) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    auto size = static_cast<size_t>(end_ptr_ + reserved_size - 1 - current_ptr_);
    if (unlikely(slice.size() > size)) {
      error_flag_ = true;
    } else {
      size = slice.size();
    }
    std::memcpy(current_ptr_, slice.begin(), size);
    current_ptr_ += size;
    return *this;
  }

  StringBuilder &operator<<(bool b) {
    return *this << (b ? Slice("true") : Slice("false"));
  }

  StringBuilder &operator<<(char c) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    *current_ptr_++ = c;
    return *this;
  }

  StringBuilder &operator<<(unsigned char c) {
    return *this << static_cast<unsigned int>(c);
  }

  StringBuilder &operator<<(signed char c) {
    return *this << static_cast<int>(c);
  }

  // TODO: optimize
  StringBuilder &operator<<(int x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%d", x);
    return *this;
  }

  StringBuilder &operator<<(unsigned int x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%u", x);
    return *this;
  }

  StringBuilder &operator<<(long int x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%ld", x);
    return *this;
  }

  StringBuilder &operator<<(long unsigned int x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%lu", x);
    return *this;
  }

  StringBuilder &operator<<(long long int x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%lld", x);
    return *this;
  }

  StringBuilder &operator<<(long long unsigned int x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%llu", x);
    return *this;
  }

  StringBuilder &operator<<(double x) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    auto left = end_ptr_ + reserved_size - current_ptr_;
    int len = std::snprintf(current_ptr_, left, "%lf", x);
    if (unlikely(len >= left)) {
      error_flag_ = true;
      current_ptr_ += left - 1;
    } else {
      current_ptr_ += len;
    }
    return *this;
  }

  template <class T>
  StringBuilder &operator<<(const T *ptr) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      return on_error();
    }
    current_ptr_ += std::snprintf(current_ptr_, reserved_size, "%p", ptr);
    return *this;
  }

  void vprintf(const char *fmt, va_list list) {
    if (unlikely(end_ptr_ < current_ptr_)) {
      on_error();
      return;
    }

    auto left = end_ptr_ + reserved_size - current_ptr_;
    int len = std::vsnprintf(current_ptr_, left, fmt, list);
    if (unlikely(len >= left)) {
      error_flag_ = true;
      current_ptr_ += left - 1;
    } else {
      current_ptr_ += len;
    }
  }

  void printf(const char *fmt, ...) TD_ATTRIBUTE_FORMAT_PRINTF(2, 3) {
    va_list list;
    va_start(list, fmt);
    vprintf(fmt, list);
    va_end(list);
  }

 private:
  char *begin_ptr_;
  char *current_ptr_;
  char *end_ptr_;
  bool error_flag_ = false;
  static constexpr size_t reserved_size = 30;

  StringBuilder &on_error() {
    error_flag_ = true;
    return *this;
  }
};

template <class T>
std::enable_if_t<std::is_arithmetic<T>::value, string> to_string(const T &x) {
  const size_t buf_size = 1000;
  auto buf = StackAllocator::alloc(buf_size);
  StringBuilder sb(buf.as_slice());
  sb << x;
  return sb.as_cslice().str();
}

}  // namespace td
