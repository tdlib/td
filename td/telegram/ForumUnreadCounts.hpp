//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ForumUnreadCounts.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void ForumUnreadCounts::store(StorerT &storer) const {
  td::store(topic_count_, storer);
  td::store(unmuted_topic_count_, storer);
  td::store(message_count_, storer);
  td::store(muted_message_count_, storer);
}

template <class ParserT>
void ForumUnreadCounts::parse(ParserT &parser) {
  td::parse(topic_count_, parser);
  td::parse(unmuted_topic_count_, parser);
  td::parse(message_count_, parser);
  td::parse(muted_message_count_, parser);
}

}  // namespace td
