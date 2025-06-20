//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ToDoCompletion.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ToDoCompletion::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(id_, storer);
  td::store(completed_by_user_id_, storer);
  td::store(date_, storer);
}

template <class ParserT>
void ToDoCompletion::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(id_, parser);
  td::parse(completed_by_user_id_, parser);
  td::parse(date_, parser);
}

}  // namespace td
