//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/TonWalletCard.h"

#include "td/telegram/TonWalletCardSticker.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void TonWalletCard::store(StorerT &storer) const {
  bool has_balance = balance_ != 0;
  bool has_stickers = !stickers_.empty();
  bool has_background_nft_address = !background_nft_address_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_balance);
  STORE_FLAG(has_stickers);
  STORE_FLAG(has_background_nft_address);
  END_STORE_FLAGS();
  td::store(address_, storer);
  if (has_balance) {
    td::store(balance_, storer);
  }
  if (has_stickers) {
    td::store(stickers_, storer);
  }
  if (has_background_nft_address) {
    td::store(background_nft_address_, storer);
  }
}

template <class ParserT>
void TonWalletCard::parse(ParserT &parser) {
  bool has_balance;
  bool has_stickers;
  bool has_background_nft_address;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_balance);
  PARSE_FLAG(has_stickers);
  PARSE_FLAG(has_background_nft_address);
  END_PARSE_FLAGS();
  td::parse(address_, parser);
  if (has_balance) {
    td::parse(balance_, parser);
  }
  if (has_stickers) {
    td::parse(stickers_, parser);
  }
  if (has_background_nft_address) {
    td::parse(background_nft_address_, parser);
  }
}

}  // namespace td
