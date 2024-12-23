//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class StarGiftAttributeSticker {
  string name_;
  FileId sticker_file_id_;
  int32 rarity_permille_ = 0;

  friend bool operator==(const StarGiftAttributeSticker &lhs, const StarGiftAttributeSticker &rhs);

 public:
  StarGiftAttributeSticker() = default;

  StarGiftAttributeSticker(Td *td, telegram_api::object_ptr<telegram_api::starGiftAttributeModel> &&attribute);

  StarGiftAttributeSticker(Td *td, telegram_api::object_ptr<telegram_api::starGiftAttributePattern> &&attribute);

  bool is_valid() const {
    return 0 < rarity_permille_ && rarity_permille_ <= 1000 && sticker_file_id_.is_valid();
  }

  td_api::object_ptr<td_api::upgradedGiftModel> get_upgraded_gift_model_object(const Td *td) const;

  td_api::object_ptr<td_api::upgradedGiftPatternEmoji> get_upgraded_gift_pattern_emoji_object(const Td *td) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarGiftAttributeSticker &lhs, const StarGiftAttributeSticker &rhs);

inline bool operator!=(const StarGiftAttributeSticker &lhs, const StarGiftAttributeSticker &rhs) {
  return !(lhs == rhs);
}

class StarGiftAttributeBackground {
  string name_;
  int32 center_color_ = 0;
  int32 edge_color_ = 0;
  int32 pattern_color_ = 0;
  int32 text_color_ = 0;
  int32 rarity_permille_ = 0;

  friend bool operator==(const StarGiftAttributeBackground &lhs, const StarGiftAttributeBackground &rhs);

  bool is_valid_color(int32 color) const {
    return 0 <= color && color <= 0xFFFFFF;
  }

 public:
  StarGiftAttributeBackground() = default;

  explicit StarGiftAttributeBackground(telegram_api::object_ptr<telegram_api::starGiftAttributeBackdrop> &&attribute);

  bool is_valid() const {
    return 0 < rarity_permille_ && rarity_permille_ <= 1000 && is_valid_color(center_color_) &&
           is_valid_color(edge_color_) && is_valid_color(pattern_color_) && is_valid_color(text_color_);
  }

  td_api::object_ptr<td_api::upgradedGiftBackground> get_upgraded_gift_background_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarGiftAttributeBackground &lhs, const StarGiftAttributeBackground &rhs);

inline bool operator!=(const StarGiftAttributeBackground &lhs, const StarGiftAttributeBackground &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
