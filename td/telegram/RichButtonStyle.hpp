//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/RichButtonStyle.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RichButtonStyle::store(StorerT &storer) const {
  bool has_type = type_ != Type::Default;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_type);
  END_STORE_FLAGS();
  if (has_type) {
    td::store(type_, storer);
  }
}

template <class ParserT>
void RichButtonStyle::parse(ParserT &parser) {
  bool has_type;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_type);
  END_PARSE_FLAGS();
  if (has_type) {
    td::parse(type_, parser);
  }
}

}  // namespace td
