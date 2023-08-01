//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MediaArea.h"

namespace td {

MediaArea::MediaArea(Td *td, telegram_api::object_ptr<telegram_api::MediaArea> &&media_area_ptr) {
  CHECK(media_area_ptr != nullptr);
  switch (media_area_ptr->get_id()) {
    case telegram_api::mediaAreaGeoPoint::ID: {
      auto area = telegram_api::move_object_as<telegram_api::mediaAreaGeoPoint>(media_area_ptr);
      coordinates_ = MediaAreaCoordinates(area->coordinates_);
      location_ = Location(td, area->geo_);
      if (coordinates_.is_valid() && !location_.empty()) {
        type_ = Type::Location;
      } else {
        LOG(ERROR) << "Receive " << to_string(area);
      }
      break;
    }
    case telegram_api::mediaAreaVenue::ID: {
      auto area = telegram_api::move_object_as<telegram_api::mediaAreaVenue>(media_area_ptr);
      coordinates_ = MediaAreaCoordinates(area->coordinates_);
      venue_ = Venue(td, area->geo_, std::move(area->title_), std::move(area->address_), std::move(area->provider_),
                     std::move(area->venue_id_), std::move(area->venue_type_));
      if (coordinates_.is_valid() && !venue_.empty()) {
        type_ = Type::Venue;
      } else {
        LOG(ERROR) << "Receive " << to_string(area);
      }
      break;
    }
    case telegram_api::inputMediaAreaVenue::ID:
      LOG(ERROR) << "Receive " << to_string(media_area_ptr);
      break;
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::storyArea> MediaArea::get_story_area_object() const {
  CHECK(is_valid());
  td_api::object_ptr<td_api::StoryAreaType> type;
  switch (type_) {
    case Type::Location:
      type = td_api::make_object<td_api::storyAreaTypeLocation>(location_.get_location_object());
      break;
    case Type::Venue:
      type = td_api::make_object<td_api::storyAreaTypeVenue>(venue_.get_venue_object());
      break;
    default:
      UNREACHABLE();
  }
  return td_api::make_object<td_api::storyArea>(coordinates_.get_story_area_position_object(), std::move(type));
}

bool operator==(const MediaArea &lhs, const MediaArea &rhs) {
  return lhs.type_ == rhs.type_ && lhs.coordinates_ == rhs.coordinates_ && lhs.location_ == rhs.location_ &&
         lhs.venue_ == rhs.venue_;
}

bool operator!=(const MediaArea &lhs, const MediaArea &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MediaArea &media_area) {
  return string_builder << "StoryArea[" << media_area.coordinates_ << ": " << media_area.location_ << '/'
                        << media_area.venue_ << ']';
}

}  // namespace td
