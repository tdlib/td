//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
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

td_api::object_ptr<td_api::ActiveStoryState> ActiveStoryState::get_active_story_state_object() const {
  if (!max_active_story_id_.is_server()) {
    return nullptr;
  }
  if (has_live_story_) {
    return td_api::make_object<td_api::activeStoryStateLive>(max_active_story_id_.get());
  }
  if (has_unread_stories()) {
    return td_api::make_object<td_api::activeStoryStateUnread>();
  }
  return td_api::make_object<td_api::activeStoryStateRead>();
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
