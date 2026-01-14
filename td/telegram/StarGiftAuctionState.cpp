//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftAuctionState.h"

#include "td/telegram/StarGiftAuctionUserState.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

namespace td {

StarGiftAuctionState::StarGiftAuctionState(
    const telegram_api::object_ptr<telegram_api::StarGiftAuctionState> &state_ptr) {
  CHECK(state_ptr != nullptr);
  switch (state_ptr->get_id()) {
    case telegram_api::starGiftAuctionState::ID: {
      const auto *state = static_cast<const telegram_api::starGiftAuctionState *>(state_ptr.get());
      is_active_ = true;
      start_date_ = state->start_date_;
      end_date_ = state->end_date_;
      version_ = state->version_;
      min_bid_amount_ = StarManager::get_star_count(state->min_bid_amount_);
      bid_levels_ = AuctionBidLevel::get_auction_bid_levels(state->bid_levels_);
      for (auto &top_bidder : state->top_bidders_) {
        UserId top_bidder_user_id(top_bidder);
        if (!top_bidder_user_id.is_valid()) {
          LOG(ERROR) << "Receive " << top_bidder_user_id;
        } else {
          top_bidder_user_ids_.push_back(top_bidder_user_id);
        }
      }
      const size_t MAX_BIDDER_COUNT = 3u;
      if (top_bidder_user_ids_.size() > MAX_BIDDER_COUNT) {
        LOG(ERROR) << "Receive " << top_bidder_user_ids_;
        top_bidder_user_ids_.resize(MAX_BIDDER_COUNT);
      }
      for (const auto &round : state->rounds_) {
        rounds_.emplace_back(round);
      }
      next_round_at_ = state->next_round_at_;
      last_gift_num_ = state->last_gift_num_;
      gifts_left_ = state->gifts_left_;
      current_round_ = state->current_round_;
      total_rounds_ = state->total_rounds_;
      if (total_rounds_ <= 0) {
        LOG(ERROR) << "Receive total " << total_rounds_ << " rounds";
        total_rounds_ = 1;
      }
      if (current_round_ <= 0 || current_round_ > total_rounds_) {
        LOG(ERROR) << "Receive round " << current_round_ << " out of " << total_rounds_ << " rounds";
        current_round_ = clamp(current_round_, 1, total_rounds_);
      }
      break;
    }
    case telegram_api::starGiftAuctionStateFinished::ID: {
      const auto *state = static_cast<const telegram_api::starGiftAuctionStateFinished *>(state_ptr.get());
      is_active_ = false;
      start_date_ = state->start_date_;
      end_date_ = state->end_date_;
      average_price_ = StarManager::get_star_count(state->average_price_);
      listed_count_ = max(0, state->listed_count_);
      fragment_listed_count_ = max(0, state->fragment_listed_count_);
      fragment_listed_url_ = std::move(state->fragment_listed_url_);
      break;
    }
    case telegram_api::starGiftAuctionStateNotModified::ID:
      is_not_modified_ = false;
      break;
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::AuctionState> StarGiftAuctionState::get_auction_state_object(
    Td *td, const StarGiftAuctionUserState &user_state) const {
  if (is_active_) {
    auto bid_levels =
        transform(bid_levels_, [](const AuctionBidLevel &level) { return level.get_auction_bid_object(); });
    auto top_bidder_user_ids = td->user_manager_->get_user_ids_object(top_bidder_user_ids_, "auctionStateActive");
    auto rounds =
        transform(rounds_, [](const StarGiftAuctionRound &round) { return round.get_auction_round_object(); });
    return td_api::make_object<td_api::auctionStateActive>(
        start_date_, end_date_, min_bid_amount_, std::move(bid_levels), std::move(top_bidder_user_ids),
        std::move(rounds), next_round_at_, current_round_, total_rounds_, last_gift_num_, gifts_left_,
        user_state.get_acquired_count(), user_state.get_user_auction_bid_object(td));
  } else {
    return td_api::make_object<td_api::auctionStateFinished>(start_date_, end_date_, average_price_,
                                                             user_state.get_acquired_count(), listed_count_,
                                                             fragment_listed_count_, fragment_listed_url_);
  }
}

bool operator==(const StarGiftAuctionState &lhs, const StarGiftAuctionState &rhs) {
  return lhs.is_active_ == rhs.is_active_ && lhs.start_date_ == rhs.start_date_ && lhs.end_date_ == rhs.end_date_ &&
         lhs.version_ == rhs.version_ && lhs.min_bid_amount_ == rhs.min_bid_amount_ &&
         lhs.bid_levels_ == rhs.bid_levels_ && lhs.top_bidder_user_ids_ == rhs.top_bidder_user_ids_ &&
         lhs.rounds_ == rhs.rounds_ && lhs.next_round_at_ == rhs.next_round_at_ &&
         lhs.last_gift_num_ == rhs.last_gift_num_ && lhs.gifts_left_ == rhs.gifts_left_ &&
         lhs.current_round_ == rhs.current_round_ && lhs.total_rounds_ == rhs.total_rounds_ &&
         lhs.average_price_ == rhs.average_price_ && lhs.listed_count_ == rhs.listed_count_ &&
         lhs.fragment_listed_count_ == rhs.fragment_listed_count_ &&
         lhs.fragment_listed_url_ == rhs.fragment_listed_url_;
}

}  // namespace td
