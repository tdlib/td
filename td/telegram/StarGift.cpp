//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGift.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/StarGiftId.h"
#include "td/telegram/StarGiftResalePrice.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

void StarGift::fix_availability(int32 &total, int32 &remains) {
  if (total < 0) {
    LOG(ERROR) << "Receive " << total << " total available gifts";
    total = 0;
  }
  if ((total != 0 || remains != 0) && (remains < 0 || remains > total)) {
    LOG(ERROR) << "Receive " << remains << " remained available gifts out of " << total;
    if (remains < 0) {
      remains = 0;
    } else {
      remains = total;
    }
  }
}

StarGift::StarGift(Td *td, telegram_api::object_ptr<telegram_api::StarGift> &&star_gift_ptr, bool allow_unique_gift) {
  CHECK(star_gift_ptr != nullptr);
  auto constructor_id = star_gift_ptr->get_id();
  if (allow_unique_gift && constructor_id == telegram_api::starGiftUnique::ID) {
    auto star_gift = telegram_api::move_object_as<telegram_api::starGiftUnique>(star_gift_ptr);
    if (star_gift->id_ == 0) {
      LOG(ERROR) << "Receive " << to_string(star_gift);
      return;
    }
    is_unique_ = true;
    id_ = star_gift->id_;
    title_ = std::move(star_gift->title_);
    slug_ = std::move(star_gift->slug_);
    num_ = star_gift->num_;
    if (star_gift->owner_id_ != nullptr) {
      owner_dialog_id_ = DialogId(star_gift->owner_id_);
    }
    owner_name_ = std::move(star_gift->owner_name_);
    owner_address_ = std::move(star_gift->owner_address_);
    gift_address_ = std::move(star_gift->gift_address_);
    unique_availability_issued_ = star_gift->availability_issued_;
    unique_availability_total_ = star_gift->availability_total_;
    if (!star_gift->resell_amount_.empty()) {
      if (star_gift->resell_amount_.size() < 2u ||
          star_gift->resell_amount_[0]->get_id() != telegram_api::starsAmount::ID ||
          star_gift->resell_amount_[1]->get_id() != telegram_api::starsTonAmount::ID) {
        LOG(ERROR) << "Receive unsupported resale amount";
      } else {
        resale_star_count_ = StarGiftResalePrice(std::move(star_gift->resell_amount_[0])).get_star_count();
        resale_ton_count_ = StarGiftResalePrice(std::move(star_gift->resell_amount_[1])).get_ton_count();
        resale_ton_only_ = star_gift->resale_ton_only_;
      }
    }
    if (star_gift->released_by_ != nullptr) {
      released_by_dialog_id_ = DialogId(star_gift->released_by_);
      td->dialog_manager_->force_create_dialog(released_by_dialog_id_, "StarGift", true);
    }
    is_premium_ = star_gift->require_premium_;
    for (auto &attribute : star_gift->attributes_) {
      switch (attribute->get_id()) {
        case telegram_api::starGiftAttributeModel::ID:
          if (model_.is_valid()) {
            LOG(ERROR) << "Receive duplicate model for " << *this;
          }
          model_ = StarGiftAttributeSticker(
              td, telegram_api::move_object_as<telegram_api::starGiftAttributeModel>(attribute));
          if (!model_.is_valid()) {
            LOG(ERROR) << "Receive invalid model for " << *this;
          }
          break;
        case telegram_api::starGiftAttributePattern::ID:
          if (pattern_.is_valid()) {
            LOG(ERROR) << "Receive duplicate symbol for " << *this;
          }
          pattern_ = StarGiftAttributeSticker(
              td, telegram_api::move_object_as<telegram_api::starGiftAttributePattern>(attribute));
          if (!pattern_.is_valid()) {
            LOG(ERROR) << "Receive invalid symbol for " << *this;
          }
          break;
        case telegram_api::starGiftAttributeBackdrop::ID:
          if (backdrop_.is_valid()) {
            LOG(ERROR) << "Receive duplicate backdrop for " << *this;
          }
          backdrop_ = StarGiftAttributeBackdrop(
              telegram_api::move_object_as<telegram_api::starGiftAttributeBackdrop>(attribute));
          if (!backdrop_.is_valid()) {
            LOG(ERROR) << "Receive invalid backdrop for " << *this;
          }
          break;
        case telegram_api::starGiftAttributeOriginalDetails::ID:
          if (original_details_.is_valid()) {
            LOG(ERROR) << "Receive duplicate original details for " << *this;
          }
          original_details_ = StarGiftAttributeOriginalDetails(
              td, telegram_api::move_object_as<telegram_api::starGiftAttributeOriginalDetails>(attribute));
          if (!original_details_.is_valid()) {
            LOG(ERROR) << "Receive invalid original details for " << *this;
          }
          break;
        default:
          UNREACHABLE();
      }
    }
    return;
  }
  if (constructor_id != telegram_api::starGift::ID) {
    LOG(ERROR) << "Receive " << to_string(star_gift_ptr);
    return;
  }
  auto star_gift = telegram_api::move_object_as<telegram_api::starGift>(star_gift_ptr);
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
  fix_availability(star_gift->availability_total_, star_gift->availability_remains_);
  if (star_gift->availability_remains_ == 0 && star_gift->availability_total_ > 0) {
    first_sale_date_ = max(0, star_gift->first_sale_date_);
    last_sale_date_ = max(first_sale_date_, star_gift->last_sale_date_);
  }
  fix_availability(star_gift->per_user_total_, star_gift->per_user_remains_);

  id_ = star_gift->id_;
  star_count_ = StarManager::get_star_count(star_gift->stars_);
  default_sell_star_count_ = StarManager::get_star_count(star_gift->convert_stars_);
  upgrade_star_count_ = StarManager::get_star_count(star_gift->upgrade_stars_);
  sticker_file_id_ = sticker_id;
  availability_remains_ = star_gift->availability_remains_;
  availability_total_ = star_gift->availability_total_;
  per_user_remains_ = star_gift->per_user_remains_;
  per_user_total_ = star_gift->per_user_total_;
  is_for_birthday_ = star_gift->birthday_;
  if (star_gift->released_by_ != nullptr) {
    released_by_dialog_id_ = DialogId(star_gift->released_by_);
    td->dialog_manager_->force_create_dialog(released_by_dialog_id_, "StarGift", true);
  }
  is_premium_ = star_gift->require_premium_;
}

