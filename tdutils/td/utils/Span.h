//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

#include <array>

namespace td {

namespace detail {
template <class T, class InnerT>
class SpanImpl {
  InnerT *data_{nullptr};
  size_t size_{0};

 public:
  SpanImpl() = default;
  SpanImpl(InnerT *data, size_t size) : data_(data), size_(size) {
  }
  SpanImpl(InnerT &data) : SpanImpl(&data, 1) {
  }

  template <class OtherInnerT>
  SpanImpl(const SpanImpl<T, OtherInnerT> &other) : SpanImpl(other.data(), other.size()) {
  }

  template <size_t N>
  SpanImpl(const std::array<T, N> &arr) : SpanImpl(arr.data(), arr.size()) {
  }
  template <size_t N>
  SpanImpl(std::array<T, N> &arr) : SpanImpl(arr.data(), arr.size()) {
  }
  template <size_t N>
  SpanImpl(const T (&arr)[N]) : SpanImpl(arr, N) {
  }
  template <size_t N>
  SpanImpl(T (&arr)[N]) : SpanImpl(arr, N) {
  }
  SpanImpl(const vector<T> &v) : SpanImpl(v.data(), v.size()) {
  }
  SpanImpl(vector<T> &v) : SpanImpl(v.data(), v.size()) {
  }

  template <class OtherInnerT>
  SpanImpl &operator=(const SpanImpl<T, OtherInnerT> &other) {
    SpanImpl copy{other};
    *this = copy;
  }
  template <class OtherInnerT>
  bool operator==(const SpanImpl<T, OtherInnerT> &other) const {
    if (size() != other.size()) {
      return false;
    }
    for (size_t i = 0; i < size(); i++) {
      if (!((*this)[i] == other[i])) {
        return false;
      }
    }
    return true;
  }

  InnerT &operator[](size_t i) {
    DCHECK(i < size());
    return data_[i];
  }

  const InnerT &operator[](size_t i) const {
    DCHECK(i < size());
    return data_[i];
  }

  InnerT *data() const {
    return data_;
  }
  InnerT *begin() const {
    return data_;
  }
  InnerT *end() const {
    return data_ + size_;
  }
  size_t size() const {
    return size_;
  }
  bool empty() const {
    return size() == 0;
  }

  SpanImpl &truncate(size_t size) {
    CHECK(size <= size_);
    size_ = size;
    return *this;
  }

  SpanImpl substr(size_t offset) const {
    CHECK(offset <= size_);
    return SpanImpl(begin() + offset, size_ - offset);
  }
  SpanImpl substr(size_t offset, size_t size) const {
    CHECK(offset <= size_);
    CHECK(size_ - offset >= size);
    return SpanImpl(begin() + offset, size);
  }
};
}  // namespace detail

template <class T>
using Span = detail::SpanImpl<T, const T>;

template <class T>
using MutableSpan = detail::SpanImpl<T, T>;

template <class T>
Span<T> span(const T *ptr, size_t size) {
  return Span<T>(ptr, size);
}
template <class T>
MutableSpan<T> mutable_span(T *ptr, size_t size) {
  return MutableSpan<T>(ptr, size);
}

}  // namespace td
