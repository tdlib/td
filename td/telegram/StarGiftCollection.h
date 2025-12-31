//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/StarGiftCollectionId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class StarGiftCollection {
  StarGiftCollectionId collection_id_;
  string title_;
  FileId icon_file_id_;
  int32 gift_count_ = 0;
  int64 hash_ = 0;

  friend bool operator==(const StarGiftCollection &lhs, const StarGiftCollection &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftCollection &gift_collection);

 public:
  StarGiftCollection() = default;

  StarGiftCollection(Td *td, telegram_api::object_ptr<telegram_api::starGiftCollection> &&gift_collection);

  int64 get_hash() const {
    return hash_;
  }

  td_api::object_ptr<td_api::giftCollection> get_gift_collection_object(Td *td) const;
};

bool operator==(const StarGiftCollection &lhs, const StarGiftCollection &rhs);

inline bool operator!=(const StarGiftCollection &lhs, const StarGiftCollection &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftCollection &gift_collection);

}  // namespace td
