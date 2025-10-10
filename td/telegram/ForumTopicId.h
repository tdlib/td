//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/ServerMessageId.h"

#include "td/utils/common.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/StringBuilder.h"

#include <type_traits>

namespace td {

class ForumTopicId {
  int32 id_ = 0;

 public:
  ForumTopicId() = default;

  explicit constexpr ForumTopicId(int32 forum_topic_id) : id_(forum_topic_id) {
  }
  template <class T, typename = std::enable_if_t<std::is_convertible<T, int32>::value>>
  ForumTopicId(T forum_topic_id) = delete;

  static ForumTopicId from_top_thread_message_id(MessageId top_thread_message_id) {
    return ForumTopicId(top_thread_message_id.get_server_message_id().get());
  }

  MessageId to_top_thread_message_id() const {
    return MessageId(ServerMessageId(id_));
  }

  static ForumTopicId general() {
    return ForumTopicId(1);
  }

  static vector<ForumTopicId> get_forum_topic_ids(const vector<int32> &input_forum_topic_ids) {
    vector<ForumTopicId> forum_topic_ids;
    forum_topic_ids.reserve(input_forum_topic_ids.size());
    for (auto input_forum_topic_id : input_forum_topic_ids) {
      forum_topic_ids.push_back(ForumTopicId(input_forum_topic_id));
    }
    return forum_topic_ids;
  }

  static vector<int32> get_top_msg_ids(const vector<ForumTopicId> &forum_topic_ids) {
    vector<int32> top_msg_ids;
    top_msg_ids.reserve(forum_topic_ids.size());
    for (auto forum_topic_id : forum_topic_ids) {
      top_msg_ids.push_back(forum_topic_id.get());
    }
    return top_msg_ids;
  }

  bool is_valid() const {
    return id_ > 0;
  }

  int32 get() const {
    return id_;
  }

  bool operator==(const ForumTopicId &other) const {
    return id_ == other.id_;
  }

  bool operator!=(const ForumTopicId &other) const {
    return id_ != other.id_;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_int(id_);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    id_ = parser.fetch_int();
  }
};

struct ForumTopicIdHash {
  uint32 operator()(ForumTopicId forum_topic_id) const {
    return Hash<int32>()(forum_topic_id.get());
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, ForumTopicId forum_topic_id) {
  return string_builder << "topic " << forum_topic_id.get();
}

}  // namespace td
