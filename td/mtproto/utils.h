//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/Storer.h"
#include "td/utils/StorerBase.h"
#include "td/utils/tl_storers.h"

#include <limits>

namespace td {

template <class T>
using TLStorer = DefaultStorer<T>;

template <class T>
class TLObjectStorer : public Storer {
  mutable size_t size_ = std::numeric_limits<size_t>::max();
  const T &object_;

 public:
  explicit TLObjectStorer(const T &object) : object_(object) {
  }

  size_t size() const override {
    if (size_ == std::numeric_limits<size_t>::max()) {
      TlStorerCalcLength storer;
      storer.store_binary(object_.get_id());
      object_.store(storer);
      size_ = storer.get_length();
    }
    return size_;
  }
  size_t store(uint8 *ptr) const override {
    TlStorerUnsafe storer(ptr);
    storer.store_binary(object_.get_id());
    object_.store(storer);
    return static_cast<size_t>(storer.get_buf() - ptr);
  }
};

namespace mtproto_api {
class Object;
class Function;
}  // namespace mtproto_api

TLStorer<mtproto_api::Function> create_storer(const mtproto_api::Function &function);

TLObjectStorer<mtproto_api::Object> create_storer(const mtproto_api::Object &object);

}  // namespace td
