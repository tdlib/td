//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ActiveStoryState.h"

namespace td {

bool ActiveStoryState::has_unread_stories() const {
  CHECK(!has_live_story_);
  return max_active_story_id_.get() > max_read_story_id_.get();
}

bool operator==(const ActiveStoryState &lhs, const ActiveStoryState &rhs) {
  if (lhs.has_live_story_ != rhs.has_live_story_) {
    return false;
  }
  if (lhs.has_live_story_) {
    return lhs.max_active_story_id_ == rhs.max_active_story_id_;
  }
  return lhs.has_unread_stories() == rhs.has_unread_stories();
}

bool operator!=(const ActiveStoryState &lhs, const ActiveStoryState &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
