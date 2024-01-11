//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageForwardInfo.h"
#include "td/telegram/MessageOrigin.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageForwardInfo::store(StorerT &storer) const {
  bool has_from = from_dialog_id.is_valid() && from_message_id.is_valid();
  bool has_psa_type = !psa_type.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_imported);
  STORE_FLAG(has_from);
  STORE_FLAG(has_psa_type);
  END_STORE_FLAGS();
  td::store(origin, storer);
  td::store(date, storer);
  if (has_from) {
    td::store(from_dialog_id, storer);
    td::store(from_message_id, storer);
  }
  if (has_psa_type) {
    td::store(psa_type, storer);
  }
}

template <class ParserT>
void MessageForwardInfo::parse(ParserT &parser) {
  bool has_from;
  bool has_psa_type;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_imported);
  PARSE_FLAG(has_from);
  PARSE_FLAG(has_psa_type);
  END_PARSE_FLAGS();
  td::parse(origin, parser);
  td::parse(date, parser);
  if (has_from) {
    td::parse(from_dialog_id, parser);
    td::parse(from_message_id, parser);
  }
  if (has_psa_type) {
    td::parse(psa_type, parser);
  }
}

}  // namespace td
