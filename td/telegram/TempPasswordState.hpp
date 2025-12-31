//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/TempPasswordState.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void TempPasswordState::store(StorerT &storer) const {
  CHECK(has_temp_password);
  td::store(temp_password, storer);
  td::store(valid_until, storer);
}

template <class ParserT>
void TempPasswordState::parse(ParserT &parser) {
  has_temp_password = true;
  td::parse(temp_password, parser);
  td::parse(valid_until, parser);
}

}  // namespace td
