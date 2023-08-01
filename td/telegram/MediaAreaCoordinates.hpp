//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MediaAreaCoordinates.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MediaAreaCoordinates::store(StorerT &storer) const {
  using td::store;
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  store(x_, storer);
  store(y_, storer);
  store(width_, storer);
  store(height_, storer);
  store(rotation_angle_, storer);
}

template <class ParserT>
void MediaAreaCoordinates::parse(ParserT &parser) {
  using td::parse;
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  double x;
  double y;
  double width;
  double height;
  double rotation_angle;
  parse(x, parser);
  parse(y, parser);
  parse(width, parser);
  parse(height, parser);
  parse(rotation_angle, parser);
  init(x, y, width, height, rotation_angle);
}

}  // namespace td
