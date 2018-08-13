//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Location.h"

#include "td/telegram/Global.h"
#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

#include <cmath>

namespace td {

void Location::init(double latitude, double longitude, int64 access_hash) {
  if (std::isfinite(latitude) && std::isfinite(longitude) && std::abs(latitude) <= 90 && std::abs(longitude) <= 180) {
    is_empty_ = false;
    latitude_ = latitude;
    longitude_ = longitude;
    access_hash_ = access_hash;
    G()->add_location_access_hash(latitude_, longitude_, access_hash_);
  }
}

Location::Location(double latitude, double longitude, int64 access_hash) {
  init(latitude, longitude, access_hash);
}

Location::Location(const tl_object_ptr<secret_api::decryptedMessageMediaGeoPoint> &geo_point)
    : Location(geo_point->lat_, geo_point->long_, 0) {
}

Location::Location(const tl_object_ptr<telegram_api::GeoPoint> &geo_point_ptr) {
  if (geo_point_ptr == nullptr) {
    return;
  }
  switch (geo_point_ptr->get_id()) {
    case telegram_api::geoPointEmpty::ID:
      break;
    case telegram_api::geoPoint::ID: {
      auto geo_point = static_cast<const telegram_api::geoPoint *>(geo_point_ptr.get());
      init(geo_point->lat_, geo_point->long_, geo_point->access_hash_);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

Location::Location(const tl_object_ptr<td_api::location> &location) {
  if (location == nullptr) {
    return;
  }

  init(location->latitude_, location->longitude_, 0);
}

bool Location::empty() const {
  return is_empty_;
}

bool Location::is_valid_map_point() const {
  const double MAX_VALID_MAP_LATITUDE = 85.05112877;
  return !empty() && std::abs(latitude_) <= MAX_VALID_MAP_LATITUDE;
}

tl_object_ptr<td_api::location> Location::get_location_object() const {
  if (empty()) {
    return nullptr;
  }
  return make_tl_object<td_api::location>(latitude_, longitude_);
}

tl_object_ptr<telegram_api::InputGeoPoint> Location::get_input_geo_point() const {
  if (empty()) {
    LOG(ERROR) << "Location is empty";
    return make_tl_object<telegram_api::inputGeoPointEmpty>();
  }

  return make_tl_object<telegram_api::inputGeoPoint>(latitude_, longitude_);
}

tl_object_ptr<telegram_api::inputMediaGeoPoint> Location::get_input_media_geo_point() const {
  return make_tl_object<telegram_api::inputMediaGeoPoint>(get_input_geo_point());
}

SecretInputMedia Location::get_secret_input_media_geo_point() const {
  return SecretInputMedia{nullptr, make_tl_object<secret_api::decryptedMessageMediaGeoPoint>(latitude_, longitude_)};
}

bool operator==(const Location &lhs, const Location &rhs) {
  if (lhs.is_empty_) {
    return rhs.is_empty_;
  }
  return !rhs.is_empty_ && std::abs(lhs.latitude_ - rhs.latitude_) < 1e-6 &&
         std::abs(lhs.longitude_ - rhs.longitude_) < 1e-6;
}

bool operator!=(const Location &lhs, const Location &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Location &location) {
  if (location.empty()) {
    return string_builder << "Location[empty]";
  }
  return string_builder << "Location[latitude = " << location.latitude_ << ", longitude = " << location.longitude_
                        << "]";
}

Venue::Venue(const tl_object_ptr<telegram_api::GeoPoint> &geo_point_ptr, string title, string address, string provider,
             string id, string type)
    : location_(geo_point_ptr)
    , title_(std::move(title))
    , address_(std::move(address))
    , provider_(std::move(provider))
    , id_(std::move(id))
    , type_(std::move(type)) {
}

Venue::Venue(Location location, string title, string address, string provider, string id, string type)
    : location_(location)
    , title_(std::move(title))
    , address_(std::move(address))
    , provider_(std::move(provider))
    , id_(std::move(id))
    , type_(std::move(type)) {
}

Venue::Venue(const tl_object_ptr<td_api::venue> &venue)
    : location_(venue->location_)
    , title_(venue->title_)
    , address_(venue->address_)
    , provider_(venue->provider_)
    , id_(venue->id_)
    , type_(venue->type_) {
}

bool Venue::empty() const {
  return location_.empty();
}

const Location &Venue::location() const {
  return location_;
}

tl_object_ptr<td_api::venue> Venue::get_venue_object() const {
  return make_tl_object<td_api::venue>(location_.get_location_object(), title_, address_, provider_, id_, type_);
}

tl_object_ptr<telegram_api::inputMediaVenue> Venue::get_input_media_venue() const {
  return make_tl_object<telegram_api::inputMediaVenue>(location_.get_input_geo_point(), title_, address_, provider_,
                                                       id_, type_);
}

SecretInputMedia Venue::get_secret_input_media_venue() const {
  return SecretInputMedia{nullptr,
                          make_tl_object<secret_api::decryptedMessageMediaVenue>(
                              location_.get_latitude(), location_.get_longitude(), title_, address_, provider_, id_)};
}

tl_object_ptr<telegram_api::inputBotInlineMessageMediaVenue> Venue::get_input_bot_inline_message_media_venue(
    int32 flags, tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup) const {
  return make_tl_object<telegram_api::inputBotInlineMessageMediaVenue>(
      flags, location_.get_input_geo_point(), title_, address_, provider_, id_, type_, std::move(reply_markup));
}

bool operator==(const Venue &lhs, const Venue &rhs) {
  return lhs.location_ == rhs.location_ && lhs.title_ == rhs.title_ && lhs.address_ == rhs.address_ &&
         lhs.provider_ == rhs.provider_ && lhs.id_ == rhs.id_ && lhs.type_ == rhs.type_;
}

bool operator!=(const Venue &lhs, const Venue &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Venue &venue) {
  return string_builder << "Venue[location = " << venue.location_ << ", title = " << venue.title_
                        << ", address = " << venue.address_ << ", provider = " << venue.provider_
                        << ", id = " << venue.id_ << ", type = " << venue.type_ << "]";
}

}  // namespace td
