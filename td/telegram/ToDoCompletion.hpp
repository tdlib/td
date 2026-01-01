//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ToDoCompletion.h"
#include "td/telegram/UserId.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ToDoCompletion::store(StorerT &storer) const {
  bool is_completed_by_dialog = completed_by_dialog_id_.get_type() != DialogType::User;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_completed_by_dialog);
  END_STORE_FLAGS();
  td::store(id_, storer);
  if (is_completed_by_dialog) {
    td::store(completed_by_dialog_id_, storer);
  } else {
    td::store(completed_by_dialog_id_.get_user_id(), storer);
  }
  td::store(date_, storer);
}

template <class ParserT>
void ToDoCompletion::parse(ParserT &parser) {
  bool is_completed_by_dialog;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_completed_by_dialog);
  END_PARSE_FLAGS();
  td::parse(id_, parser);
  if (is_completed_by_dialog) {
    td::parse(completed_by_dialog_id_, parser);
  } else {
    UserId completed_by_user_id;
    td::parse(completed_by_user_id, parser);
    completed_by_dialog_id_ = DialogId(completed_by_user_id);
  }
  td::parse(date_, parser);
}

}  // namespace td
