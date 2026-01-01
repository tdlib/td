//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/ForumTopicId.h"

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct ForumTopicFullId {
 private:
  DialogId dialog_id;
  ForumTopicId forum_topic_id;

 public:
  ForumTopicFullId() : dialog_id(), forum_topic_id() {
  }

  ForumTopicFullId(DialogId dialog_id, ForumTopicId forum_topic_id)
      : dialog_id(dialog_id), forum_topic_id(forum_topic_id) {
  }

  bool operator==(const ForumTopicFullId &other) const {
    return dialog_id == other.dialog_id && forum_topic_id == other.forum_topic_id;
  }

  bool operator!=(const ForumTopicFullId &other) const {
    return !(*this == other);
  }

  DialogId get_dialog_id() const {
    return dialog_id;
  }

  ForumTopicId get_forum_topic_id() const {
    return forum_topic_id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    dialog_id.store(storer);
    forum_topic_id.store(storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    dialog_id.parse(parser);
    forum_topic_id.parse(parser);
  }
};

struct ForumTopicFullIdHash {
  uint32 operator()(ForumTopicFullId forum_topic_full_id) const {
    return combine_hashes(DialogIdHash()(forum_topic_full_id.get_dialog_id()),
                          ForumTopicIdHash()(forum_topic_full_id.get_forum_topic_id()));
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, ForumTopicFullId forum_topic_full_id) {
  return string_builder << forum_topic_full_id.get_forum_topic_id() << " in " << forum_topic_full_id.get_dialog_id();
}

}  // namespace td
