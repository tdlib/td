//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TonWalletCard.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

TonWalletCard::TonWalletCard(Td *td, telegram_api::object_ptr<telegram_api::wallet_card> &&card)
    : address_(std::move(card->address_)), balance_(card->balance_) {
  if (balance_ < 0) {
    LOG(ERROR) << "Receive TON wallet balance " << balance_;
    balance_ = 0;
  }
  for (auto &sticker_ptr : card->stickers_) {
    TonWalletCardSticker sticker(td, std::move(sticker_ptr));
    if (!sticker.is_valid()) {
      LOG(ERROR) << "Receive invalid sticker on TON wallet card " << address_;
      continue;
    }
    stickers_.push_back(std::move(sticker));
  }
  if (card->bg_nft_item_ != nullptr) {
    auto background_nft_address = std::move(card->bg_nft_item_->address_);
    if (any_of(stickers_, [&background_nft_address](const auto &sticker) {
          return sticker.get_nft_address() == background_nft_address;
        })) {
      background_nft_address_ = std::move(background_nft_address);
    } else {
      LOG(ERROR) << "Receive background NFT " << background_nft_address << " not from stickers on TON wallet card "
                 << address_;
    }
  }
}

unique_ptr<TonWalletCard> TonWalletCard::get_ton_wallet_card(
    Td *td, telegram_api::object_ptr<telegram_api::wallet_card> &&card) {
  if (card == nullptr) {
    return nullptr;
  }
  if (card->address_.empty()) {
    LOG(ERROR) << "Receive TON wallet card without address";
    return nullptr;
  }
  return unique_ptr<TonWalletCard>(new TonWalletCard(td, std::move(card)));
}

td_api::object_ptr<td_api::tonWalletCard> TonWalletCard::get_ton_wallet_card_object(Td *td) const {
  return td_api::make_object<td_api::tonWalletCard>(
      address_, balance_,
      transform(stickers_,
                [td](const TonWalletCardSticker &sticker) { return sticker.get_ton_wallet_card_sticker_object(td); }),
      background_nft_address_);
}

void TonWalletCard::add_dependencies(Dependencies &dependencies) const {
  for (auto &sticker : stickers_) {
    sticker.add_dependencies(dependencies);
  }
}

bool operator==(const TonWalletCard &lhs, const TonWalletCard &rhs) {
  return lhs.address_ == rhs.address_ && lhs.balance_ == rhs.balance_ && lhs.stickers_ == rhs.stickers_ &&
         lhs.background_nft_address_ == rhs.background_nft_address_;
}

bool operator!=(const TonWalletCard &lhs, const TonWalletCard &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
