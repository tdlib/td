//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ToDoList.h"

#include "td/telegram/MessageEntity.hpp"
#include "td/telegram/ToDoItem.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ToDoList::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(others_can_append_);
  STORE_FLAG(others_can_complete_);
  END_STORE_FLAGS();
  td::store(title_, storer);
  td::store(items_, storer);
}

template <class ParserT>
void ToDoList::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(others_can_append_);
  PARSE_FLAG(others_can_complete_);
  END_PARSE_FLAGS();
  td::parse(title_, parser);
  td::parse(items_, parser);
  validate("parse ToDoList");
}

}  // namespace td
