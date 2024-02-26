//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BusinessInfo.h"
#include "td/telegram/BusinessWorkHours.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void BusinessInfo::store(StorerT &storer) const {
  bool has_location = !is_empty_location(location_);
  bool has_work_hours = !work_hours_.is_empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_location);
  STORE_FLAG(has_work_hours);
  END_STORE_FLAGS();
  if (has_location) {
    td::store(location_, storer);
  }
  if (has_work_hours) {
    td::store(work_hours_, storer);
  }
}

template <class ParserT>
void BusinessInfo::parse(ParserT &parser) {
  bool has_location;
  bool has_work_hours;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_location);
  PARSE_FLAG(has_work_hours);
  END_PARSE_FLAGS();
  if (has_location) {
    td::parse(location_, parser);
  }
  if (has_work_hours) {
    td::parse(work_hours_, parser);
  }
}

}  // namespace td
