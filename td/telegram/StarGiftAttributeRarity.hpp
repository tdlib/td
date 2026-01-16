//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarGiftAttributeRarity.h"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGiftAttributeRarity::store(StorerT &storer) const {
  bool has_rarity_permille = rarity_permille_ >= 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_rarity_permille);
  END_STORE_FLAGS();
  td::store(type_, storer);
  if (has_rarity_permille) {
    td::store(rarity_permille_, storer);
  }
}

template <class ParserT>
void StarGiftAttributeRarity::parse(ParserT &parser) {
  if (parser.version() < static_cast<int32>(Version::AddStarGiftAttributeRarity)) {
    type_ = Type::Permille;
    td::parse(rarity_permille_, parser);
    return;
  }
  bool has_rarity_permille;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_rarity_permille);
  END_PARSE_FLAGS();
  td::parse(type_, parser);
  if (has_rarity_permille) {
    td::parse(rarity_permille_, parser);
  }
}

}  // namespace td
