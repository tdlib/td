//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BotVerification.h"

#include "td/telegram/MessageEntity.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void BotVerification::store(StorerT &storer) const {
  bool has_description_entities = !description_.entities.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_description_entities);
  END_STORE_FLAGS();
  td::store(bot_user_id_, storer);
  td::store(icon_, storer);
  td::store(description_.text, storer);
  if (has_description_entities) {
    td::store(description_.entities, storer);
  }
}

template <class ParserT>
void BotVerification::parse(ParserT &parser) {
  bool has_description_entities;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(has_description_entities);
  END_PARSE_FLAGS();
  td::parse(bot_user_id_, parser);
  td::parse(icon_, parser);
  td::parse(description_.text, parser);
  if (has_description_entities) {
    td::parse(description_.entities, parser);
  }
}

}  // namespace td
