//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Bitmask {
 public:
  struct Decode {};
  struct Ones {};
  Bitmask() = default;
  Bitmask(Decode, Slice data);
  Bitmask(Ones, int64 count);
  std::string encode() const;
  int64 get_ready_prefix_size(int64 offset, int64 part_size, int64 file_size) const;
  int64 get_total_size(int64 part_size) const;
  bool get(int64 offset_part) const;

  int64 get_ready_parts(int64 offset_part) const;

  std::vector<int32> as_vector() const;
  void set(int64 offset_part);
  int64 size() const;

 private:
  std::string data_;
};

inline StringBuilder &operator<<(StringBuilder &sb, const Bitmask &mask) {
  for (int64 i = 0; i < mask.size(); i++) {
    sb << (mask.get(i) ? '1' : '0');
  }
  return sb;
}

}  // namespace td
