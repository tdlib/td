//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BusinessBotRights.h"
#include "td/telegram/BusinessBotRights.hpp"
#include "td/telegram/BusinessConnectedBot.h"
#include "td/telegram/BusinessRecipients.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void BusinessConnectedBot::store(StorerT &storer) const {
  bool can_reply = false;
  bool has_rights = true;
  bool has_device = !device_.empty();
  bool has_location = !location_.empty();
  bool has_date = date_ != 0;
  string location_;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(can_reply);
  STORE_FLAG(has_rights);
  STORE_FLAG(has_device);
  STORE_FLAG(has_location);
  STORE_FLAG(has_date);
  END_STORE_FLAGS();
  td::store(user_id_, storer);
  td::store(recipients_, storer);
  td::store(rights_, storer);
  if (has_device) {
    td::store(device_, storer);
  }
  if (has_location) {
    td::store(location_, storer);
  }
  if (has_date) {
    td::store(date_, storer);
  }
}

template <class ParserT>
void BusinessConnectedBot::parse(ParserT &parser) {
  bool can_reply;
  bool has_rights;
  bool has_device;
  bool has_location;
  bool has_date;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(can_reply);
  PARSE_FLAG(has_rights);
  PARSE_FLAG(has_device);
  PARSE_FLAG(has_location);
  PARSE_FLAG(has_date);
  END_PARSE_FLAGS();
  td::parse(user_id_, parser);
  td::parse(recipients_, parser);
  if (has_rights) {
    td::parse(rights_, parser);
  } else {
    rights_ = BusinessBotRights::legacy(can_reply);
  }
  if (has_device) {
    td::parse(device_, parser);
  }
  if (has_location) {
    td::parse(location_, parser);
  }
  if (has_date) {
    td::parse(date_, parser);
  }
}

}  // namespace td
