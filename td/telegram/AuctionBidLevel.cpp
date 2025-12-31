//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AuctionBidLevel.h"

#include "td/telegram/StarManager.h"

#include "td/utils/logging.h"

namespace td {

AuctionBidLevel::AuctionBidLevel(const telegram_api::object_ptr<telegram_api::auctionBidLevel> &bid_level)
    : position_(bid_level->pos_)
    , star_count_(StarManager::get_star_count(bid_level->amount_))
    , date_(bid_level->date_) {
}

bool AuctionBidLevel::is_before(const AuctionBidLevel &other) const {
  return position_ < other.position_ &&
         (star_count_ > other.star_count_ || (star_count_ == other.star_count_ && date_ <= other.date_));
}

vector<AuctionBidLevel> AuctionBidLevel::get_auction_bid_levels(
    const vector<telegram_api::object_ptr<telegram_api::auctionBidLevel>> &bid_levels) {
  vector<AuctionBidLevel> result;
  for (const auto &bid_level : bid_levels) {
    AuctionBidLevel level(bid_level);
    if (result.empty() || result.back().is_before(level)) {
      result.push_back(std::move(level));
    }
  }
  if (result.size() != bid_levels.size()) {
    LOG(ERROR) << "Receive unsorted bid levels";
    for (const auto &bid_level : bid_levels) {
      LOG(ERROR) << to_string(bid_level);
    }
  }
  return result;
}

td_api::object_ptr<td_api::auctionBid> AuctionBidLevel::get_auction_bid_object() const {
  return td_api::make_object<td_api::auctionBid>(star_count_, date_, position_);
}

bool operator==(const AuctionBidLevel &lhs, const AuctionBidLevel &rhs) {
  return lhs.position_ == rhs.position_ && lhs.star_count_ == rhs.star_count_ && lhs.date_ == rhs.date_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const AuctionBidLevel &bid_level) {
  return string_builder << "[#" << bid_level.position_ << ": " << bid_level.star_count_ << " at " << bid_level.date_
                        << ']';
}

}  // namespace td
