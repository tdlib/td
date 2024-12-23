//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftAttribute.h"

#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

namespace td {

StarGiftAttributeSticker::StarGiftAttributeSticker(
    Td *td, telegram_api::object_ptr<telegram_api::starGiftAttributeModel> &&attribute)
    : name_(std::move(attribute->name_))
    , sticker_file_id_(td->stickers_manager_
                           ->on_get_sticker_document(std::move(attribute->document_), StickerFormat::Unknown,
                                                     "StarGiftAttributeSticker")
                           .second)
    , rarity_permille_(attribute->rarity_permille_) {
}

StarGiftAttributeSticker::StarGiftAttributeSticker(
    Td *td, telegram_api::object_ptr<telegram_api::starGiftAttributePattern> &&attribute)
    : name_(std::move(attribute->name_))
    , sticker_file_id_(td->stickers_manager_
                           ->on_get_sticker_document(std::move(attribute->document_), StickerFormat::Unknown,
                                                     "StarGiftAttributeSticker")
                           .second)
    , rarity_permille_(attribute->rarity_permille_) {
}

td_api::object_ptr<td_api::upgradedGiftModel> StarGiftAttributeSticker::get_upgraded_gift_model_object(
    const Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::upgradedGiftModel>(
      name_, td->stickers_manager_->get_sticker_object(sticker_file_id_), rarity_permille_);
}

td_api::object_ptr<td_api::upgradedGiftPatternEmoji> StarGiftAttributeSticker::get_upgraded_gift_pattern_emoji_object(
    const Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::upgradedGiftPatternEmoji>(
      name_, td->stickers_manager_->get_sticker_object(sticker_file_id_), rarity_permille_);
}

bool operator==(const StarGiftAttributeSticker &lhs, const StarGiftAttributeSticker &rhs) {
  return lhs.name_ == rhs.name_ && lhs.sticker_file_id_ == rhs.sticker_file_id_ &&
         lhs.rarity_permille_ == rhs.rarity_permille_;
}

}  // namespace td
