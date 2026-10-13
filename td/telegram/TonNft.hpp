//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/TonNft.h"

#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DocumentsManager.hpp"
#include "td/telegram/PhotoSize.hpp"
#include "td/telegram/StarGift.hpp"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void TonNftAttribute::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(trait_type_, storer);
  td::store(value_, storer);
}

template <class ParserT>
void TonNftAttribute::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(trait_type_, parser);
  td::parse(value_, parser);
}

template <class StorerT>
void TonNft::store(StorerT &storer) const {
  Td *td = storer.context()->td().get_actor_unsafe();
  CHECK(td != nullptr);
  bool has_collection_address = !collection_address_.empty();
  bool has_owner_address = !owner_address_.empty();
  bool has_index = !index_.empty();
  bool has_name = !name_.empty();
  bool has_description = !description_.empty();
  bool has_image = image_.file_id.is_valid();
  bool has_image_small = image_small_.file_id.is_valid();
  bool has_content_url_file_id = content_url_file_id_.is_valid();
  bool has_lottie_file_id = lottie_file_id_.is_valid();
  bool has_attributes = !attributes_.empty();
  bool has_extra = !extra_.empty();
  bool has_gift = gift_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_collection_address);
  STORE_FLAG(has_owner_address);
  STORE_FLAG(has_index);
  STORE_FLAG(has_name);
  STORE_FLAG(has_description);
  STORE_FLAG(has_image);
  STORE_FLAG(has_image_small);
  STORE_FLAG(has_content_url_file_id);
  STORE_FLAG(has_lottie_file_id);
  STORE_FLAG(has_attributes);
  STORE_FLAG(has_extra);
  STORE_FLAG(has_gift);
  END_STORE_FLAGS();
  td::store(address_, storer);
  if (has_collection_address) {
    td::store(collection_address_, storer);
  }
  if (has_owner_address) {
    td::store(owner_address_, storer);
  }
  if (has_index) {
    td::store(index_, storer);
  }
  if (has_name) {
    td::store(name_, storer);
  }
  if (has_description) {
    td::store(description_, storer);
  }
  if (has_image) {
    td::store(image_, storer);
  }
  if (has_image_small) {
    td::store(image_small_, storer);
  }
  if (has_content_url_file_id) {
    td->documents_manager_->store_document(content_url_file_id_, storer);
  }
  if (has_lottie_file_id) {
    td->stickers_manager_->store_sticker(lottie_file_id_, false, storer, "TonNft");
  }
  if (has_attributes) {
    td::store(attributes_, storer);
  }
  if (has_extra) {
    td::store(extra_, storer);
  }
  if (has_gift) {
    td::store(gift_, storer);
  }
}

template <class ParserT>
void TonNft::parse(ParserT &parser) {
  Td *td = parser.context()->td().get_actor_unsafe();
  CHECK(td != nullptr);
  bool has_collection_address;
  bool has_owner_address;
  bool has_index;
  bool has_name;
  bool has_description;
  bool has_image;
  bool has_image_small;
  bool has_content_url_file_id;
  bool has_lottie_file_id;
  bool has_attributes;
  bool has_extra;
  bool has_gift;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_collection_address);
  PARSE_FLAG(has_owner_address);
  PARSE_FLAG(has_index);
  PARSE_FLAG(has_name);
  PARSE_FLAG(has_description);
  PARSE_FLAG(has_image);
  PARSE_FLAG(has_image_small);
  PARSE_FLAG(has_content_url_file_id);
  PARSE_FLAG(has_lottie_file_id);
  PARSE_FLAG(has_attributes);
  PARSE_FLAG(has_extra);
  PARSE_FLAG(has_gift);
  END_PARSE_FLAGS();
  td::parse(address_, parser);
  if (has_collection_address) {
    td::parse(collection_address_, parser);
  }
  if (has_owner_address) {
    td::parse(owner_address_, parser);
  }
  if (has_index) {
    td::parse(index_, parser);
  }
  if (has_name) {
    td::parse(name_, parser);
  }
  if (has_description) {
    td::parse(description_, parser);
  }
  if (has_image) {
    td::parse(image_, parser);
  }
  if (has_image_small) {
    td::parse(image_small_, parser);
  }
  if (has_content_url_file_id) {
    content_url_file_id_ = td->documents_manager_->parse_document(parser);
  }
  if (has_lottie_file_id) {
    lottie_file_id_ = td->stickers_manager_->parse_sticker(false, parser);
  }
  if (has_attributes) {
    td::parse(attributes_, parser);
  }
  if (has_extra) {
    td::parse(extra_, parser);
  }
  if (has_gift) {
    td::parse(gift_, parser);
  }
}

}  // namespace td
