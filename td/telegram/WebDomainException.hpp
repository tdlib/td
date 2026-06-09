//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/WebDomainException.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void WebDomainException::store(StorerT &storer) const {
  using td::store;
  bool has_favicon_custom_emoji_id = favicon_custom_emoji_id_.is_valid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_favicon_custom_emoji_id);
  END_STORE_FLAGS();
  store(domain_, storer);
  store(url_, storer);
  store(title_, storer);
  if (has_favicon_custom_emoji_id) {
    store(favicon_custom_emoji_id_, storer);
  }
}

template <class ParserT>
void WebDomainException::parse(ParserT &parser) {
  using td::parse;
  bool has_favicon_custom_emoji_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_favicon_custom_emoji_id);
  END_PARSE_FLAGS();
  parse(domain_, parser);
  parse(url_, parser);
  parse(title_, parser);
  if (has_favicon_custom_emoji_id) {
    parse(favicon_custom_emoji_id_, parser);
  }
}

}  // namespace td
