//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/UserStarGift.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StarGiftManager.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

UserStarGift::UserStarGift(Td *td, telegram_api::object_ptr<telegram_api::savedStarGift> &&gift, DialogId dialog_id)
    : gift_(td, std::move(gift->gift_), true)
    , message_(get_formatted_text(td->user_manager_.get(), std::move(gift->message_), true, false, "userStarGift"))
    , prepaid_upgrade_hash_(std::move(gift->prepaid_upgrade_hash_))
    , convert_star_count_(StarManager::get_star_count(gift->convert_stars_))
    , upgrade_star_count_(StarManager::get_star_count(gift->upgrade_stars_))
    , transfer_star_count_(StarManager::get_star_count(gift->transfer_stars_))
    , drop_original_details_star_count_(StarManager::get_star_count(gift->drop_original_details_stars_))
    , date_(gift->date_)
    , can_transfer_at_(max(0, gift->can_transfer_at_))
    , can_resell_at_(max(0, gift->can_resell_at_))
    , can_export_at_(max(0, gift->can_export_at_))
    , gift_num_(max(0, gift->gift_num_))
    , can_craft_at_(max(0, gift->can_craft_at_))
    , is_name_hidden_(gift->name_hidden_)
    , is_saved_(!gift->unsaved_)
    , is_pinned_(gift->pinned_to_top_)
    , can_upgrade_(gift->can_upgrade_)
    , can_transfer_((gift->flags_ & telegram_api::savedStarGift::TRANSFER_STARS_MASK) != 0)
    , was_refunded_(gift->refunded_)
    , is_upgrade_separate_(gift->upgrade_separate_) {
  if (gift->from_id_ != nullptr) {
    sender_dialog_id_ = DialogId(gift->from_id_);
    if (!sender_dialog_id_.is_valid()) {
      LOG(ERROR) << "Receive " << sender_dialog_id_ << " as sender of " << gift_;
      sender_dialog_id_ = DialogId();
    }
  }
  for (auto star_gift_collection_id : gift->collection_id_) {
    StarGiftCollectionId collection_id(star_gift_collection_id);
    if (collection_id.is_valid()) {
      collection_ids_.push_back(collection_id);
    } else {
      LOG(ERROR) << "Receive " << collection_id << " for " << gift_;
    }
  }
  auto is_user = dialog_id.get_type() == DialogType::User;
  auto is_me = is_user && dialog_id == td->dialog_manager_->get_my_dialog_id();
  if (is_user) {
    if (gift->msg_id_ != 0) {
      star_gift_id_ = StarGiftId(ServerMessageId(gift->msg_id_));
    }
  } else {
    if (gift->saved_id_ != 0) {
      star_gift_id_ = StarGiftId(dialog_id, gift->saved_id_);
    }
  }
  if (sender_dialog_id_ != DialogId() && !sender_dialog_id_.is_valid()) {
    LOG(ERROR) << "Receive " << sender_dialog_id_ << " as sender of " << gift_;
    sender_dialog_id_ = DialogId();
  }
  if (!is_saved_ && is_user && !is_me && !td->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive non-saved " << gift_ << " for " << dialog_id;
    is_saved_ = true;
  }
  td->star_gift_manager_->on_get_star_gift(gift_, true);
}

td_api::object_ptr<td_api::receivedGift> UserStarGift::get_received_gift_object(Td *td) const {
  auto collection_ids = transform(collection_ids_, [](auto collection_id) { return collection_id.get(); });
  return td_api::make_object<td_api::receivedGift>(
      star_gift_id_.get_star_gift_id(),
      sender_dialog_id_ == DialogId() ? nullptr : get_message_sender_object(td, sender_dialog_id_, "receivedGift"),
      get_formatted_text_object(td->user_manager_.get(), message_, true, -1), gift_num_, is_name_hidden_, is_saved_,
      is_pinned_, can_upgrade_, can_transfer_, was_refunded_, date_, gift_.get_sent_gift_object(td),
      std::move(collection_ids), convert_star_count_, upgrade_star_count_,
      upgrade_star_count_ > 0 && is_upgrade_separate_, transfer_star_count_, drop_original_details_star_count_,
      can_transfer_at_, can_resell_at_, can_export_at_, prepaid_upgrade_hash_, can_craft_at_);
}

}  // namespace td
