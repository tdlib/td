//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/StoryContent.h"
#include "td/telegram/StoryContentType.h"
#include "td/telegram/Td.h"

namespace td {

StoryManager::StoryManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

StoryManager::~StoryManager() = default;

void StoryManager::tear_down() {
  parent_.reset();
}

bool StoryManager::is_local_story_id(StoryId story_id) {
  return story_id.get() < 0;
}

bool StoryManager::is_story_owned(DialogId owner_dialog_id) const {
  return owner_dialog_id == DialogId(td_->contacts_manager_->get_my_id());
}

const StoryManager::Story *StoryManager::get_story(StoryFullId story_full_id) const {
  return stories_.get_pointer(story_full_id);
}

StoryManager::Story *StoryManager::get_story_editable(StoryFullId story_full_id) {
  return stories_.get_pointer(story_full_id);
}

td_api::object_ptr<td_api::story> StoryManager::get_story_object(StoryFullId story_full_id) const {
  return get_story_object(story_full_id, get_story(story_full_id));
}

td_api::object_ptr<td_api::story> StoryManager::get_story_object(StoryFullId story_full_id, const Story *story) const {
  if (story == nullptr) {
    return nullptr;
  }
  auto dialog_id = story_full_id.get_dialog_id();
  bool is_owned = is_story_owned(dialog_id);
  if (!is_owned && !story->is_pinned_ && G()->unix_time() >= story->expire_date_) {
    return nullptr;
  }

  td_api::object_ptr<td_api::userPrivacySettingRules> privacy_rules;
  if (is_owned) {
    privacy_rules = story->privacy_rules_.get_user_privacy_setting_rules_object(td_);
  }

  CHECK(dialog_id.get_type() == DialogType::User);
  return td_api::make_object<td_api::story>(
      story_full_id.get_story_id().get(),
      td_->contacts_manager_->get_user_id_object(dialog_id.get_user_id(), "get_story_object"), story->date_,
      story->is_pinned_, story->interaction_info_.get_story_interaction_info_object(td_), std::move(privacy_rules),
      story->is_public_, story->is_for_close_friends_, get_story_content_object(td_, story->content_.get()),
      get_formatted_text_object(story->caption_, true, -1));
}

StoryId StoryManager::on_get_story(DialogId owner_dialog_id,
                                   telegram_api::object_ptr<telegram_api::storyItem> &&story_item) {
  CHECK(story_item != nullptr);
  StoryId story_id(story_item->id_);
  if (!story_id.is_valid() || is_local_story_id(story_id)) {
    return StoryId();
  }

  StoryFullId story_full_id{owner_dialog_id, story_id};
  auto story = get_story_editable(story_full_id);
  bool is_changed = false;
  bool need_save_to_database = false;
  if (story == nullptr) {
    auto s = make_unique<Story>();
    story = s.get();
    stories_.set(story_full_id, std::move(s));
  }
  CHECK(story != nullptr);

  bool is_bot = td_->auth_manager_->is_bot();
  auto caption =
      get_message_text(td_->contacts_manager_.get(), std::move(story_item->caption_), std::move(story_item->entities_),
                       true, is_bot, story_item->date_, false, "on_get_story");
  auto content = get_story_content(td_, std::move(story_item->media_), owner_dialog_id);
  if (content == nullptr) {
    return StoryId();
  }
  auto content_type = content->get_type();
  if (story->content_ == nullptr || story->content_->get_type() != content_type) {
    story->content_ = std::move(content);
    is_changed = true;
  } else {
    merge_story_contents(td_, story->content_.get(), content.get(), owner_dialog_id, false, need_save_to_database,
                         is_changed);
  }

  auto privacy_rules = UserPrivacySettingRules::get_user_privacy_setting_rules(td_, std::move(story_item->privacy_));
  auto interaction_info = StoryInteractionInfo(td_, std::move(story_item->views_));
  if (story->is_pinned_ != story_item->pinned_ || story->is_public_ != story_item->public_ ||
      story->is_for_close_friends_ != story_item->close_friends_ || story->date_ != story_item->date_ ||
      story->expire_date_ != story_item->expire_date_ || !(story->privacy_rules_ == privacy_rules) ||
      story->interaction_info_ != interaction_info || story->caption_ != caption) {
    story->is_pinned_ = story_item->pinned_;
    story->is_public_ = story_item->public_;
    story->is_for_close_friends_ = story_item->close_friends_;
    story->date_ = story_item->date_;
    story->expire_date_ = story_item->expire_date_;
    story->privacy_rules_ = std::move(privacy_rules);
    story->interaction_info_ = std::move(interaction_info);
    story->caption_ = std::move(caption);
    is_changed = true;
  }

  if (is_changed || need_save_to_database) {
    // save_story(story, story_id);
  }
  if (is_changed) {
    // send_closure(G()->td(), &Td::send_update, td_api::make_object<td_api::updateStory>(get_story_object(story_id, story)));
  }
  return story_id;
}

}  // namespace td
