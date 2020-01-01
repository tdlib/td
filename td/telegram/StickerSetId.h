//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <functional>
#include <type_traits>

namespace td {

class StickerSetId {
  int64 id = 0;

 public:
  StickerSetId() = default;

  explicit constexpr StickerSetId(int64 sticker_set_id) : id(sticker_set_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int64>::value>>
  StickerSetId(T sticker_set_id) = delete;

  bool is_valid() const {
    return id != 0;
  }

  int64 get() const {
    return id;
  }

  bool operator==(const StickerSetId &other) const {
    return id == other.id;
  }

  bool operator!=(const StickerSetId &other) const {
    return id != other.id;
  }
};

struct StickerSetIdHash {
  std::size_t operator()(StickerSetId sticker_set_id) const {
    return std::hash<int64>()(sticker_set_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, StickerSetId sticker_set_id) {
  return string_builder << "sticker set " << sticker_set_id.get();
}

}  // namespace td
