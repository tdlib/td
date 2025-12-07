//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class AuctionBidLevel {
  int32 position_;
  int64 star_count_;
  int32 date_;

  bool is_before(const AuctionBidLevel &other) const;

  friend bool operator==(const AuctionBidLevel &lhs, const AuctionBidLevel &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const AuctionBidLevel &bid_level);

 public:
  explicit AuctionBidLevel(const telegram_api::object_ptr<telegram_api::auctionBidLevel> &bid_level);

  static vector<AuctionBidLevel> get_auction_bid_levels(
      const vector<telegram_api::object_ptr<telegram_api::auctionBidLevel>> &bid_levels);

  td_api::object_ptr<td_api::auctionBid> get_auction_bid_object() const;
};

bool operator==(const AuctionBidLevel &lhs, const AuctionBidLevel &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const AuctionBidLevel &bid_level);

}  // namespace td
