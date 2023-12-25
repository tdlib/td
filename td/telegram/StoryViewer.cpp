//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryViewer.h"

#include "td/telegram/BlockListId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

StoryViewer::StoryViewer(Td *td, telegram_api::object_ptr<telegram_api::StoryView> &&story_view_ptr) {
  CHECK(story_view_ptr != nullptr);
  if (story_view_ptr->get_id() != telegram_api::storyView::ID) {
    return;
  }
  auto story_view = telegram_api::move_object_as<telegram_api::storyView>(story_view_ptr);
  UserId user_id(story_view->user_id_);
  if (!user_id.is_valid()) {
    return;
  }
  user_id_ = user_id;
  date_ = td::max(story_view->date_, static_cast<int32>(0));
  is_blocked_ = story_view->blocked_;
  is_blocked_for_stories_ = story_view->blocked_my_stories_from_;
  reaction_type_ = ReactionType(story_view->reaction_);

  td->messages_manager_->on_update_dialog_is_blocked(DialogId(user_id), is_blocked_, is_blocked_for_stories_);
}

td_api::object_ptr<td_api::storyInteraction> StoryViewer::get_story_interaction_object(
    ContactsManager *contacts_manager) const {
  auto block_list_id = BlockListId(is_blocked_, is_blocked_for_stories_);
  return td_api::make_object<td_api::storyInteraction>(
      contacts_manager->get_user_id_object(user_id_, "get_story_interaction_object"), date_,
      block_list_id.get_block_list_object(), reaction_type_.get_reaction_type_object());
}

bool StoryViewer::is_valid() const {
  return user_id_.is_valid() && date_ > 0;
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewer &viewer) {
  return string_builder << '[' << viewer.user_id_ << " with " << viewer.reaction_type_ << " at " << viewer.date_ << ']';
}

StoryViewers::StoryViewers(Td *td, int32 total_count, int32 total_forward_count, int32 total_reaction_count,
                           vector<telegram_api::object_ptr<telegram_api::StoryView>> &&story_views,
                           string &&next_offset)
    : total_count_(total_count)
    , total_forward_count_(total_forward_count)
    , total_reaction_count_(total_reaction_count)
    , next_offset_(std::move(next_offset)) {
  for (auto &story_view : story_views) {
    StoryViewer story_viewer(td, std::move(story_view));
    if (!story_viewer.is_valid()) {
      LOG(ERROR) << "Receive invalid " << story_viewer << " in story interaction";
      continue;
    }
    story_viewers_.push_back(std::move(story_viewer));
  }
}

vector<UserId> StoryViewers::get_viewer_user_ids() const {
  vector<UserId> result;
  for (const auto &story_viewer : story_viewers_) {
    auto user_id = story_viewer.get_viewer_user_id();
    if (user_id.is_valid()) {
      result.push_back(user_id);
    }
  }
  return result;
}

vector<DialogId> StoryViewers::get_actor_dialog_ids() const {
  return transform(story_viewers_, [](auto &viewer) { return viewer.get_actor_dialog_id(); });
}

td_api::object_ptr<td_api::storyInteractions> StoryViewers::get_story_interactions_object(
    ContactsManager *contacts_manager) const {
  return td_api::make_object<td_api::storyInteractions>(
      total_count_, total_forward_count_, total_reaction_count_,
      transform(story_viewers_,
                [contacts_manager](const StoryViewer &story_viewer) {
                  return story_viewer.get_story_interaction_object(contacts_manager);
                }),
      next_offset_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StoryViewers &viewers) {
  return string_builder << viewers.story_viewers_;
}

}  // namespace td
