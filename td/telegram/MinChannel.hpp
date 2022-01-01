//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MinChannel.h"
#include "td/telegram/Photo.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const MinChannel &min_channel, StorerT &storer) {
  bool has_title = !min_channel.title_.empty();
  bool has_photo = min_channel.photo_.small_file_id.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_title);
  STORE_FLAG(has_photo);
  STORE_FLAG(min_channel.is_megagroup_);
  END_STORE_FLAGS();
  if (has_title) {
    store(min_channel.title_, storer);
  }
  if (has_photo) {
    store(min_channel.photo_, storer);
  }
}

template <class ParserT>
void parse(MinChannel &min_channel, ParserT &parser) {
  bool has_title;
  bool has_photo;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_title);
  PARSE_FLAG(has_photo);
  PARSE_FLAG(min_channel.is_megagroup_);
  END_PARSE_FLAGS();
  if (has_title) {
    parse(min_channel.title_, parser);
  }
  if (has_photo) {
    parse(min_channel.photo_, parser);
  }
}

}  // namespace td
