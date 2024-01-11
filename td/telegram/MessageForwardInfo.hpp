//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageForwardInfo.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageOrigin.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void MessageForwardInfo::store(StorerT &storer) const {
  bool has_from = from_dialog_id_.is_valid() && from_message_id_.is_valid();
  bool has_psa_type = !psa_type_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_imported_);
  STORE_FLAG(has_from);
  STORE_FLAG(has_psa_type);
  END_STORE_FLAGS();
  td::store(origin_, storer);
  td::store(date_, storer);
  if (has_from) {
    td::store(from_dialog_id_, storer);
    td::store(from_message_id_, storer);
  }
  if (has_psa_type) {
    td::store(psa_type_, storer);
  }
}

template <class ParserT>
void MessageForwardInfo::parse(ParserT &parser) {
  bool has_from;
  bool has_psa_type;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_imported_);
  PARSE_FLAG(has_from);
  PARSE_FLAG(has_psa_type);
  END_PARSE_FLAGS();
  td::parse(origin_, parser);
  td::parse(date_, parser);
  if (has_from) {
    td::parse(from_dialog_id_, parser);
    td::parse(from_message_id_, parser);
    if (!from_dialog_id_.is_valid() || !from_message_id_.is_valid()) {
      from_dialog_id_ = DialogId();
      from_message_id_ = MessageId();
    }
  }
  if (has_psa_type) {
    td::parse(psa_type_, parser);
  }
}

}  // namespace td
