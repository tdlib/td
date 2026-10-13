//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MediaAreaCoordinates.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/TonNft.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;
class Td;

class TonWalletCardSticker {
  MediaAreaCoordinates coordinates_;
  TonNft nft_;

  friend bool operator==(const TonWalletCardSticker &lhs, const TonWalletCardSticker &rhs);

 public:
  TonWalletCardSticker() = default;

  TonWalletCardSticker(Td *td, telegram_api::object_ptr<telegram_api::wallet_cardSticker> &&sticker);

  bool is_valid() const {
    return coordinates_.is_valid() && nft_.is_valid();
  }

  const string &get_nft_address() const {
    return nft_.get_address();
  }

  td_api::object_ptr<td_api::tonWalletCardSticker> get_ton_wallet_card_sticker_object(Td *td) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const TonWalletCardSticker &lhs, const TonWalletCardSticker &rhs);
bool operator!=(const TonWalletCardSticker &lhs, const TonWalletCardSticker &rhs);

}  // namespace td
