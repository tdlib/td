//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGift.h"

#include "td/telegram/StarManager.h"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

StarGift::StarGift(Td *td, telegram_api::object_ptr<telegram_api::starGift> &&star_gift) {
  CHECK(star_gift != nullptr);
  if (star_gift->id_ == 0) {
    LOG(ERROR) << "Receive " << to_string(star_gift);
    return;
  }
  auto sticker_id =
      td->stickers_manager_->on_get_sticker_document(std::move(star_gift->sticker_), StickerFormat::Unknown, "StarGift")
          .second;
  if (!sticker_id.is_valid()) {
    return;
  }
  if (star_gift->availability_total_ < 0) {
    LOG(ERROR) << "Receive " << star_gift->availability_total_ << " total available gifts";
    star_gift->availability_total_ = 0;
  }
  if ((star_gift->availability_total_ != 0 || star_gift->availability_remains_ != 0) &&
      (star_gift->availability_remains_ < 0 || star_gift->availability_remains_ > star_gift->availability_total_)) {
    LOG(ERROR) << "Receive " << star_gift->availability_total_ << " remained available gifts out of "
               << star_gift->availability_total_;
    if (star_gift->availability_remains_ < 0) {
      return;
    }
    star_gift->availability_remains_ = star_gift->availability_total_;
  }
  if (star_gift->availability_remains_ == 0 && star_gift->availability_total_ > 0) {
    first_sale_date_ = max(0, star_gift->first_sale_date_);
    last_sale_date_ = max(first_sale_date_, star_gift->last_sale_date_);
  }

  id_ = star_gift->id_;
  star_count_ = StarManager::get_star_count(star_gift->stars_);
  default_sell_star_count_ = StarManager::get_star_count(star_gift->convert_stars_);
  sticker_file_id_ = sticker_id;
  availability_remains_ = star_gift->availability_remains_;
  availability_total_ = star_gift->availability_total_;
}

td_api::object_ptr<td_api::gift> StarGift::get_gift_object(const Td *td) const {
  CHECK(is_valid());
  return td_api::make_object<td_api::gift>(id_, td->stickers_manager_->get_sticker_object(sticker_file_id_),
                                           star_count_, default_sell_star_count_, availability_remains_,
                                           availability_total_, first_sale_date_, last_sale_date_);
}

bool operator==(const StarGift &lhs, const StarGift &rhs) {
  return lhs.id_ == rhs.id_ && lhs.sticker_file_id_ == rhs.sticker_file_id_ && lhs.star_count_ == rhs.star_count_ &&
         lhs.default_sell_star_count_ == rhs.default_sell_star_count_ &&
         lhs.availability_remains_ == rhs.availability_remains_ && lhs.availability_total_ == rhs.availability_total_ &&
         lhs.first_sale_date_ == rhs.first_sale_date_ && lhs.last_sale_date_ == rhs.last_sale_date_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGift &star_gift) {
  return string_builder << "Gift[" << star_gift.id_ << " for " << star_gift.star_count_ << ']';
}

}  // namespace td
