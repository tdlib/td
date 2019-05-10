//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundType.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const BackgroundType &type, StorerT &storer) {
  bool has_color = type.color != 0;
  bool has_intensity = type.intensity != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(type.is_blurred);
  STORE_FLAG(type.is_moving);
  STORE_FLAG(has_color);
  STORE_FLAG(has_intensity);
  END_STORE_FLAGS();
  store(type.type, storer);
  if (has_color) {
    store(type.color, storer);
  }
  if (has_intensity) {
    store(type.intensity, storer);
  }
}

template <class ParserT>
void parse(BackgroundType &type, ParserT &parser) {
  bool has_color;
  bool has_intensity;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(type.is_blurred);
  PARSE_FLAG(type.is_moving);
  PARSE_FLAG(has_color);
  PARSE_FLAG(has_intensity);
  END_PARSE_FLAGS();
  parse(type.type, parser);
  if (has_color) {
    parse(type.color, parser);
  }
  if (has_intensity) {
    parse(type.intensity, parser);
  }
}

}  // namespace td
