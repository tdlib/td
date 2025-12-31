//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftAuctionUserState.h"

#include "td/telegram/MessageSender.h"
#include "td/telegram/StarManager.h"

namespace td {

StarGiftAuctionUserState::StarGiftAuctionUserState(
    const telegram_api::object_ptr<telegram_api::starGiftAuctionUserState> &state) {
  CHECK(state != nullptr);
  bid_amount_ = StarManager::get_star_count(state->bid_amount_);
  bid_date_ = max(0, state->bid_date_);
  min_bid_amount_ = StarManager::get_star_count(state->min_bid_amount_);
  if (state->bid_peer_ != nullptr) {
    bid_dialog_id_ = DialogId(state->bid_peer_);
  }
  was_returned_ = state->returned_;
  acquired_count_ = state->acquired_count_;
}

td_api::object_ptr<td_api::userAuctionBid> StarGiftAuctionUserState::get_user_auction_bid_object(Td *td) const {
  if (bid_amount_ == 0 || bid_date_ == 0 || !bid_dialog_id_.is_valid()) {
    return nullptr;
  }
  return td_api::make_object<td_api::userAuctionBid>(bid_amount_, bid_date_, min_bid_amount_,
                                                     get_message_sender_object(td, bid_dialog_id_, "userAuctionBid"),
                                                     was_returned_);
}

bool operator==(const StarGiftAuctionUserState &lhs, const StarGiftAuctionUserState &rhs) {
  return lhs.bid_amount_ == rhs.bid_amount_ && lhs.bid_date_ == rhs.bid_date_ &&
         lhs.min_bid_amount_ == rhs.min_bid_amount_ && lhs.bid_dialog_id_ == rhs.bid_dialog_id_ &&
         lhs.was_returned_ == rhs.was_returned_ && lhs.acquired_count_ == rhs.acquired_count_;
}

}  // namespace td
