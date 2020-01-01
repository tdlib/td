//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/StorerBase.h"
#include "td/utils/tl_storers.h"

#include <limits>

namespace td {
namespace mtproto {

template <class Impl>
class PacketStorer
    : public Storer
    , public Impl {
 public:
  size_t size() const override {
    if (size_ != std::numeric_limits<size_t>::max()) {
      return size_;
    }
    TlStorerCalcLength storer;
    this->do_store(storer);
    return size_ = storer.get_length();
  }

  size_t store(uint8 *ptr) const override {
    TlStorerUnsafe storer(ptr);
    this->do_store(storer);
    return static_cast<size_t>(storer.get_buf() - ptr);
  }

  using Impl::Impl;

 private:
  mutable size_t size_ = std::numeric_limits<size_t>::max();
};

}  // namespace mtproto
}  // namespace td