td_api::object_ptr<td_api::giftPurchaseLimits> StarGift::get_gift_purchase_limits_object(int32 total, int32 remains) {
  if (total <= 0) {
    return nullptr;
  }
  return td_api::make_object<td_api::giftPurchaseLimits>(total, remains);
}

td_api::object_ptr<td_api::gift> StarGift::get_gift_object(const Td *td) const {
  CHECK(is_valid());
  CHECK(!is_unique_);
  return td_api::make_object<td_api::gift>(id_, td->dialog_manager_->get_chat_id_object(released_by_dialog_id_, "gift"),
                                           td->stickers_manager_->get_sticker_object(sticker_file_id_), star_count_,
                                           default_sell_star_count_, upgrade_star_count_, is_for_birthday_, is_premium_,
                                           get_gift_purchase_limits_object(per_user_total_, per_user_remains_),
                                           get_gift_purchase_limits_object(availability_total_, availability_remains_),
                                           first_sale_date_, last_sale_date_);
}

td_api::object_ptr<td_api::upgradedGift> StarGift::get_upgraded_gift_object(Td *td) const {
  CHECK(is_valid());
  CHECK(is_unique_);
  td_api::object_ptr<td_api::giftResaleParameters> resale_parameters;
  if (resale_star_count_ > 0 && resale_ton_count_ > 0) {
    resale_parameters = td_api::make_object<td_api::giftResaleParameters>(
        resale_star_count_, resale_ton_count_ / 10000000, resale_ton_only_);
  }
  return td_api::make_object<td_api::upgradedGift>(
      id_, td->dialog_manager_->get_chat_id_object(released_by_dialog_id_, "upgradedGift"), title_, slug_, num_,
      unique_availability_issued_, unique_availability_total_, is_premium_,
      !owner_dialog_id_.is_valid() ? nullptr : get_message_sender_object(td, owner_dialog_id_, "upgradedGift"),
      owner_address_, owner_name_, gift_address_, model_.get_upgraded_gift_model_object(td),
      pattern_.get_upgraded_gift_symbol_object(td), backdrop_.get_upgraded_gift_backdrop_object(),
      original_details_.get_upgraded_gift_original_details_object(td), std::move(resale_parameters));
}

