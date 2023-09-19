//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Location.h"
#include "td/telegram/MediaAreaCoordinates.h"
#include "td/telegram/ReactionType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/Venue.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

#include <utility>

namespace td {

class Td;

class MediaArea {
  enum class Type : int32 { None, Location, Venue, Reaction };
  Type type_ = Type::None;
  MediaAreaCoordinates coordinates_;
  Location location_;
  Venue venue_;
  int64 input_query_id_ = 0;
  string input_result_id_;
  ReactionType reaction_type_;
  bool is_dark_ = false;
  bool is_flipped_ = false;

  friend bool operator==(const MediaArea &lhs, const MediaArea &rhs);
  friend bool operator!=(const MediaArea &lhs, const MediaArea &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MediaArea &media_area);

 public:
  MediaArea() = default;

  MediaArea(Td *td, telegram_api::object_ptr<telegram_api::MediaArea> &&media_area_ptr);

  MediaArea(Td *td, td_api::object_ptr<td_api::inputStoryArea> &&input_story_area,
            const vector<MediaArea> &old_media_areas);

  bool has_reaction_type(const ReactionType &reaction_type) const;

  td_api::object_ptr<td_api::storyArea> get_story_area_object(
      const vector<std::pair<ReactionType, int32>> &reaction_counts) const;

  telegram_api::object_ptr<telegram_api::MediaArea> get_input_media_area() const;

  bool is_valid() const {
    return type_ != Type::None;
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MediaArea &lhs, const MediaArea &rhs);
bool operator!=(const MediaArea &lhs, const MediaArea &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const MediaArea &media_area);

}  // namespace td
