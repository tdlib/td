//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

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

  static ForumTopicId general() {
    return ForumTopicId(1);
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
