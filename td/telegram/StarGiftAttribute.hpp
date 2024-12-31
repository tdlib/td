//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarGiftAttribute.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGiftAttributeSticker::store(StorerT &storer) const {
  CHECK(is_valid());
  Td *td = storer.context()->td().get_actor_unsafe();
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(name_, storer);
  td->stickers_manager_->store_sticker(sticker_file_id_, false, storer, "StarGiftAttributeSticker");
  td::store(rarity_permille_, storer);
}

template <class ParserT>
void StarGiftAttributeSticker::parse(ParserT &parser) {
  Td *td = parser.context()->td().get_actor_unsafe();
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(name_, parser);
  sticker_file_id_ = td->stickers_manager_->parse_sticker(false, parser);
  td::parse(rarity_permille_, parser);
}

template <class StorerT>
void StarGiftAttributeBackdrop::store(StorerT &storer) const {
  CHECK(is_valid());
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(name_, storer);
  td::store(center_color_, storer);
  td::store(edge_color_, storer);
  td::store(pattern_color_, storer);
  td::store(text_color_, storer);
  td::store(rarity_permille_, storer);
}

template <class ParserT>
void StarGiftAttributeBackdrop::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(name_, parser);
  td::parse(center_color_, parser);
  td::parse(edge_color_, parser);
  td::parse(pattern_color_, parser);
  td::parse(text_color_, parser);
  td::parse(rarity_permille_, parser);
}

template <class StorerT>
void StarGiftAttributeOriginalDetails::store(StorerT &storer) const {
  CHECK(is_valid());
  bool has_sender_user_id = sender_user_id_.is_valid();
  bool has_message = !message_.text.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_sender_user_id);
  STORE_FLAG(has_message);
  END_STORE_FLAGS();
  if (has_sender_user_id) {
    td::store(sender_user_id_, storer);
  }
  td::store(receiver_user_id_, storer);
  td::store(date_, storer);
  if (has_message) {
    td::store(message_, storer);
  }
}

template <class ParserT>
void StarGiftAttributeOriginalDetails::parse(ParserT &parser) {
  bool has_sender_user_id;
  bool has_message;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_sender_user_id);
  PARSE_FLAG(has_message);
  END_PARSE_FLAGS();
  if (has_sender_user_id) {
    td::parse(sender_user_id_, parser);
  }
  td::parse(receiver_user_id_, parser);
  td::parse(date_, parser);
  if (has_message) {
    td::parse(message_, parser);
  }
}

}  // namespace td
