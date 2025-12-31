//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ToDoItem.h"

#include "td/telegram/MessageEntity.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ToDoItem::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(id_, storer);
  td::store(title_, storer);
}

template <class ParserT>
void ToDoItem::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(id_, parser);
  td::parse(title_, parser);
  validate("parse");
}

}  // namespace td
