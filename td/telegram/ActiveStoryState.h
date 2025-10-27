//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/StoryId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"

namespace td {

class ActiveStoryState {
  StoryId max_active_story_id_;
  StoryId max_read_story_id_;
  bool has_live_story_ = false;

  friend bool operator==(const ActiveStoryState &lhs, const ActiveStoryState &rhs);

  bool has_unread_stories() const;

 public:
  ActiveStoryState(StoryId max_active_story_id, StoryId max_read_story_id, bool has_live_story)
      : max_active_story_id_(max_active_story_id)
      , max_read_story_id_(max_read_story_id)
      , has_live_story_(has_live_story) {
  }

  td_api::object_ptr<td_api::ActiveStoryState> get_active_story_state_object() const;
};

bool operator==(const ActiveStoryState &lhs, const ActiveStoryState &rhs);

bool operator!=(const ActiveStoryState &lhs, const ActiveStoryState &rhs);

}  // namespace td
