//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <set>
#include <utility>

namespace td {

template <class ValueT>
class Enumerator {
 public:
  using Key = int32;
  template <class T>
  Key add(T &&value) {
    ValueT v = std::forward<T>(value);
    container_->set_zero_value(&v);
    auto it = set_.lower_bound(Key{0});
    container_->set_zero_value(nullptr);
    if (it != set_.end() && container_->get_value(*it) == v) {
      return *it;
    }
    auto key = container_->add_value(std::move(v));
    set_.insert(it, key);
    return key;
  }
  ValueT &get(Key key) {
    return container_->get_value(key);
  }
  const ValueT &get(Key key) const {
    return container_->get_value(key);
  }

 private:
  class Container {
   public:
    bool compare(Key a, Key b) const {
      return get_value(a) < get_value(b);
    }
    const ValueT &get_value(Key key) const {
      if (key == 0) {
        CHECK(zero_value_);
        return *zero_value_;
      }
      size_t pos = narrow_cast<size_t>(key - 1);
      CHECK(pos < values_.size());
      return values_[pos];
    }
    ValueT &get_value(Key key) {
      return const_cast<ValueT &>(const_cast<const Container *>(this)->get_value(key));
    }
    void set_zero_value(ValueT *value) {
      zero_value_ = value;
    }
    Key add_value(ValueT &&value) {
      values_.push_back(std::move(value));
      return narrow_cast<Key>(values_.size());
    }

   private:
    std::vector<ValueT> values_;
    ValueT *zero_value_ = nullptr;
  };

  class Comparator {
   public:
    explicit Comparator(Container *container) : container_(container) {
    }
    bool operator()(Key a, Key b) const {
      return container_->compare(a, b);
    }

   private:
    Container *container_;
  };

  std::unique_ptr<Container> container_{std::make_unique<Container>()};
  std::set<Key, Comparator> set_{Comparator{container_.get()}};
};

}  // namespace td
