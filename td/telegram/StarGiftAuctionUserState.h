//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Td;

class StarGiftAuctionUserState {
  int64 bid_amount_ = 0;
  int32 bid_date_ = 0;
  int64 min_bid_amount_ = 0;
  DialogId bid_dialog_id_;
  bool was_returned_ = false;
  int32 acquired_count_ = 0;

  friend bool operator==(const StarGiftAuctionUserState &lhs, const StarGiftAuctionUserState &rhs);

 public:
  StarGiftAuctionUserState() = default;

  explicit StarGiftAuctionUserState(const telegram_api::object_ptr<telegram_api::starGiftAuctionUserState> &state);

  td_api::object_ptr<td_api::userAuctionBid> get_user_auction_bid_object(Td *td) const;

  int64 get_bid_amount() const {
    return bid_amount_;
  }

  int32 get_acquired_count() const {
    return acquired_count_;
  }

  bool is_active() const {
    return bid_dialog_id_ != DialogId();
  }
};

bool operator==(const StarGiftAuctionUserState &lhs, const StarGiftAuctionUserState &rhs);

inline bool operator!=(const StarGiftAuctionUserState &lhs, const StarGiftAuctionUserState &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
