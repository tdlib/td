//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/StarGiftAttribute.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGiftAttributeModel::store(StorerT &storer) const {
  CHECK(is_valid());
  Td *td = storer.context()->td().get_actor_unsafe();
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(name_, storer);
  td->stickers_manager_->store_sticker(sticker_file_id_, false, storer, "StarGiftAttributeModel");
  td::store(rarity_permille_, storer);
}

template <class ParserT>
void StarGiftAttributeModel::parse(ParserT &parser) {
  Td *td = parser.context()->td().get_actor_unsafe();
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(name_, parser);
  sticker_file_id_ = td->stickers_manager_->parse_sticker(false, parser);
  td::parse(rarity_permille_, parser);
}

}  // namespace td
