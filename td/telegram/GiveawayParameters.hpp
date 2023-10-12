//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/GiveawayParameters.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void GiveawayParameters::store(StorerT &storer) const {
  bool has_additional_channel_ids = !additional_channel_ids_.empty();
  bool has_country_codes = !country_codes_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(only_new_subscribers_);
  STORE_FLAG(has_additional_channel_ids);
  STORE_FLAG(has_country_codes);
  END_STORE_FLAGS();
  td::store(boosted_channel_id_, storer);
  if (has_additional_channel_ids) {
    td::store(additional_channel_ids_, storer);
  }
  td::store(date_, storer);
  if (has_country_codes) {
    td::store(country_codes_, storer);
  }
}

template <class ParserT>
void GiveawayParameters::parse(ParserT &parser) {
  bool has_additional_channel_ids;
  bool has_country_codes;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(only_new_subscribers_);
  PARSE_FLAG(has_additional_channel_ids);
  PARSE_FLAG(has_country_codes);
  END_PARSE_FLAGS();
  td::parse(boosted_channel_id_, parser);
  if (has_additional_channel_ids) {
    td::parse(additional_channel_ids_, parser);
  }
  td::parse(date_, parser);
  if (has_country_codes) {
    td::parse(country_codes_, parser);
  }
}

}  // namespace td
