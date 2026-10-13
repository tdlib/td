//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonWalletCardSticker.h"

namespace td {

TonWalletCardSticker::TonWalletCardSticker(Td *td, telegram_api::object_ptr<telegram_api::wallet_cardSticker> &&sticker)
    : coordinates_(sticker->coordinates_), nft_(td, std::move(sticker->nft_)) {
}

td_api::object_ptr<td_api::tonWalletCardSticker> TonWalletCardSticker::get_ton_wallet_card_sticker_object(
    Td *td) const {
  return td_api::make_object<td_api::tonWalletCardSticker>(coordinates_.get_area_position_object(),
                                                           nft_.get_ton_nft_object(td));
}

void TonWalletCardSticker::add_dependencies(Dependencies &dependencies) const {
  nft_.add_dependencies(dependencies);
}

bool operator==(const TonWalletCardSticker &lhs, const TonWalletCardSticker &rhs) {
  return lhs.coordinates_ == rhs.coordinates_ && lhs.nft_ == rhs.nft_;
}

bool operator!=(const TonWalletCardSticker &lhs, const TonWalletCardSticker &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
