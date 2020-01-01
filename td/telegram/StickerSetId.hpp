//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StickerSetId.h"

#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

namespace td {

template <class StorerT>
void store(const StickerSetId &sticker_set_id, StorerT &storer) {
  storer.context()->td().get_actor_unsafe()->stickers_manager_->store_sticker_set_id(sticker_set_id, storer);
}

template <class ParserT>
void parse(StickerSetId &sticker_set_id, ParserT &parser) {
  parser.context()->td().get_actor_unsafe()->stickers_manager_->parse_sticker_set_id(sticker_set_id, parser);
}

}  // namespace td
