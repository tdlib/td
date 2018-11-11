//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/files/FileBitmask.h"
#include "td/utils/misc.h"
namespace td {
Bitmask::Bitmask(Decode, Slice data) : data_(zero_one_decode(data)) {
}
Bitmask::Bitmask(Ones, int64 count) : data_((count + 7) / 8, '\0') {
  for (int64 i = 0; i < count; i++) {
    set(i);
  }
}
std::string Bitmask::encode() const {
  // remove zeroes in the end to make encoding deteministic
  td::Slice data(data_);
  while (!data.empty() && data.back() == 0) {
    data.remove_suffix(1);
  }
  return zero_one_encode(data_);
}
Bitmask::ReadySize Bitmask::get_ready_size(int64 offset, int64 part_size) const {
  ReadySize res;
  res.offset = offset;
  auto offset_part = offset / part_size;
  auto ones = get_ready_parts(offset_part);
  if (ones == 0) {
    res.ready_size = 0;
  } else {
    res.ready_size = (offset_part + ones) * part_size - offset;
  }
  CHECK(res.ready_size >= 0);
  return res;
}
int64 Bitmask::get_total_size(int64 part_size) const {
  int64 res = 0;
  for (int64 i = 0; i < size(); i++) {
    res += get(i);
  }
  return res * part_size;
}
bool Bitmask::get(int64 offset) const {
  if (offset < 0) {
    return 0;
  }
  if (offset / 8 >= narrow_cast<int64>(data_.size())) {
    return 0;
  }
  return (data_[offset / 8] & (1 << (offset % 8))) != 0;
}

int64 Bitmask::get_ready_parts(int64 offset) const {
  int64 res = 0;
  while (get(offset + res)) {
    res++;
  }
  return res;
};

std::vector<int32> Bitmask::as_vector() const {
  std::vector<int32> res;
  for (int32 i = 0; i < narrow_cast<int32>(data_.size() * 8); i++) {
    if (get(i)) {
      res.push_back(i);
    }
  }
  return res;
}
void Bitmask::set(int64 offset) {
  auto need_size = narrow_cast<size_t>(offset / 8 + 1);
  if (need_size > data_.size()) {
    data_.resize(need_size, 0);
  }
  data_[need_size - 1] |= (1 << (offset % 8));
}

int64 Bitmask::size() const {
  return data_.size() * 8;
}

}  // namespace td
