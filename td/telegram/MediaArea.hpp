//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MediaArea.h"
#include "td/telegram/MediaAreaCoordinates.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MediaArea::store(StorerT &storer) const {
  using td::store;
  bool has_input_query_id = input_query_id_ != 0;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_input_query_id);
  STORE_FLAG(is_dark_);
  STORE_FLAG(is_flipped_);
  END_STORE_FLAGS();
  store(type_, storer);
  store(coordinates_, storer);
  switch (type_) {
    case Type::Location:
      store(location_, storer);
      break;
    case Type::Venue:
      store(venue_, storer);
      if (has_input_query_id) {
        store(input_query_id_, storer);
        store(input_result_id_, storer);
      }
      break;
    case Type::Reaction:
      store(reaction_type_, storer);
      break;
    default:
      UNREACHABLE();
  }
}

template <class ParserT>
void MediaArea::parse(ParserT &parser) {
  using td::parse;
  bool has_input_query_id;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_input_query_id);
  PARSE_FLAG(is_dark_);
  PARSE_FLAG(is_flipped_);
  END_PARSE_FLAGS();
  parse(type_, parser);
  parse(coordinates_, parser);
  switch (type_) {
    case Type::Location:
      parse(location_, parser);
      break;
    case Type::Venue:
      parse(venue_, parser);
      if (has_input_query_id) {
        parse(input_query_id_, parser);
        parse(input_result_id_, parser);
      }
      break;
    case Type::Reaction:
      parse(reaction_type_, parser);
      break;
    default:
      parser.set_error("Load invalid area type");
  }
}

}  // namespace td
