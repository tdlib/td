//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftCollection.h"

#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

StarGiftCollection::StarGiftCollection(Td *td,
                                       telegram_api::object_ptr<telegram_api::starGiftCollection> &&gift_collection) {
  CHECK(gift_collection != nullptr);
  collection_id_ = StarGiftCollectionId(gift_collection->collection_id_);
  if (!collection_id_.is_valid()) {
    LOG(ERROR) << "Receive " << collection_id_;
    collection_id_ = {};
    return;
  }
  title_ = std::move(gift_collection->title_);
  icon_file_id_ =
      td->stickers_manager_
          ->on_get_sticker_document(std::move(gift_collection->icon_), StickerFormat::Unknown, "StarGiftCollection")
          .second;
  gift_count_ = max(0, gift_collection->gifts_count_);
  hash_ = gift_collection->hash_;
}

td_api::object_ptr<td_api::giftCollection> StarGiftCollection::get_gift_collection_object(Td *td) const {
  return td_api::make_object<td_api::giftCollection>(
      collection_id_.get(), title_, td->stickers_manager_->get_sticker_object(icon_file_id_), gift_count_);
}

bool operator==(const StarGiftCollection &lhs, const StarGiftCollection &rhs) {
  return lhs.collection_id_ == rhs.collection_id_ && lhs.title_ == rhs.title_ &&
         lhs.icon_file_id_ == rhs.icon_file_id_ && lhs.gift_count_ == rhs.gift_count_ && lhs.hash_ == rhs.hash_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGiftCollection &gift_collection) {
  return string_builder << gift_collection.collection_id_ << ' ' << gift_collection.title_ << " with "
                        << gift_collection.gift_count_ << " gifts";
}

}  // namespace td
