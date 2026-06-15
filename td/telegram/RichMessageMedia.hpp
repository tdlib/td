//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/RichMessageMedia.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RichMessageMedia::store(StorerT &storer) const {
  bool has_id = !id_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_id);
  END_STORE_FLAGS();
  if (has_id) {
    td::store(title_, storer);
  }
  store_message_content(media_.get(), storer);
}

template <class ParserT>
void RichMessageMedia::parse(ParserT &parser) {
  bool has_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_id);
  END_PARSE_FLAGS();
  if (has_id) {
    td::parse(id_, parser);
  }
  parse_message_content(media_, parser);
}

}  // namespace td
