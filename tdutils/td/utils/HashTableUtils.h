//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"

namespace td {

template <class KeyT>
bool is_hash_table_key_empty(const KeyT &key) {
  return key == KeyT();
}

inline uint32 randomize_hash(size_t h) {
  auto result = static_cast<uint32>(h & 0xFFFFFFFF);
  result ^= result >> 16;
  result *= 0x85ebca6b;
  result ^= result >> 13;
  result *= 0xc2b2ae35;
  result ^= result >> 16;
  return result;
}

}  // namespace td
