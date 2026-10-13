//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/TonWalletCardSticker.h"

#include "td/telegram/MediaAreaCoordinates.hpp"
#include "td/telegram/TonNft.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void TonWalletCardSticker::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(coordinates_, storer);
  td::store(nft_, storer);
}

template <class ParserT>
void TonWalletCardSticker::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(coordinates_, parser);
  td::parse(nft_, parser);
}

}  // namespace td
