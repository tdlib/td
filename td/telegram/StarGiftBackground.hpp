//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StarGiftBackground.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void StarGiftBackground::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(center_color_, storer);
  td::store(edge_color_, storer);
  td::store(text_color_, storer);
}

template <class ParserT>
void StarGiftBackground::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(center_color_, parser);
  td::parse(edge_color_, parser);
  td::parse(text_color_, parser);
}

}  // namespace td
