//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AuctionBidLevel.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"

namespace td {

class StarGiftAuctionUserState;
class Td;

class StarGiftAuctionState {
  bool is_not_modified_ = false;
  bool is_active_ = false;
  int32 start_date_ = 0;
  int32 end_date_ = 0;

  // active
  int32 version_ = 0;
  int64 min_bid_amount_ = 0;
  vector<AuctionBidLevel> bid_levels_;
  vector<UserId> top_bidder_user_ids_;
  int32 next_round_at_ = 0;
  int32 gifts_left_ = 0;
  int32 current_round_ = 0;
  int32 total_rounds_ = 0;

  // finished
  int64 average_price_ = 0;

  friend bool operator==(const StarGiftAuctionState &lhs, const StarGiftAuctionState &rhs);

 public:
  StarGiftAuctionState() = default;

  explicit StarGiftAuctionState(const telegram_api::object_ptr<telegram_api::StarGiftAuctionState> &state_ptr);

  bool is_not_modified() const {
    return is_not_modified_;
  }

  int32 get_version() const {
    return version_;
  }

  td_api::object_ptr<td_api::AuctionState> get_auction_state_object(Td *td,
                                                                    const StarGiftAuctionUserState &user_state) const;
};

bool operator==(const StarGiftAuctionState &lhs, const StarGiftAuctionState &rhs);

inline bool operator!=(const StarGiftAuctionState &lhs, const StarGiftAuctionState &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
