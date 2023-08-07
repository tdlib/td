//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

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

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewer &viewer);

 public:
  StoryViewer(UserId user_id, int32 date) : user_id_(user_id), date_(td::max(date, static_cast<int32>(0))) {
  }

  UserId get_user_id() const {
    return user_id_;
  }

  bool is_empty() const {
    return user_id_ == UserId() && date_ == 0;
  }

  td_api::object_ptr<td_api::storyViewer> get_story_viewer_object(ContactsManager *contacts_manager) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewer &viewer);

class StoryViewers {
  vector<StoryViewer> story_viewers_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewers &viewers);

 public:
  StoryViewers() = default;

  explicit StoryViewers(vector<telegram_api::object_ptr<telegram_api::storyView>> &&story_views);

  bool is_empty() const {
    return story_viewers_.empty();
  }

  vector<UserId> get_user_ids() const;

  td_api::object_ptr<td_api::storyViewers> get_story_viewers_object(ContactsManager *contacts_manager) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewers &viewers);

}  // namespace td
