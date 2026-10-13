//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TonWalletCardSticker.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;
class Td;

class TonWalletCard {
  string address_;
  int64 balance_ = 0;
  vector<TonWalletCardSticker> stickers_;
  string background_nft_address_;

  friend bool operator==(const TonWalletCard &lhs, const TonWalletCard &rhs);

  TonWalletCard(Td *td, telegram_api::object_ptr<telegram_api::wallet_card> &&card);

 public:
  TonWalletCard() = default;

  static unique_ptr<TonWalletCard> get_ton_wallet_card(Td *td,
                                                       telegram_api::object_ptr<telegram_api::wallet_card> &&card);

  td_api::object_ptr<td_api::tonWalletCard> get_ton_wallet_card_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const TonWalletCard &lhs, const TonWalletCard &rhs);
bool operator!=(const TonWalletCard &lhs, const TonWalletCard &rhs);

}  // namespace td
