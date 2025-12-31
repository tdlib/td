//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftAuctionRound.h"

namespace td {

StarGiftAuctionRound::StarGiftAuctionRound(
    const telegram_api::object_ptr<telegram_api::StarGiftAuctionRound> &round_ptr) {
  CHECK(round_ptr != nullptr);
  switch (round_ptr->get_id()) {
    case telegram_api::starGiftAuctionRound::ID: {
      auto *round = static_cast<const telegram_api::starGiftAuctionRound *>(round_ptr.get());
      num_ = round->num_;
      duration_ = round->duration_;
      break;
    }
    case telegram_api::starGiftAuctionRoundExtendable::ID: {
      auto *round = static_cast<const telegram_api::starGiftAuctionRoundExtendable *>(round_ptr.get());
      num_ = round->num_;
      duration_ = round->duration_;
      extend_top_ = round->extend_top_;
      extend_window_ = round->extend_window_;
      break;
    }
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::auctionRound> StarGiftAuctionRound::get_auction_round_object() const {
  return td_api::make_object<td_api::auctionRound>(num_, duration_, extend_window_, extend_top_);
}

bool operator==(const StarGiftAuctionRound &lhs, const StarGiftAuctionRound &rhs) {
  return lhs.num_ == rhs.num_ && lhs.duration_ == rhs.duration_ && lhs.extend_window_ == rhs.extend_window_ &&
         lhs.extend_top_ == rhs.extend_top_;
}

}  // namespace td
