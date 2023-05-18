//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryInteractionInfo.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

StoryInteractionInfo::StoryInteractionInfo(Td *td, telegram_api::object_ptr<telegram_api::storyViews> &&story_views) {
  if (story_views == nullptr) {
    return;
  }
  for (auto &viewer_id : story_views->recent_viewers_) {
    UserId user_id(viewer_id);
    if (user_id.is_valid() && td->contacts_manager_->have_min_user(user_id)) {
      if (recent_viewer_user_ids_.size() == MAX_RECENT_VIEWERS) {
        LOG(ERROR) << "Receive too many recent story viewers: " << story_views->recent_viewers_;
        break;
      }
      recent_viewer_user_ids_.push_back(user_id);
    } else {
      LOG(ERROR) << "Receive " << user_id << " as recent viewer";
    }
  }
  view_count_ = story_views->views_count_;
  if (view_count_ < 0) {
    LOG(ERROR) << "Receive " << view_count_ << " story views";
    view_count_ = 0;
  }
}

bool operator==(const StoryInteractionInfo &lhs, const StoryInteractionInfo &rhs) {
  return lhs.recent_viewer_user_ids_ == rhs.recent_viewer_user_ids_ && lhs.view_count_ == rhs.view_count_;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryInteractionInfo &info) {
  return string_builder << info.view_count_ << " views by " << info.recent_viewer_user_ids_;
}

}  // namespace td
