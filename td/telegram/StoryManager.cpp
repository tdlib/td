//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/StoryContent.h"
#include "td/telegram/StoryContentType.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class GetStoriesByIDQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  UserId user_id_;

 public:
  explicit GetStoriesByIDQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId user_id, vector<int32> input_story_ids) {
    user_id_ = user_id;
    auto r_input_user = td_->contacts_manager_->get_input_user(user_id_);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    send_query(G()->net_query_creator().create(
        telegram_api::stories_getStoriesByID(r_input_user.move_as_ok(), std::move(input_story_ids))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_getStoriesByID>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetStoriesByIDQuery: " << to_string(result);
    td_->story_manager_->on_get_stories(DialogId(user_id_), std::move(result));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

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

vector<FileId> StoryManager::get_story_file_ids(const Story *story) const {
  if (story == nullptr) {
    return {};
  }
  return get_story_content_file_ids(td_, story->content_.get());
}

void StoryManager::delete_story_files(const Story *story) const {
  for (auto file_id : get_story_file_ids(story)) {
    send_closure(G()->file_manager(), &FileManager::delete_file, file_id, Promise<Unit>(), "delete_story_files");
  }
}

void StoryManager::change_story_files(StoryFullId story_full_id, const Story *story,
                                      const vector<FileId> &old_file_ids) {
  auto new_file_ids = get_story_file_ids(story);
  if (new_file_ids == old_file_ids) {
    return;
  }

  for (auto file_id : old_file_ids) {
    if (!td::contains(new_file_ids, file_id)) {
      send_closure(G()->file_manager(), &FileManager::delete_file, file_id, Promise<Unit>(), "change_story_files");
    }
  }

  auto file_source_id = get_story_file_source_id(story_full_id);
  if (file_source_id.is_valid()) {
    td_->file_manager_->change_files_source(file_source_id, old_file_ids, new_file_ids);
  }
}

StoryId StoryManager::on_get_story(DialogId owner_dialog_id,
                                   telegram_api::object_ptr<telegram_api::storyItem> &&story_item) {
  CHECK(story_item != nullptr);
  StoryId story_id(story_item->id_);
  if (!story_id.is_valid() || is_local_story_id(story_id)) {
    LOG(ERROR) << "Receive " << to_string(story_item);
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
  auto old_file_ids = get_story_file_ids(story);
  if (story->content_ == nullptr || story->content_->get_type() != content_type) {
    story->content_ = std::move(content);
    is_changed = true;
  } else {
    merge_story_contents(td_, story->content_.get(), content.get(), owner_dialog_id, false, need_save_to_database,
                         is_changed);
    story->content_ = std::move(content);
    // story->last_edit_pts_ = 0;
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
    change_story_files(story_full_id, story, old_file_ids);

    // save_story(story, story_id);
  }
  if (is_changed) {
    // send_closure(G()->td(), &Td::send_update, td_api::make_object<td_api::updateStory>(get_story_object(story_id, story)));
  }
  return story_id;
}

std::pair<int32, vector<StoryId>> StoryManager::on_get_stories(
    DialogId owner_dialog_id, telegram_api::object_ptr<telegram_api::stories_stories> &&stories) {
  td_->contacts_manager_->on_get_users(std::move(stories->users_), "on_get_stories");
  vector<StoryId> story_ids;
  for (auto &story : stories->stories_) {
    switch (story->get_id()) {
      case telegram_api::storyItemDeleted::ID:
        LOG(ERROR) << "Receive storyItemDeleted";
        break;
      case telegram_api::storyItemSkipped::ID:
        LOG(ERROR) << "Receive storyItemSkipped";
        break;
      case telegram_api::storyItem::ID: {
        auto story_id = on_get_story(owner_dialog_id, telegram_api::move_object_as<telegram_api::storyItem>(story));
        if (story_id.is_valid()) {
          story_ids.push_back(story_id);
        }
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  auto total_count = stories->count_;
  if (total_count < static_cast<int32>(story_ids.size())) {
    LOG(ERROR) << "Expected at most " << total_count << " stories, but receive " << story_ids.size();
    total_count = static_cast<int32>(story_ids.size());
  }
  return {total_count, std::move(story_ids)};
}

FileSourceId StoryManager::get_story_file_source_id(StoryFullId story_full_id) {
  if (td_->auth_manager_->is_bot()) {
    return FileSourceId();
  }

  auto dialog_id = story_full_id.get_dialog_id();
  auto story_id = story_full_id.get_story_id();
  if (!dialog_id.is_valid() || !story_id.is_valid()) {
    return FileSourceId();
  }

  auto &file_source_id = story_full_id_to_file_source_id_[story_full_id];
  if (!file_source_id.is_valid()) {
    file_source_id = td_->file_reference_manager_->create_story_file_source(story_full_id);
  }
  return file_source_id;
}

void StoryManager::reload_story(StoryFullId story_full_id, Promise<Unit> &&promise) {
  auto dialog_id = story_full_id.get_dialog_id();
  if (dialog_id.get_type() != DialogType::User) {
    return promise.set_error(Status::Error(400, "Unsupported story owner"));
  }
  auto user_id = dialog_id.get_user_id();
  td_->create_handler<GetStoriesByIDQuery>(std::move(promise))->send(user_id, {story_full_id.get_story_id().get()});
}

}  // namespace td
