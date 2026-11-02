//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

// unread state of the known topics of a forum
struct ForumUnreadCounts {
  int32 topic_count_ = 0;
  int32 unmuted_topic_count_ = 0;
  int32 message_count_ = 0;
  int32 muted_message_count_ = 0;

  ForumUnreadCounts &operator+=(const ForumUnreadCounts &other) {
    topic_count_ += other.topic_count_;
    unmuted_topic_count_ += other.unmuted_topic_count_;
    message_count_ += other.message_count_;
    muted_message_count_ += other.muted_message_count_;
    return *this;
  }

  ForumUnreadCounts &operator-=(const ForumUnreadCounts &other) {
    topic_count_ -= other.topic_count_;
    unmuted_topic_count_ -= other.unmuted_topic_count_;
    message_count_ -= other.message_count_;
    muted_message_count_ -= other.muted_message_count_;
    return *this;
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

inline bool operator==(const ForumUnreadCounts &lhs, const ForumUnreadCounts &rhs) {
  return lhs.topic_count_ == rhs.topic_count_ && lhs.unmuted_topic_count_ == rhs.unmuted_topic_count_ &&
         lhs.message_count_ == rhs.message_count_ && lhs.muted_message_count_ == rhs.muted_message_count_;
}

inline bool operator!=(const ForumUnreadCounts &lhs, const ForumUnreadCounts &rhs) {
  return !(lhs == rhs);
}

inline StringBuilder &operator<<(StringBuilder &string_builder, const ForumUnreadCounts &counts) {
  return string_builder << "[" << counts.topic_count_ << '/' << counts.unmuted_topic_count_ << " topics with "
                        << counts.message_count_ << '/' << counts.muted_message_count_ << " messages]";
}

}  // namespace td
