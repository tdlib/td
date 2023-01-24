//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/EmojiGroup.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void EmojiGroup::store(StorerT &storer) const {
  td::store(title_, storer);
  td::store(icon_custom_emoji_id_, storer);
  td::store(emojis_, storer);
}

template <class ParserT>
void EmojiGroup::parse(ParserT &parser) {
  td::parse(title_, parser);
  td::parse(icon_custom_emoji_id_, parser);
  td::parse(emojis_, parser);
}

template <class StorerT>
void EmojiGroupList::store(StorerT &storer) const {
  td::store(used_language_codes_, storer);
  td::store(hash_, storer);
  td::store(emoji_groups_, storer);
}

template <class ParserT>
void EmojiGroupList::parse(ParserT &parser) {
  td::parse(used_language_codes_, parser);
  td::parse(hash_, parser);
  td::parse(emoji_groups_, parser);
}

}  // namespace td
