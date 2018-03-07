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

#include <map>
#include <utility>

namespace td {

template <class ValueT>
class Enumerator {
 public:
  using Key = int32;

  Key add(ValueT v) {
    int32 next_id = narrow_cast<int32>(arr_.size() + 1);
    bool was_inserted;
    decltype(map_.begin()) it;
    std::tie(it, was_inserted) = map_.insert(std::make_pair(std::move(v), next_id));
    if (was_inserted) {
      arr_.push_back(&it->first);
    }
    return it->second;
  }

  //ValueT &get(Key key) {
  //CHECK(key != 0);
  //return *arr_[narrow_cast<size_t>(key - 1)];
  //}
  const ValueT &get(Key key) const {
    CHECK(key != 0);
    return *arr_[narrow_cast<size_t>(key - 1)];
  }

 private:
  std::map<ValueT, int32> map_;
  std::vector<const ValueT *> arr_;
};

}  // namespace td
