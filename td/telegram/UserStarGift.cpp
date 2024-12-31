//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/UserStarGift.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StarGiftManager.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/logging.h"

namespace td {

UserStarGift::UserStarGift(Td *td, telegram_api::object_ptr<telegram_api::userStarGift> &&gift, bool is_me)
    : sender_user_id_(gift->from_id_)
    , gift_(td, std::move(gift->gift_), true)
    , message_(get_formatted_text(td->user_manager_.get(), std::move(gift->message_), true, false, "userStarGift"))
    , message_id_(ServerMessageId(gift->msg_id_))
    , convert_star_count_(StarManager::get_star_count(gift->convert_stars_))
    , upgrade_star_count_(StarManager::get_star_count(gift->upgrade_stars_))
    , transfer_star_count_(StarManager::get_star_count(gift->transfer_stars_))
    , date_(gift->date_)
    , can_export_at_(max(0, gift->can_export_at_))
    , is_name_hidden_(gift->name_hidden_)
    , is_saved_(!gift->unsaved_)
    , can_upgrade_(gift->can_upgrade_)
    , can_transfer_((gift->flags_ & telegram_api::userStarGift::TRANSFER_STARS_MASK) != 0)
    , was_refunded_(gift->refunded_) {
  if (sender_user_id_ != UserId() && !sender_user_id_.is_valid()) {
    LOG(ERROR) << "Receive " << sender_user_id_ << " as sender of a gift";
    sender_user_id_ = UserId();
  }
  if (!is_saved_ && !is_me) {
    LOG(ERROR) << "Receive non-saved gift for another user";
    is_saved_ = true;
  }
  if (message_id_ != MessageId() && !message_id_.is_valid()) {
    LOG(ERROR) << "Receive " << message_id_;
    message_id_ = MessageId();
  }
  td->star_gift_manager_->on_get_star_gift(gift_, true);
  if (is_me && message_id_.is_valid() && sender_user_id_ != UserId()) {
    td->star_gift_manager_->on_get_user_star_gift({DialogId(sender_user_id_), message_id_}, can_upgrade_,
                                                  upgrade_star_count_ > 0 ? 0 : gift_.get_upgrade_star_count());
  }
}

td_api::object_ptr<td_api::userGift> UserStarGift::get_user_gift_object(Td *td) const {
  return td_api::make_object<td_api::userGift>(
      td->user_manager_->get_user_id_object(sender_user_id_, "userGift"),
      get_formatted_text_object(td->user_manager_.get(), message_, true, -1), is_name_hidden_, is_saved_, can_upgrade_,
      can_transfer_, was_refunded_, date_, gift_.get_sent_gift_object(td), message_id_.get(), convert_star_count_,
      upgrade_star_count_, transfer_star_count_, can_export_at_);
}

}  // namespace td
