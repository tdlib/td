//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
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
  bool has_fill = type.fill.top_color != 0 || type.fill.bottom_color != 0;
  bool has_intensity = type.intensity != 0;
  bool is_gradient = !type.fill.is_solid();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(type.is_blurred);
  STORE_FLAG(type.is_moving);
  STORE_FLAG(has_fill);
  STORE_FLAG(has_intensity);
  STORE_FLAG(is_gradient);
  END_STORE_FLAGS();
  store(type.type, storer);
  if (has_fill) {
    store(type.fill.top_color, storer);
    if (is_gradient) {
      store(type.fill.bottom_color, storer);
      store(type.fill.rotation_angle, storer);
    }
  }
  if (has_intensity) {
    store(type.intensity, storer);
  }
}

template <class ParserT>
void parse(BackgroundType &type, ParserT &parser) {
  bool has_fill;
  bool has_intensity;
  bool is_gradient;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(type.is_blurred);
  PARSE_FLAG(type.is_moving);
  PARSE_FLAG(has_fill);
  PARSE_FLAG(has_intensity);
  PARSE_FLAG(is_gradient);
  END_PARSE_FLAGS();
  parse(type.type, parser);
  if (has_fill) {
    parse(type.fill.top_color, parser);
    if (is_gradient) {
      parse(type.fill.bottom_color, parser);
      parse(type.fill.rotation_angle, parser);
    } else {
      type.fill.bottom_color = type.fill.top_color;
    }
  }
  if (has_intensity) {
    parse(type.intensity, parser);
  }
}

}  // namespace td
