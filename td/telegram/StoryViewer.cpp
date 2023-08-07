//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryViewer.h"

#include "td/telegram/ContactsManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

td_api::object_ptr<td_api::storyViewer> StoryViewer::get_story_viewer_object(ContactsManager *contacts_manager) const {
  return td_api::make_object<td_api::storyViewer>(
      contacts_manager->get_user_id_object(user_id_, "get_story_viewer_object"), date_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewer &viewer) {
  return string_builder << '[' << viewer.user_id_ << " at " << viewer.date_ << ']';
}

StoryViewers::StoryViewers(vector<telegram_api::object_ptr<telegram_api::storyView>> &&story_views) {
  for (auto &story_view : story_views) {
    UserId user_id(story_view->user_id_);
    if (!user_id.is_valid()) {
      LOG(ERROR) << "Receive " << user_id << " as story viewer";
      continue;
    }
    story_viewers_.emplace_back(user_id, story_view->date_);
  }
}

vector<UserId> StoryViewers::get_user_ids() const {
  return transform(story_viewers_, [](auto &viewer) { return viewer.get_user_id(); });
}

td_api::object_ptr<td_api::storyViewers> StoryViewers::get_story_viewers_object(
    ContactsManager *contacts_manager) const {
  return td_api::make_object<td_api::storyViewers>(
      transform(story_viewers_, [contacts_manager](const StoryViewer &story_viewer) {
        return story_viewer.get_story_viewer_object(contacts_manager);
      }));
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewers &viewers) {
  return string_builder << viewers.story_viewers_;
}

}  // namespace td
