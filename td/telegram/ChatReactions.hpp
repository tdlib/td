//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChatReactions.h"
#include "td/telegram/ReactionType.hpp"

#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ChatReactions::store(StorerT &storer) const {
  bool has_reactions = !reaction_types_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(allow_all_);
  STORE_FLAG(allow_custom_);
  STORE_FLAG(has_reactions);
  END_STORE_FLAGS();
  if (has_reactions) {
    td::store(reaction_types_, storer);
  }
}

template <class ParserT>
void ChatReactions::parse(ParserT &parser) {
  bool has_reactions;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(allow_all_);
  PARSE_FLAG(allow_custom_);
  PARSE_FLAG(has_reactions);
  END_PARSE_FLAGS();
  if (has_reactions) {
    td::parse(reaction_types_, parser);
  }
}

}  // namespace td
