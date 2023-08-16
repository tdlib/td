//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ReactionType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class ContactsManager;

class StoryViewer {
  UserId user_id_;
  int32 date_ = 0;
  bool is_blocked_ = false;
  bool is_blocked_for_stories_ = false;
  ReactionType reaction_type_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewer &viewer);

 public:
  StoryViewer(telegram_api::object_ptr<telegram_api::storyView> &&story_view)
      : user_id_(story_view->user_id_)
      , date_(td::max(story_view->date_, static_cast<int32>(0)))
      , is_blocked_(story_view->blocked_)
      , is_blocked_for_stories_(story_view->blocked_my_stories_from_)
      , reaction_type_(story_view->reaction_) {
  }

  UserId get_user_id() const {
    return user_id_;
  }

  td_api::object_ptr<td_api::storyViewer> get_story_viewer_object(ContactsManager *contacts_manager) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewer &viewer);

class StoryViewers {
  int32 total_count_ = 0;
  int32 total_reaction_count_ = 0;
  vector<StoryViewer> story_viewers_;
  string next_offset_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewers &viewers);

 public:
  StoryViewers(int32 total_count, int32 total_reaction_count,
               vector<telegram_api::object_ptr<telegram_api::storyView>> &&story_views, string &&next_offset);

  vector<UserId> get_user_ids() const;

  td_api::object_ptr<td_api::storyViewers> get_story_viewers_object(ContactsManager *contacts_manager) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewers &viewers);

}  // namespace td
