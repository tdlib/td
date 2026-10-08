//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BotVerifierSettings.h"

#include "td/telegram/MessageEntity.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void BotVerifierSettings::store(StorerT &storer) const {
  bool has_description = !description_.text.empty();
  bool has_description_entities = !description_.entities.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_description);
  STORE_FLAG(can_modify_custom_description_);
  STORE_FLAG(has_description_entities);
  END_STORE_FLAGS();
  td::store(icon_, storer);
  td::store(company_, storer);
  if (has_description) {
    td::store(description_.text, storer);
  }
  if (has_description_entities) {
    td::store(description_.entities, storer);
  }
}

template <class ParserT>
void BotVerifierSettings::parse(ParserT &parser) {
  bool has_description;
  bool has_description_entities;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_description);
  PARSE_FLAG(can_modify_custom_description_);
  PARSE_FLAG(has_description_entities);
  END_PARSE_FLAGS();
  td::parse(icon_, parser);
  td::parse(company_, parser);
  if (has_description) {
    td::parse(description_.text, parser);
  }
  if (has_description_entities) {
    td::parse(description_.entities, parser);
  }
}

}  // namespace td
