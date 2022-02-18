//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/FlatHashMapChunks.h"
#include "td/utils/FlatHashMapLinear.h"

//#include <unordered_map>
//#include <unordered_set>

namespace td {
template <class KeyT, class ValueT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
//using FlatHashMap = FlatHashMapImpl<KeyT, ValueT, HashT, EqT>;
using FlatHashMap = FlatHashMapChunks<KeyT, ValueT, HashT, EqT>;
//using FlatHashMap = std::unordered_map<KeyT, ValueT, HashT, EqT>;

template <class KeyT, class HashT = std::hash<KeyT>, class EqT = std::equal_to<KeyT>>
//using FlatHashSet = FlatHashSetImpl<KeyT, HashT, EqT>;
using FlatHashSet = FlatHashSetChunks<KeyT, HashT, EqT>;
//using FlatHashSet = std::unordered_set<KeyT, HashT, EqT>;

}  // namespace td
