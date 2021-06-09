//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BackgroundType.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void BackgroundType::store(StorerT &storer) const {
  using td::store;
  bool has_fill = fill.top_color != 0 || fill.bottom_color != 0;
  bool has_intensity = intensity != 0;
  auto fill_type = fill.get_type();
  bool is_gradient = fill_type == BackgroundFill::Type::Gradient;
  bool is_freeform_gradient = fill_type == BackgroundFill::Type::FreeformGradient;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_blurred);
  STORE_FLAG(is_moving);
  STORE_FLAG(has_fill);
  STORE_FLAG(has_intensity);
  STORE_FLAG(is_gradient);
  STORE_FLAG(is_freeform_gradient);
  END_STORE_FLAGS();
  store(type, storer);
  if (is_freeform_gradient) {
    store(fill.top_color, storer);
    store(fill.bottom_color, storer);
    store(fill.third_color, storer);
    store(fill.fourth_color, storer);
  } else if (has_fill) {
    store(fill.top_color, storer);
    if (is_gradient) {
      store(fill.bottom_color, storer);
      store(fill.rotation_angle, storer);
    }
  }
  if (has_intensity) {
    store(intensity, storer);
  }
}

template <class ParserT>
void BackgroundType::parse(ParserT &parser) {
  using td::parse;
  bool has_fill;
  bool has_intensity;
  bool is_gradient;
  bool is_freeform_gradient;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_blurred);
  PARSE_FLAG(is_moving);
  PARSE_FLAG(has_fill);
  PARSE_FLAG(has_intensity);
  PARSE_FLAG(is_gradient);
  PARSE_FLAG(is_freeform_gradient);
  END_PARSE_FLAGS();
  parse(type, parser);
  if (is_freeform_gradient) {
    parse(fill.top_color, parser);
    parse(fill.bottom_color, parser);
    parse(fill.third_color, parser);
    parse(fill.fourth_color, parser);
  } else if (has_fill) {
    parse(fill.top_color, parser);
    if (is_gradient) {
      parse(fill.bottom_color, parser);
      parse(fill.rotation_angle, parser);
    } else {
      fill.bottom_color = fill.top_color;
    }
  }
  if (has_intensity) {
    parse(intensity, parser);
  }
}

}  // namespace td