td_api::object_ptr<td_api::giftForResale> StarGift::get_gift_for_resale_object(Td *td) const {
  CHECK(is_valid());
  CHECK(is_unique_);
  return td_api::make_object<td_api::giftForResale>(get_upgraded_gift_object(td),
                                                    owner_dialog_id_ == td->dialog_manager_->get_my_dialog_id()
                                                        ? StarGiftId::from_slug(slug_).get_star_gift_id()
                                                        : string());
}

td_api::object_ptr<td_api::SentGift> StarGift::get_sent_gift_object(Td *td) const {
  if (is_unique_) {
    return td_api::make_object<td_api::sentGiftUpgraded>(get_upgraded_gift_object(td));
  } else {
    return td_api::make_object<td_api::sentGiftRegular>(get_gift_object(td));
  }
}

void StarGift::add_dependencies(Dependencies &dependencies) const {
  dependencies.add_message_sender_dependencies(owner_dialog_id_);
  original_details_.add_dependencies(dependencies);
  dependencies.add_dialog_and_dependencies(released_by_dialog_id_);
}

bool operator==(const StarGift &lhs, const StarGift &rhs) {
  return lhs.id_ == rhs.id_ && lhs.sticker_file_id_ == rhs.sticker_file_id_ && lhs.star_count_ == rhs.star_count_ &&
         lhs.default_sell_star_count_ == rhs.default_sell_star_count_ &&
         lhs.upgrade_star_count_ == rhs.upgrade_star_count_ && lhs.availability_remains_ == rhs.availability_remains_ &&
         lhs.availability_total_ == rhs.availability_total_ && lhs.first_sale_date_ == rhs.first_sale_date_ &&
         lhs.last_sale_date_ == rhs.last_sale_date_ && lhs.is_for_birthday_ == rhs.is_for_birthday_ &&
         lhs.is_unique_ == rhs.is_unique_ && lhs.model_ == rhs.model_ && lhs.pattern_ == rhs.pattern_ &&
         lhs.backdrop_ == rhs.backdrop_ && lhs.original_details_ == rhs.original_details_ && lhs.title_ == rhs.title_ &&
         lhs.slug_ == rhs.slug_ && lhs.owner_dialog_id_ == rhs.owner_dialog_id_ &&
         lhs.owner_address_ == rhs.owner_address_ && lhs.owner_name_ == rhs.owner_name_ &&
         lhs.gift_address_ == rhs.gift_address_ && lhs.num_ == rhs.num_ &&
         lhs.unique_availability_issued_ == rhs.unique_availability_issued_ &&
         lhs.unique_availability_total_ == rhs.unique_availability_total_ &&
         lhs.resale_star_count_ == rhs.resale_star_count_ && lhs.released_by_dialog_id_ == rhs.released_by_dialog_id_ &&
         lhs.is_premium_ == rhs.is_premium_ && lhs.per_user_remains_ == rhs.per_user_remains_ &&
         lhs.per_user_total_ == rhs.per_user_total_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StarGift &star_gift) {
  return string_builder << "Gift[" << star_gift.id_ << " for " << star_gift.star_count_ << ']';
}

}  // namespace td
