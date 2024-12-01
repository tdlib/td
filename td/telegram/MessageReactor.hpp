//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageReactor.h"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageReactor::store(StorerT &storer) const {
  bool has_dialog_id = dialog_id_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_top_);
  STORE_FLAG(is_me_);
  STORE_FLAG(is_anonymous_);
  STORE_FLAG(has_dialog_id);
  END_STORE_FLAGS();
  if (has_dialog_id) {
    td::store(dialog_id_, storer);
  }
  td::store(count_, storer);
}

template <class ParserT>
void MessageReactor::parse(ParserT &parser) {
  bool has_dialog_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_top_);
  PARSE_FLAG(is_me_);
  PARSE_FLAG(is_anonymous_);
  PARSE_FLAG(has_dialog_id);
  END_PARSE_FLAGS();
  if (has_dialog_id) {
    td::parse(dialog_id_, parser);
  }
  td::parse(count_, parser);
}

}  // namespace td
