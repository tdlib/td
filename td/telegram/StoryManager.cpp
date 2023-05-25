//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/StoryContent.h"
#include "td/telegram/StoryContentType.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "tddb/td/db/binlog/BinlogHelper.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
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

class GetPinnedStoriesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::stories_stories>> promise_;

 public:
  explicit GetPinnedStoriesQuery(Promise<telegram_api::object_ptr<telegram_api::stories_stories>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(UserId user_id, StoryId offset_story_id, int32 limit) {
    auto r_input_user = td_->contacts_manager_->get_input_user(user_id);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    send_query(G()->net_query_creator().create(
        telegram_api::stories_getPinnedStories(r_input_user.move_as_ok(), offset_story_id.get(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_getPinnedStories>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetPinnedStoriesQuery: " << to_string(result);
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetUserStoriesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::stories_userStories>> promise_;

 public:
  explicit GetUserStoriesQuery(Promise<telegram_api::object_ptr<telegram_api::stories_userStories>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(UserId user_id) {
    auto r_input_user = td_->contacts_manager_->get_input_user(user_id);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    send_query(G()->net_query_creator().create(telegram_api::stories_getUserStories(r_input_user.move_as_ok())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_getUserStories>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetUserStoriesQuery: " << to_string(result);
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class StoryManager::SendStoryQuery final : public Td::ResultHandler {
  FileId file_id_;
  unique_ptr<StoryManager::PendingStory> pending_story_;

 public:
  void send(FileId file_id, unique_ptr<PendingStory> pending_story,
            telegram_api::object_ptr<telegram_api::InputFile> input_file) {
    file_id_ = file_id;
    pending_story_ = std::move(pending_story);
    CHECK(pending_story_ != nullptr);

    const auto *story = pending_story_->story_.get();
    const StoryContent *content = story->content_.get();
    auto input_media = get_story_content_input_media(td_, content, std::move(input_file));
    CHECK(input_media != nullptr);

    const FormattedText &caption = story->caption_;
    auto entities = get_input_message_entities(td_->contacts_manager_.get(), &caption, "SendStoryQuery");
    auto privacy_rules = story->privacy_rules_.get_input_privacy_rules(td_);
    int32 flags = 0;
    if (!caption.text.empty()) {
      flags |= telegram_api::stories_sendStory::CAPTION_MASK;
    }
    if (!entities.empty()) {
      flags |= telegram_api::stories_sendStory::ENTITIES_MASK;
    }
    if (pending_story_->story_->is_pinned_) {
      flags |= telegram_api::stories_sendStory::PINNED_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::stories_sendStory(flags, false /*ignored*/, false /*ignored*/, std::move(input_media),
                                        caption.text, std::move(entities), std::move(privacy_rules),
                                        pending_story_->random_id_, 86400),
        {{pending_story_->dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_sendStory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendStoryQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), Promise<Unit>());

    td_->file_manager_->delete_partial_remote_location(file_id_);
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SendStoryQuery: " << status;

    if (G()->close_flag() && G()->use_message_database()) {
      // do not send error, message will be re-sent
      return;
    }

    if (begins_with(status.message(), "FILE_PART_") && ends_with(status.message(), "_MISSING")) {
      td_->story_manager_->on_send_story_file_part_missing(std::move(pending_story_),
                                                           to_integer<int32>(status.message().substr(10)));
      return;
    } else {
      td_->file_manager_->delete_partial_remote_location(file_id_);
    }
  }
};

class StoryManager::UploadMediaCallback final : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file) final {
    send_closure_later(G()->story_manager(), &StoryManager::on_upload_story, file_id, std::move(input_file));
  }
  void on_upload_encrypted_ok(FileId file_id,
                              telegram_api::object_ptr<telegram_api::InputEncryptedFile> input_file) final {
    UNREACHABLE();
  }
  void on_upload_secure_ok(FileId file_id, telegram_api::object_ptr<telegram_api::InputSecureFile> input_file) final {
    UNREACHABLE();
  }
  void on_upload_error(FileId file_id, Status error) final {
    send_closure_later(G()->story_manager(), &StoryManager::on_upload_story_error, file_id, std::move(error));
  }
};

StoryManager::PendingStory::PendingStory(DialogId dialog_id, StoryId story_id, uint64 log_event_id,
                                         uint32 send_story_num, int64 random_id, unique_ptr<Story> &&story)
    : dialog_id_(dialog_id)
    , story_id_(story_id)
    , log_event_id_(log_event_id)
    , send_story_num_(send_story_num)
    , random_id_(random_id)
    , story_(std::move(story)) {
}

StoryManager::StoryManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_media_callback_ = std::make_shared<UploadMediaCallback>();
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

void StoryManager::get_dialog_pinned_stories(DialogId owner_dialog_id, StoryId from_story_id, int32 limit,
                                             Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  if (!td_->messages_manager_->have_dialog_force(owner_dialog_id, "get_dialog_pinned_stories")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td_->messages_manager_->have_input_peer(owner_dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }
  if (owner_dialog_id.get_type() != DialogType::User) {
    return promise.set_value(td_api::make_object<td_api::stories>());
  }

  if (is_local_story_id(from_story_id)) {
    return promise.set_error(Status::Error(400, "Invalid value of parameter from_story_id specified"));
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), owner_dialog_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::stories_stories>> &&result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StoryManager::on_get_dialog_pinned_stories, owner_dialog_id, result.move_as_ok(),
                     std::move(promise));
      });
  td_->create_handler<GetPinnedStoriesQuery>(std::move(query_promise))
      ->send(owner_dialog_id.get_user_id(), from_story_id, limit);
}

void StoryManager::on_get_dialog_pinned_stories(DialogId owner_dialog_id,
                                                telegram_api::object_ptr<telegram_api::stories_stories> &&stories,
                                                Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto result = on_get_stories(owner_dialog_id, std::move(stories));
  promise.set_value(get_stories_object(result.first, transform(result.second, [owner_dialog_id](StoryId story_id) {
                                         return StoryFullId(owner_dialog_id, story_id);
                                       })));
}

void StoryManager::get_dialog_expiring_stories(DialogId owner_dialog_id,
                                               Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  if (!td_->messages_manager_->have_dialog_force(owner_dialog_id, "get_dialog_pinned_stories")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td_->messages_manager_->have_input_peer(owner_dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the chat"));
  }
  if (owner_dialog_id.get_type() != DialogType::User) {
    return promise.set_value(td_api::make_object<td_api::stories>());
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), owner_dialog_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::stories_userStories>> &&result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StoryManager::on_get_dialog_expiring_stories, owner_dialog_id, result.move_as_ok(),
                     std::move(promise));
      });
  td_->create_handler<GetUserStoriesQuery>(std::move(query_promise))->send(owner_dialog_id.get_user_id());
}

void StoryManager::on_get_dialog_expiring_stories(DialogId owner_dialog_id,
                                                  telegram_api::object_ptr<telegram_api::stories_userStories> &&stories,
                                                  Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  td_->contacts_manager_->on_get_users(std::move(stories->users_), "on_get_dialog_expiring_stories");
  auto story_ids = on_get_stories(owner_dialog_id, std::move(stories->stories_->stories_));
  promise.set_value(get_stories_object(-1, transform(story_ids, [owner_dialog_id](StoryId story_id) {
    return StoryFullId(owner_dialog_id, story_id);
  })));
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
      get_formatted_text_object(story->caption_, true, get_story_content_duration(td_, story->content_.get())));
}

td_api::object_ptr<td_api::stories> StoryManager::get_stories_object(int32 total_count,
                                                                     const vector<StoryFullId> &story_full_ids) const {
  if (total_count == -1) {
    total_count = static_cast<int32>(story_full_ids.size());
  }
  return td_api::make_object<td_api::stories>(total_count, transform(story_full_ids, [this](StoryFullId story_full_id) {
                                                return get_story_object(story_full_id);
                                              }));
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
  auto story_ids = on_get_stories(owner_dialog_id, std::move(stories->stories_));
  auto total_count = stories->count_;
  if (total_count < static_cast<int32>(story_ids.size())) {
    LOG(ERROR) << "Expected at most " << total_count << " stories, but receive " << story_ids.size();
    total_count = static_cast<int32>(story_ids.size());
  }
  return {total_count, std::move(story_ids)};
}

vector<StoryId> StoryManager::on_get_stories(DialogId owner_dialog_id,
                                             vector<telegram_api::object_ptr<telegram_api::StoryItem>> &&stories) {
  vector<StoryId> story_ids;
  for (auto &story : stories) {
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
  return story_ids;
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

void StoryManager::send_story(td_api::object_ptr<td_api::InputStoryContent> &&input_story_content,
                              td_api::object_ptr<td_api::formattedText> &&input_caption,
                              td_api::object_ptr<td_api::userPrivacySettingRules> &&rules, bool is_pinned,
                              Promise<td_api::object_ptr<td_api::story>> &&promise) {
  if (input_story_content == nullptr) {
    return promise.set_error(Status::Error(400, "Can't send story without content"));
  }
  bool is_bot = td_->auth_manager_->is_bot();
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  TRY_RESULT_PROMISE(promise, content, get_input_story_content(td_, std::move(input_story_content), dialog_id));
  TRY_RESULT_PROMISE(promise, caption,
                     get_formatted_text(td_, DialogId(), std::move(input_caption), is_bot, true, false, false));
  TRY_RESULT_PROMISE(promise, privacy_rules,
                     UserPrivacySettingRules::get_user_privacy_setting_rules(td_, std::move(rules)));

  auto story = make_unique<Story>();
  story->date_ = G()->unix_time();
  story->expire_date_ = std::numeric_limits<int32>::max();
  story->is_pinned_ = is_pinned;
  story->privacy_rules_ = std::move(privacy_rules);
  story->content_ = std::move(content);
  story->caption_ = std::move(caption);

  int64 random_id;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0);

  auto story_ptr = story.get();

  do_send_story(dialog_id, StoryId(), 0 /*log_event_id*/, ++send_story_count_, random_id, std::move(story), {});

  promise.set_value(get_story_object({dialog_id, StoryId()}, story_ptr));
}

void StoryManager::do_send_story(DialogId dialog_id, StoryId story_id, uint64 log_event_id, uint32 send_story_num,
                                 int64 random_id, unique_ptr<Story> &&story, vector<int> bad_parts) {
  CHECK(story != nullptr);
  auto content = story->content_.get();
  CHECK(content != nullptr);

  FileId file_id = get_story_content_any_file_id(td_, content);
  CHECK(file_id.is_valid());

  LOG(INFO) << "Ask to upload file " << file_id << " with bad parts " << bad_parts;
  auto pending_story =
      td::make_unique<PendingStory>(dialog_id, story_id, log_event_id, send_story_num, random_id, std::move(story));
  bool is_inserted = being_uploaded_files_.emplace(file_id, std::move(pending_story)).second;
  CHECK(is_inserted);
  // need to call resume_upload synchronously to make upload process consistent with being_uploaded_files_
  // and to send is_uploading_active == true in response
  td_->file_manager_->resume_upload(file_id, std::move(bad_parts), upload_media_callback_, 1, send_story_num);
}

void StoryManager::on_upload_story(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "File " << file_id << " has been uploaded";

  auto it = being_uploaded_files_.find(file_id);
  if (it == being_uploaded_files_.end()) {
    // callback may be called just before the file upload was canceled
    return;
  }
  CHECK(input_file != nullptr);

  auto pending_story = std::move(it->second);

  being_uploaded_files_.erase(it);

  td_->create_handler<SendStoryQuery>()->send(file_id, std::move(pending_story), std::move(input_file));
}

void StoryManager::on_upload_story_error(FileId file_id, Status status) {
  if (G()->close_flag()) {
    // do not fail upload if closing
    return;
  }

  LOG(INFO) << "File " << file_id << " has upload error " << status;

  auto it = being_uploaded_files_.find(file_id);
  if (it == being_uploaded_files_.end()) {
    // callback may be called just before the file upload was canceled
    return;
  }

  auto pending_story = std::move(it->second);

  if (pending_story->log_event_id_ != 0) {
    binlog_erase(G()->td_db()->get_binlog(), pending_story->log_event_id_);
  }

  being_uploaded_files_.erase(it);
}

void StoryManager::on_send_story_file_part_missing(unique_ptr<PendingStory> &&pending_story, int bad_part) {
  do_send_story(pending_story->dialog_id_, pending_story->story_id_, pending_story->log_event_id_,
                pending_story->send_story_num_, pending_story->random_id_, std::move(pending_story->story_),
                {bad_part});
}

}  // namespace td
