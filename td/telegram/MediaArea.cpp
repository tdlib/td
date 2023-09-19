//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MediaArea.h"

#include "td/telegram/InlineQueriesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

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
    case telegram_api::mediaAreaSuggestedReaction::ID: {
      auto area = telegram_api::move_object_as<telegram_api::mediaAreaSuggestedReaction>(media_area_ptr);
      coordinates_ = MediaAreaCoordinates(area->coordinates_);
      reaction_type_ = ReactionType(area->reaction_);
      is_dark_ = area->dark_;
      is_flipped_ = area->flipped_;
      if (coordinates_.is_valid() && !reaction_type_.is_empty()) {
        type_ = Type::Reaction;
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

MediaArea::MediaArea(Td *td, td_api::object_ptr<td_api::inputStoryArea> &&input_story_area,
                     const vector<MediaArea> &old_media_areas) {
  if (input_story_area == nullptr || input_story_area->position_ == nullptr || input_story_area->type_ == nullptr) {
    return;
  }
  coordinates_ = MediaAreaCoordinates(input_story_area->position_);
  if (!coordinates_.is_valid()) {
    return;
  }
  switch (input_story_area->type_->get_id()) {
    case td_api::inputStoryAreaTypeLocation::ID: {
      auto type = td_api::move_object_as<td_api::inputStoryAreaTypeLocation>(input_story_area->type_);
      location_ = Location(type->location_);
      if (!location_.empty()) {
        type_ = Type::Location;
      }
      break;
    }
    case td_api::inputStoryAreaTypeFoundVenue::ID: {
      auto type = td_api::move_object_as<td_api::inputStoryAreaTypeFoundVenue>(input_story_area->type_);
      const InlineMessageContent *inline_message_content =
          td->inline_queries_manager_->get_inline_message_content(type->query_id_, type->result_id_);
      if (inline_message_content == nullptr || inline_message_content->message_content == nullptr) {
        break;
      }
      auto venue_ptr = get_message_content_venue(inline_message_content->message_content.get());
      if (venue_ptr == nullptr || venue_ptr->empty()) {
        break;
      }
      venue_ = *venue_ptr;
      input_query_id_ = type->query_id_;
      input_result_id_ = std::move(type->result_id_);
      type_ = Type::Venue;
      break;
    }
    case td_api::inputStoryAreaTypePreviousVenue::ID: {
      auto type = td_api::move_object_as<td_api::inputStoryAreaTypePreviousVenue>(input_story_area->type_);
      for (auto &old_media_area : old_media_areas) {
        if (old_media_area.type_ == Type::Venue && !old_media_area.venue_.empty() &&
            old_media_area.venue_.is_same(type->venue_provider_, type->venue_id_)) {
          venue_ = old_media_area.venue_;
          input_query_id_ = old_media_area.input_query_id_;
          input_result_id_ = old_media_area.input_result_id_;
          type_ = Type::Venue;
          break;
        }
      }
      break;
    }
    case td_api::inputStoryAreaTypeSuggestedReaction::ID: {
      auto type = td_api::move_object_as<td_api::inputStoryAreaTypeSuggestedReaction>(input_story_area->type_);
      reaction_type_ = ReactionType(type->reaction_type_);
      is_dark_ = type->is_dark_;
      is_flipped_ = type->is_flipped_;
      if (!reaction_type_.is_empty()) {
        type_ = Type::Reaction;
      }
      break;
    }
    default:
      UNREACHABLE();
  }
}

bool MediaArea::has_reaction_type(const ReactionType &reaction_type) const {
  return reaction_type_ == reaction_type;
}

td_api::object_ptr<td_api::storyArea> MediaArea::get_story_area_object(
    const vector<std::pair<ReactionType, int32>> &reaction_counts) const {
  CHECK(is_valid());
  td_api::object_ptr<td_api::StoryAreaType> type;
  switch (type_) {
    case Type::Location:
      type = td_api::make_object<td_api::storyAreaTypeLocation>(location_.get_location_object());
      break;
    case Type::Venue:
      type = td_api::make_object<td_api::storyAreaTypeVenue>(venue_.get_venue_object());
      break;
    case Type::Reaction: {
      int32 total_count = 0;
      for (const auto &reaction_count : reaction_counts) {
        if (reaction_count.first == reaction_type_) {
          total_count = reaction_count.second;
        }
      }
      type = td_api::make_object<td_api::storyAreaTypeSuggestedReaction>(reaction_type_.get_reaction_type_object(),
                                                                         total_count, is_dark_, is_flipped_);
      break;
    }
    default:
      UNREACHABLE();
  }
  return td_api::make_object<td_api::storyArea>(coordinates_.get_story_area_position_object(), std::move(type));
}

telegram_api::object_ptr<telegram_api::MediaArea> MediaArea::get_input_media_area() const {
  CHECK(is_valid());
  switch (type_) {
    case Type::Location:
      return telegram_api::make_object<telegram_api::mediaAreaGeoPoint>(coordinates_.get_input_media_area_coordinates(),
                                                                        location_.get_fake_geo_point());
    case Type::Venue:
      if (input_query_id_ != 0) {
        return telegram_api::make_object<telegram_api::inputMediaAreaVenue>(
            coordinates_.get_input_media_area_coordinates(), input_query_id_, input_result_id_);
      }
      return venue_.get_input_media_area_venue(coordinates_.get_input_media_area_coordinates());
    case Type::Reaction: {
      int32 flags = 0;
      if (is_dark_) {
        flags |= telegram_api::mediaAreaSuggestedReaction::DARK_MASK;
      }
      if (is_flipped_) {
        flags |= telegram_api::mediaAreaSuggestedReaction::FLIPPED_MASK;
      }
      return telegram_api::make_object<telegram_api::mediaAreaSuggestedReaction>(
          flags, false /*ignored*/, false /*ignored*/, coordinates_.get_input_media_area_coordinates(),
          reaction_type_.get_input_reaction());
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const MediaArea &lhs, const MediaArea &rhs) {
  return lhs.type_ == rhs.type_ && lhs.coordinates_ == rhs.coordinates_ && lhs.location_ == rhs.location_ &&
         lhs.venue_ == rhs.venue_ && lhs.input_query_id_ == rhs.input_query_id_ &&
         lhs.input_result_id_ == rhs.input_result_id_ && lhs.reaction_type_ == rhs.reaction_type_ &&
         lhs.is_dark_ == rhs.is_dark_ && lhs.is_flipped_ == rhs.is_flipped_;
}

bool operator!=(const MediaArea &lhs, const MediaArea &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MediaArea &media_area) {
  return string_builder << "StoryArea[" << media_area.coordinates_ << ": " << media_area.location_ << '/'
                        << media_area.venue_ << '/' << media_area.reaction_type_ << ']';
}

}  // namespace td
