//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Global.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/Version.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

namespace td {

class Location {
  bool is_empty_ = true;
  double latitude_ = 0.0;
  double longitude_ = 0.0;
  int64 access_hash_ = 0;

  friend bool operator==(const Location &lhs, const Location &rhs);
  friend bool operator!=(const Location &lhs, const Location &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const Location &location);

  void init(double latitude, double longitude, int64 access_hash);

 public:
  Location() = default;

  Location(double latitude, double longitude, int64 access_hash);

  explicit Location(const tl_object_ptr<secret_api::decryptedMessageMediaGeoPoint> &geo_point);

  explicit Location(const tl_object_ptr<telegram_api::GeoPoint> &geo_point_ptr);

  explicit Location(const tl_object_ptr<td_api::location> &location);

  bool empty() const;

  bool is_valid_map_point() const;

  tl_object_ptr<td_api::location> get_location_object() const;

  tl_object_ptr<telegram_api::InputGeoPoint> get_input_geo_point() const;

  tl_object_ptr<telegram_api::inputMediaGeoPoint> get_input_media_geo_point() const;

  double get_latitude() const {
    return latitude_;
  }

  double get_longitude() const {
    return longitude_;
  }

  int64 get_access_hash() const {
    return access_hash_;
  }

  void set_access_hash(int64 access_hash) {
    access_hash_ = access_hash;
  }

  SecretInputMedia get_secret_input_media_geo_point() const;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    bool has_access_hash = access_hash_ != 0;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_empty_);
    STORE_FLAG(has_access_hash);
    END_STORE_FLAGS();
    store(latitude_, storer);
    store(longitude_, storer);
    if (has_access_hash) {
      store(access_hash_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    bool has_access_hash;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_empty_);
    PARSE_FLAG(has_access_hash);
    END_PARSE_FLAGS();
    parse(latitude_, parser);
    parse(longitude_, parser);
    if (has_access_hash) {
      parse(access_hash_, parser);
      G()->add_location_access_hash(latitude_, longitude_, access_hash_);
    }
  }
};

bool operator==(const Location &lhs, const Location &rhs);
bool operator!=(const Location &lhs, const Location &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const Location &location);

class Venue {
  Location location_;
  string title_;
  string address_;
  string provider_;
  string id_;
  string type_;

  friend bool operator==(const Venue &lhs, const Venue &rhs);
  friend bool operator!=(const Venue &lhs, const Venue &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const Venue &venue);

 public:
  Venue() = default;

  Venue(const tl_object_ptr<telegram_api::GeoPoint> &geo_point_ptr, string title, string address, string provider,
        string id, string type);

  Venue(Location location, string title, string address, string provider, string id, string type);

  explicit Venue(const tl_object_ptr<td_api::venue> &venue);

  bool empty() const;

  const Location &location() const;

  void set_access_hash(int64 access_hash) {
    location_.set_access_hash(access_hash);
  }

  tl_object_ptr<td_api::venue> get_venue_object() const;

  tl_object_ptr<telegram_api::inputMediaVenue> get_input_media_venue() const;

  SecretInputMedia get_secret_input_media_venue() const;

  // TODO very strange function
  tl_object_ptr<telegram_api::inputBotInlineMessageMediaVenue> get_input_bot_inline_message_media_venue(
      int32 flags, tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup) const;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(location_, storer);
    store(title_, storer);
    store(address_, storer);
    store(provider_, storer);
    store(id_, storer);
    store(type_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(location_, parser);
    parse(title_, parser);
    parse(address_, parser);
    parse(provider_, parser);
    parse(id_, parser);
    if (parser.version() >= static_cast<int32>(Version::AddVenueType)) {
      parse(type_, parser);
    }
  }
};

bool operator==(const Venue &lhs, const Venue &rhs);
bool operator!=(const Venue &lhs, const Venue &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const Venue &venue);

}  // namespace td
