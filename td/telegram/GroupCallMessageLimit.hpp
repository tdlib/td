//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/GroupCallMessageLimit.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void GroupCallMessageLimit::store(StorerT &storer) const {
  using td::store;
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  store(star_count_, storer);
  store(pin_duration_, storer);
  store(max_text_length_, storer);
  store(max_emoji_count_, storer);
  store(color1_, storer);
  store(color2_, storer);
  store(color_bg_, storer);
}

template <class ParserT>
void GroupCallMessageLimit::parse(ParserT &parser) {
  using td::parse;
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  parse(star_count_, parser);
  parse(pin_duration_, parser);
  parse(max_text_length_, parser);
  parse(max_emoji_count_, parser);
  parse(color1_, parser);
  parse(color2_, parser);
  parse(color_bg_, parser);
}

template <class StorerT>
void GroupCallMessageLimits::store(StorerT &storer) const {
  using td::store;
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  store(limits_, storer);
}

template <class ParserT>
void GroupCallMessageLimits::parse(ParserT &parser) {
  using td::parse;
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  parse(limits_, parser);
}

}  // namespace td
