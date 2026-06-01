//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/RichMessage.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RichMessage::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_rtl_);
  STORE_FLAG(is_full_);
  END_STORE_FLAGS();
  td::store(blocks_, storer);
}

template <class ParserT>
void RichMessage::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_rtl_);
  PARSE_FLAG(is_full_);
  END_PARSE_FLAGS();
  td::parse(blocks_, parser);
}

}  // namespace td
