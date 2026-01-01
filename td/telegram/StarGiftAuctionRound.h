//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class StarGiftAuctionRound {
  int32 num_ = 0;
  int32 duration_ = 0;
  int32 extend_top_ = 0;
  int32 extend_window_ = 0;

  friend bool operator==(const StarGiftAuctionRound &lhs, const StarGiftAuctionRound &rhs);

 public:
  StarGiftAuctionRound() = default;

  explicit StarGiftAuctionRound(const telegram_api::object_ptr<telegram_api::StarGiftAuctionRound> &round_ptr);

  td_api::object_ptr<td_api::auctionRound> get_auction_round_object() const;
};

bool operator==(const StarGiftAuctionRound &lhs, const StarGiftAuctionRound &rhs);

inline bool operator!=(const StarGiftAuctionRound &lhs, const StarGiftAuctionRound &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
