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
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/logevent/LogEventHelper.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/StoryContent.h"
#include "td/telegram/StoryContentType.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/WebPagesManager.h"

#include "tddb/td/db/binlog/BinlogEvent.h"
#include "tddb/td/db/binlog/BinlogHelper.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"

namespace td {

class ToggleStoriesHiddenQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  UserId user_id_;
  bool are_hidden_ = false;

 public:
  explicit ToggleStoriesHiddenQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId user_id, bool are_hidden) {
    user_id_ = user_id;
    are_hidden_ = are_hidden;
    auto r_input_user = td_->contacts_manager_->get_input_user(user_id_);
    if (r_input_user.is_error()) {
      return on_error(r_input_user.move_as_error());
    }
    send_query(G()->net_query_creator().create(
        telegram_api::contacts_toggleStoriesHidden(r_input_user.move_as_ok(), are_hidden)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::contacts_toggleStoriesHidden>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for ToggleStoriesHiddenQuery: " << result;
    if (result) {
      td_->contacts_manager_->on_update_user_stories_hidden(user_id_, are_hidden_);
    }
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetStoriesByIDQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  UserId user_id_;
  vector<int32> input_story_ids_;

 public:
  explicit GetStoriesByIDQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(UserId user_id, vector<int32> input_story_ids) {
    user_id_ = user_id;
    input_story_ids_ = input_story_ids;
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
    td_->story_manager_->on_get_stories(DialogId(user_id_), std::move(input_story_ids_), std::move(result));
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

class GetStoriesArchiveQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::stories_stories>> promise_;

 public:
  explicit GetStoriesArchiveQuery(Promise<telegram_api::object_ptr<telegram_api::stories_stories>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(StoryId offset_story_id, int32 limit) {
    send_query(G()->net_query_creator().create(telegram_api::stories_getStoriesArchive(offset_story_id.get(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_getStoriesArchive>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetStoriesArchiveQuery: " << to_string(result);
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

class EditStoryPrivacyQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditStoryPrivacyQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StoryId story_id, UserPrivacySettingRules &&privacy_rules) {
    int32 flags = telegram_api::stories_editStory::PRIVACY_RULES_MASK;
    send_query(G()->net_query_creator().create(telegram_api::stories_editStory(
        flags, story_id.get(), nullptr, string(), vector<telegram_api::object_ptr<telegram_api::MessageEntity>>(),
        privacy_rules.get_input_privacy_rules(td_))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_editStory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for EditStoryPrivacyQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (!td_->auth_manager_->is_bot() && status.message() == "STORY_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleStoryPinnedQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleStoryPinnedQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(StoryId story_id, bool is_pinned) {
    send_query(G()->net_query_creator().create(telegram_api::stories_togglePinned({story_id.get()}, is_pinned)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_togglePinned>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for ToggleStoryPinnedQuery: " << ptr;
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteStoriesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteStoriesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<int32> story_ids) {
    send_query(G()->net_query_creator().create(telegram_api::stories_deleteStories(std::move(story_ids))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_deleteStories>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for DeleteStoriesQuery: " << ptr;
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class StoryManager::SendStoryQuery final : public Td::ResultHandler {
  FileId file_id_;
  unique_ptr<PendingStory> pending_story_;

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
    auto period = story->expire_date_ - story->date_;
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
    if (period != 86400) {
      flags |= telegram_api::stories_sendStory::PERIOD_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::stories_sendStory(flags, false /*ignored*/, false /*ignored*/, std::move(input_media),
                                        caption.text, std::move(entities), std::move(privacy_rules),
                                        pending_story_->random_id_, period),
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
      // do not send error, story will be re-sent after restart
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

class StoryManager::EditStoryQuery final : public Td::ResultHandler {
  FileId file_id_;
  unique_ptr<PendingStory> pending_story_;

 public:
  void send(FileId file_id, unique_ptr<PendingStory> pending_story,
            telegram_api::object_ptr<telegram_api::InputFile> input_file, const BeingEditedStory *edited_story) {
    file_id_ = file_id;
    pending_story_ = std::move(pending_story);
    CHECK(pending_story_ != nullptr);

    int32 flags = 0;

    telegram_api::object_ptr<telegram_api::InputMedia> input_media;
    const StoryContent *content = edited_story->content_.get();
    if (content != nullptr) {
      CHECK(input_file != nullptr);
      input_media = get_story_content_input_media(td_, content, std::move(input_file));
      CHECK(input_media != nullptr);
      flags |= telegram_api::stories_editStory::MEDIA_MASK;
    }
    vector<telegram_api::object_ptr<telegram_api::MessageEntity>> entities;
    if (edited_story->edit_caption_) {
      flags |= telegram_api::stories_editStory::CAPTION_MASK;
      flags |= telegram_api::stories_editStory::ENTITIES_MASK;

      entities = get_input_message_entities(td_->contacts_manager_.get(), &edited_story->caption_, "EditStoryQuery");
    }
    send_query(G()->net_query_creator().create(
        telegram_api::stories_editStory(flags, pending_story_->story_id_.get(), std::move(input_media),
                                        edited_story->caption_.text, std::move(entities), Auto()),
        {{StoryFullId{pending_story_->dialog_id_, pending_story_->story_id_}}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::stories_editStory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditStoryQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(
        std::move(ptr), PromiseCreator::lambda([file_id = file_id_, pending_story = std::move(pending_story_)](
                                                   Result<Unit> &&result) mutable {
          send_closure(G()->story_manager(), &StoryManager::on_story_edited, file_id, std::move(pending_story),
                       std::move(result));
        }));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for EditStoryQuery: " << status;

    if (!td_->auth_manager_->is_bot() && status.message() == "STORY_NOT_MODIFIED") {
      return td_->story_manager_->on_story_edited(file_id_, std::move(pending_story_), Status::OK());
    }

    if (G()->close_flag() && G()->use_message_database()) {
      // do not send error, story will be edited after restart
      return;
    }

    if (begins_with(status.message(), "FILE_PART_") && ends_with(status.message(), "_MISSING")) {
      td_->story_manager_->on_send_story_file_part_missing(std::move(pending_story_),
                                                           to_integer<int32>(status.message().substr(10)));
      return;
    }
    td_->story_manager_->on_story_edited(file_id_, std::move(pending_story_), std::move(status));
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
    , was_reuploaded_(false)
    , story_(std::move(story)) {
}

StoryManager::StoryManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_media_callback_ = std::make_shared<UploadMediaCallback>();
}

StoryManager::~StoryManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), story_full_id_to_file_source_id_, stories_,
                                              inaccessible_story_full_ids_, deleted_story_full_ids_, story_messages_);
}

void StoryManager::tear_down() {
  parent_.reset();
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

void StoryManager::toggle_dialog_stories_hidden(DialogId dialog_id, bool are_hidden, Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_dialog_info_force(dialog_id)) {
    return promise.set_error(Status::Error(400, "Story sender not found"));
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the story sender"));
  }
  if (dialog_id.get_type() != DialogType::User) {
    return promise.set_error(Status::Error(400, "Can't archive sender stories"));
  }

  td_->create_handler<ToggleStoriesHiddenQuery>(std::move(promise))->send(dialog_id.get_user_id(), are_hidden);
}

void StoryManager::get_dialog_pinned_stories(DialogId owner_dialog_id, StoryId from_story_id, int32 limit,
                                             Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  if (!td_->messages_manager_->have_dialog_info_force(owner_dialog_id)) {
    return promise.set_error(Status::Error(400, "Story sender not found"));
  }
  if (!td_->messages_manager_->have_input_peer(owner_dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the story sender"));
  }
  if (owner_dialog_id.get_type() != DialogType::User) {
    return promise.set_value(td_api::make_object<td_api::stories>());
  }

  if (from_story_id != StoryId() && !from_story_id.is_server()) {
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
  auto result = on_get_stories(owner_dialog_id, {}, std::move(stories));
  promise.set_value(get_stories_object(result.first, transform(result.second, [owner_dialog_id](StoryId story_id) {
                                         return StoryFullId(owner_dialog_id, story_id);
                                       })));
}

void StoryManager::get_story_archive(StoryId from_story_id, int32 limit,
                                     Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  if (from_story_id != StoryId() && !from_story_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid value of parameter from_story_id specified"));
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::stories_stories>> &&result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StoryManager::on_get_story_archive, result.move_as_ok(), std::move(promise));
      });
  td_->create_handler<GetStoriesArchiveQuery>(std::move(query_promise))->send(from_story_id, limit);
}

void StoryManager::on_get_story_archive(telegram_api::object_ptr<telegram_api::stories_stories> &&stories,
                                        Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  auto result = on_get_stories(dialog_id, {}, std::move(stories));
  promise.set_value(get_stories_object(result.first, transform(result.second, [dialog_id](StoryId story_id) {
                                         return StoryFullId(dialog_id, story_id);
                                       })));
}

void StoryManager::get_dialog_expiring_stories(DialogId owner_dialog_id,
                                               Promise<td_api::object_ptr<td_api::stories>> &&promise) {
  if (!td_->messages_manager_->have_dialog_info_force(owner_dialog_id)) {
    return promise.set_error(Status::Error(400, "Story sender not found"));
  }
  if (!td_->messages_manager_->have_input_peer(owner_dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the story sender"));
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
  CHECK(owner_dialog_id.get_type() == DialogType::User);
  td_->contacts_manager_->on_update_user_has_stories(owner_dialog_id.get_user_id(), !story_ids.empty());
  promise.set_value(get_stories_object(-1, transform(story_ids, [owner_dialog_id](StoryId story_id) {
    return StoryFullId(owner_dialog_id, story_id);
  })));
}

void StoryManager::open_story(DialogId owner_dialog_id, StoryId story_id, Promise<Unit> &&promise) {
  if (!td_->messages_manager_->have_dialog_info_force(owner_dialog_id)) {
    return promise.set_error(Status::Error(400, "Story sender not found"));
  }
  if (!td_->messages_manager_->have_input_peer(owner_dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the story sender"));
  }
  if (!story_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Invalid story identifier specified"));
  }

  StoryFullId story_full_id{owner_dialog_id, story_id};
  const Story *story = get_story(story_full_id);
  if (story == nullptr) {
    return promise.set_value(Unit());
  }

  for (auto file_id : get_story_file_ids(story)) {
    td_->file_manager_->check_local_location_async(file_id, true);
  }

  promise.set_value(Unit());
}

bool StoryManager::have_story(StoryFullId story_full_id) const {
  return get_story(story_full_id) != nullptr;
}

bool StoryManager::have_story_force(StoryFullId story_full_id) const {
  // TODO try load story
  return have_story(story_full_id);
}

bool StoryManager::is_inaccessible_story(StoryFullId story_full_id) const {
  return inaccessible_story_full_ids_.count(story_full_id) > 0;
}

int32 StoryManager::get_story_duration(StoryFullId story_full_id) const {
  auto story = get_story(story_full_id);
  if (story == nullptr) {
    return -1;
  }
  auto *content = story->content_.get();
  auto it = being_edited_stories_.find(story_full_id);
  if (it != being_edited_stories_.end()) {
    if (it->second->content_ != nullptr) {
      content = it->second->content_.get();
    }
  }
  return get_story_content_duration(td_, content);
}

void StoryManager::register_story(StoryFullId story_full_id, FullMessageId full_message_id, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  LOG(INFO) << "Register " << story_full_id << " from " << full_message_id << " from " << source;
  story_messages_[story_full_id].insert(full_message_id);
}

void StoryManager::unregister_story(StoryFullId story_full_id, FullMessageId full_message_id, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  LOG(INFO) << "Unregister " << story_full_id << " from " << full_message_id << " from " << source;
  auto &message_ids = story_messages_[story_full_id];
  auto is_deleted = message_ids.erase(full_message_id) > 0;
  LOG_CHECK(is_deleted) << source << ' ' << story_full_id << ' ' << full_message_id;
  if (message_ids.empty()) {
    story_messages_.erase(story_full_id);
  }
}

td_api::object_ptr<td_api::story> StoryManager::get_story_object(StoryFullId story_full_id) const {
  return get_story_object(story_full_id, get_story(story_full_id));
}

td_api::object_ptr<td_api::story> StoryManager::get_story_object(StoryFullId story_full_id, const Story *story) const {
  if (story == nullptr) {
    return nullptr;
  }
  story->is_update_sent_ = true;
  auto dialog_id = story_full_id.get_dialog_id();
  bool is_owned = is_story_owned(dialog_id);
  if (!is_owned && !story->is_pinned_ && G()->unix_time() >= story->expire_date_) {
    return nullptr;
  }

  td_api::object_ptr<td_api::userPrivacySettingRules> privacy_rules;
  if (is_owned) {
    privacy_rules = story->privacy_rules_.get_user_privacy_setting_rules_object(td_);
  }

  auto *content = story->content_.get();
  auto *caption = &story->caption_;
  if (is_owned && story_full_id.get_story_id().is_server()) {
    auto it = being_edited_stories_.find(story_full_id);
    if (it != being_edited_stories_.end()) {
      if (it->second->content_ != nullptr) {
        content = it->second->content_.get();
      }
      if (it->second->edit_caption_) {
        caption = &it->second->caption_;
      }
    }
  }

  CHECK(dialog_id.get_type() == DialogType::User);
  return td_api::make_object<td_api::story>(
      story_full_id.get_story_id().get(),
      td_->contacts_manager_->get_user_id_object(dialog_id.get_user_id(), "get_story_object"), story->date_,
      story->is_pinned_, story->interaction_info_.get_story_interaction_info_object(td_), std::move(privacy_rules),
      story->is_public_, story->is_for_close_friends_, get_story_content_object(td_, content),
      get_formatted_text_object(story->caption_, true, get_story_content_duration(td_, content)));
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
  if (story == nullptr || story->content_ == nullptr) {
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
                                   telegram_api::object_ptr<telegram_api::StoryItem> &&story_item_ptr) {
  if (!owner_dialog_id.is_valid()) {
    LOG(ERROR) << "Receive a story in " << owner_dialog_id;
    return {};
  }
  CHECK(story_item_ptr != nullptr);
  switch (story_item_ptr->get_id()) {
    case telegram_api::storyItemDeleted::ID: {
      auto story_item = telegram_api::move_object_as<telegram_api::storyItemDeleted>(story_item_ptr);
      StoryId story_id(story_item->id_);
      on_delete_story(owner_dialog_id, story_id);
      return story_id;
    }
    case telegram_api::storyItemSkipped::ID:
      LOG(ERROR) << "Receive storyItemSkipped";
      return {};
    case telegram_api::storyItem::ID: {
      return on_get_story(owner_dialog_id, telegram_api::move_object_as<telegram_api::storyItem>(story_item_ptr));
    }
    default:
      UNREACHABLE();
  }
}

StoryId StoryManager::on_get_story(DialogId owner_dialog_id,
                                   telegram_api::object_ptr<telegram_api::storyItem> &&story_item) {
  CHECK(story_item != nullptr);
  StoryId story_id(story_item->id_);
  if (!story_id.is_server()) {
    LOG(ERROR) << "Receive " << to_string(story_item);
    return StoryId();
  }
  if (deleted_story_full_ids_.count({owner_dialog_id, story_id}) > 0) {
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
    is_changed = true;
    story_item->min_ = false;

    inaccessible_story_full_ids_.erase(story_full_id);
    send_closure_later(G()->messages_manager(),
                       &MessagesManager::update_story_max_reply_media_timestamp_in_replied_messages, story_full_id);
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

  const BeingEditedStory *edited_story = nullptr;
  auto it = being_edited_stories_.find(story_full_id);
  if (it != being_edited_stories_.end()) {
    edited_story = it->second.get();
  }

  auto content_type = content->get_type();
  auto old_file_ids = get_story_file_ids(story);
  if (edited_story != nullptr && edited_story->content_ != nullptr) {
    story->content_ = std::move(content);
    need_save_to_database = true;
  } else if (story->content_ == nullptr || story->content_->get_type() != content_type) {
    story->content_ = std::move(content);
    is_changed = true;
  } else {
    merge_story_contents(td_, story->content_.get(), content.get(), owner_dialog_id, need_save_to_database, is_changed);
    story->content_ = std::move(content);
  }

  if (is_changed || need_save_to_database) {
    change_story_files(story_full_id, story, old_file_ids);
  }

  if (story->is_pinned_ != story_item->pinned_ || story->is_public_ != story_item->public_ ||
      story->is_for_close_friends_ != story_item->close_friends_ || story->date_ != story_item->date_ ||
      story->expire_date_ != story_item->expire_date_) {
    story->is_pinned_ = story_item->pinned_;
    story->is_public_ = story_item->public_;
    story->is_for_close_friends_ = story_item->close_friends_;
    story->date_ = story_item->date_;
    story->expire_date_ = story_item->expire_date_;
    is_changed = true;
  }
  if (!is_story_owned(owner_dialog_id)) {
    story_item->min_ = false;
  }
  if (!story_item->min_) {
    auto privacy_rules = UserPrivacySettingRules::get_user_privacy_setting_rules(td_, std::move(story_item->privacy_));
    auto interaction_info = StoryInteractionInfo(td_, std::move(story_item->views_));

    if (story->privacy_rules_ != privacy_rules || story->interaction_info_ != interaction_info) {
      story->privacy_rules_ = std::move(privacy_rules);
      story->interaction_info_ = std::move(interaction_info);
      is_changed = true;
    }
  }
  if (story->caption_ != caption) {
    story->caption_ = std::move(caption);
    if (edited_story != nullptr && edited_story->edit_caption_) {
      need_save_to_database = true;
    } else {
      is_changed = true;
    }
  }

  on_story_changed(story_full_id, story, is_changed, need_save_to_database);

  return story_id;
}

void StoryManager::on_delete_story(DialogId owner_dialog_id, StoryId story_id) {
  if (!story_id.is_server()) {
    LOG(ERROR) << "Receive deleted " << story_id << " in " << owner_dialog_id;
    return;
  }

  StoryFullId story_full_id{owner_dialog_id, story_id};
  const Story *story = get_story(story_full_id);
  if (story == nullptr) {
    return;
  }
  if (story->is_update_sent_) {
    CHECK(owner_dialog_id.get_type() == DialogType::User);
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateStoryDeleted>(
                     td_->contacts_manager_->get_user_id_object(owner_dialog_id.get_user_id(), "updateStoryDeleted"),
                     story_id.get()));
  }
  delete_story_files(story);
  stories_.erase(story_full_id);
}

void StoryManager::on_story_changed(StoryFullId story_full_id, const Story *story, bool is_changed,
                                    bool need_save_to_database) {
  if (is_changed || need_save_to_database) {
    // TODO save Story and BeingEditedStory
    // save_story(story, story_id);

    if (is_changed && story->is_update_sent_) {
      send_closure(G()->td(), &Td::send_update,
                   td_api::make_object<td_api::updateStory>(get_story_object(story_full_id, story)));
    }

    send_closure_later(G()->messages_manager(),
                       &MessagesManager::update_story_max_reply_media_timestamp_in_replied_messages, story_full_id);
    send_closure_later(G()->web_pages_manager(), &WebPagesManager::on_story_changed, story_full_id);

    if (story_messages_.count(story_full_id) != 0) {
      vector<FullMessageId> full_message_ids;
      story_messages_[story_full_id].foreach(
          [&full_message_ids](const FullMessageId &full_message_id) { full_message_ids.push_back(full_message_id); });
      CHECK(!full_message_ids.empty());
      for (const auto &full_message_id : full_message_ids) {
        td_->messages_manager_->on_external_update_message_content(full_message_id);
      }
    }
  }
}

std::pair<int32, vector<StoryId>> StoryManager::on_get_stories(
    DialogId owner_dialog_id, vector<int32> &&expected_story_ids,
    telegram_api::object_ptr<telegram_api::stories_stories> &&stories) {
  td_->contacts_manager_->on_get_users(std::move(stories->users_), "on_get_stories");
  auto story_ids = on_get_stories(owner_dialog_id, std::move(stories->stories_));
  auto total_count = stories->count_;
  if (total_count < static_cast<int32>(story_ids.size())) {
    LOG(ERROR) << "Expected at most " << total_count << " stories, but receive " << story_ids.size();
    total_count = static_cast<int32>(story_ids.size());
  }
  if (!expected_story_ids.empty()) {
    FlatHashSet<int32> all_story_ids;
    for (auto expected_story_id : expected_story_ids) {
      CHECK(expected_story_id != 0);
      all_story_ids.insert(expected_story_id);
    }
    for (auto story_id : story_ids) {
      if (all_story_ids.erase(story_id.get()) == 0) {
        LOG(ERROR) << "Receive " << story_id << " in " << owner_dialog_id << ", but didn't request it";
      }
    }
    for (auto story_id : all_story_ids) {
      StoryFullId story_full_id{owner_dialog_id, StoryId(story_id)};
      LOG(INFO) << "Mark " << story_full_id << " as inaccessible";
      inaccessible_story_full_ids_.insert(story_full_id);
      send_closure_later(G()->messages_manager(),
                         &MessagesManager::update_story_max_reply_media_timestamp_in_replied_messages, story_full_id);
    }
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
  auto story_id = story_full_id.get_story_id();
  if (!story_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid story identifier"));
  }
  auto user_id = dialog_id.get_user_id();
  td_->create_handler<GetStoriesByIDQuery>(std::move(promise))->send(user_id, {story_id.get()});
}

void StoryManager::get_story(DialogId owner_dialog_id, StoryId story_id,
                             Promise<td_api::object_ptr<td_api::story>> &&promise) {
  if (!td_->messages_manager_->have_dialog_info_force(owner_dialog_id)) {
    return promise.set_error(Status::Error(400, "Story sender not found"));
  }
  if (!td_->messages_manager_->have_input_peer(owner_dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access the story sender"));
  }
  if (!story_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid story identifier specified"));
  }
  if (owner_dialog_id.get_type() != DialogType::User) {
    return promise.set_value(nullptr);
  }

  StoryFullId story_full_id{owner_dialog_id, story_id};
  const Story *story = get_story(story_full_id);
  if (story != nullptr) {
    return promise.set_value(get_story_object(story_full_id, story));
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), story_full_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
        send_closure(actor_id, &StoryManager::do_get_story, story_full_id, std::move(result), std::move(promise));
      });
  td_->create_handler<GetStoriesByIDQuery>(std::move(query_promise))
      ->send(owner_dialog_id.get_user_id(), {story_id.get()});
}

void StoryManager::do_get_story(StoryFullId story_full_id, Result<Unit> &&result,
                                Promise<td_api::object_ptr<td_api::story>> &&promise) {
  G()->ignore_result_if_closing(result);
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }
  promise.set_value(get_story_object(story_full_id));
}

void StoryManager::send_story(td_api::object_ptr<td_api::InputStoryContent> &&input_story_content,
                              td_api::object_ptr<td_api::formattedText> &&input_caption,
                              td_api::object_ptr<td_api::userPrivacySettingRules> &&rules, int32 active_period,
                              bool is_pinned, Promise<td_api::object_ptr<td_api::story>> &&promise) {
  bool is_bot = td_->auth_manager_->is_bot();
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  TRY_RESULT_PROMISE(promise, content, get_input_story_content(td_, std::move(input_story_content), dialog_id));
  TRY_RESULT_PROMISE(promise, caption,
                     get_formatted_text(td_, DialogId(), std::move(input_caption), is_bot, true, false, false));
  TRY_RESULT_PROMISE(promise, privacy_rules,
                     UserPrivacySettingRules::get_user_privacy_setting_rules(td_, std::move(rules)));
  bool is_premium = td_->option_manager_->get_option_boolean("is_premium");
  if (active_period != 86400 &&
      !(is_premium &&
        td::contains(vector<int32>{6 * 3600, 12 * 3600, 2 * 86400, 3 * 86400, 7 * 86400}, active_period))) {
    return promise.set_error(Status::Error(400, "Invalid story active period specified"));
  }

  auto story = make_unique<Story>();
  story->date_ = G()->unix_time();
  story->expire_date_ = story->date_ + active_period;
  story->is_pinned_ = is_pinned;
  story->privacy_rules_ = std::move(privacy_rules);
  story->content_ = std::move(content);
  story->caption_ = std::move(caption);

  int64 random_id;
  do {
    random_id = Random::secure_int64();
  } while (random_id == 0);

  auto story_ptr = story.get();

  auto pending_story = td::make_unique<PendingStory>(dialog_id, StoryId(), 0 /*log_event_id*/, ++send_story_count_,
                                                     random_id, std::move(story));
  do_send_story(std::move(pending_story), {});

  promise.set_value(get_story_object({dialog_id, StoryId()}, story_ptr));
}

void StoryManager::do_send_story(unique_ptr<PendingStory> &&pending_story, vector<int> bad_parts) {
  CHECK(pending_story != nullptr);
  CHECK(pending_story->story_ != nullptr);
  auto content = pending_story->story_->content_.get();
  CHECK(content != nullptr);
  auto upload_order = pending_story->send_story_num_;

  FileId file_id = get_story_content_any_file_id(td_, content);
  CHECK(file_id.is_valid());

  LOG(INFO) << "Ask to upload file " << file_id << " with bad parts " << bad_parts;
  bool is_inserted = being_uploaded_files_.emplace(file_id, std::move(pending_story)).second;
  CHECK(is_inserted);
  // need to call resume_upload synchronously to make upload process consistent with being_uploaded_files_
  // and to send is_uploading_active == true in response
  td_->file_manager_->resume_upload(file_id, std::move(bad_parts), upload_media_callback_, 1, upload_order);
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

  auto pending_story = std::move(it->second);

  being_uploaded_files_.erase(it);

  FileView file_view = td_->file_manager_->get_file_view(file_id);
  CHECK(!file_view.is_encrypted());
  if (input_file == nullptr && file_view.has_remote_location()) {
    if (file_view.main_remote_location().is_web()) {
      LOG(ERROR) << "Can't use web photo as story";
      return;
    }
    if (pending_story->was_reuploaded_) {
      LOG(ERROR) << "Failed to reupload story";
      return;
    }
    pending_story->was_reuploaded_ = true;

    // delete file reference and forcely reupload the file
    td_->file_manager_->delete_file_reference(file_id, file_view.main_remote_location().get_file_reference());
    do_send_story(std::move(pending_story), {-1});
    return;
  }
  CHECK(input_file != nullptr);

  bool is_edit = pending_story->story_id_.is_server();
  if (is_edit) {
    do_edit_story(file_id, std::move(pending_story), std::move(input_file));
  } else {
    td_->create_handler<SendStoryQuery>()->send(file_id, std::move(pending_story), std::move(input_file));
  }
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

  being_uploaded_files_.erase(it);

  bool is_edit = pending_story->story_id_.is_server();
  if (is_edit) {
    on_story_edited(file_id, std::move(pending_story), std::move(status));
  } else {
    if (pending_story->log_event_id_ != 0) {
      binlog_erase(G()->td_db()->get_binlog(), pending_story->log_event_id_);
    }
  }
}

void StoryManager::on_send_story_file_part_missing(unique_ptr<PendingStory> &&pending_story, int bad_part) {
  do_send_story(std::move(pending_story), {bad_part});
}

void StoryManager::edit_story(StoryId story_id, td_api::object_ptr<td_api::InputStoryContent> &&input_story_content,
                              td_api::object_ptr<td_api::formattedText> &&input_caption, Promise<Unit> &&promise) {
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  StoryFullId story_full_id{dialog_id, story_id};
  const Story *story = get_story(story_full_id);
  if (story == nullptr) {
    return promise.set_error(Status::Error(400, "Story not found"));
  }
  if (!story_id.is_server()) {
    return promise.set_error(Status::Error(400, "Story can't be edited"));
  }

  bool is_bot = td_->auth_manager_->is_bot();
  unique_ptr<StoryContent> content;
  bool is_caption_edited = input_caption != nullptr;
  FormattedText caption;
  if (input_story_content != nullptr) {
    TRY_RESULT_PROMISE_ASSIGN(promise, content,
                              get_input_story_content(td_, std::move(input_story_content), dialog_id));
  }
  if (is_caption_edited) {
    TRY_RESULT_PROMISE_ASSIGN(
        promise, caption, get_formatted_text(td_, DialogId(), std::move(input_caption), is_bot, true, false, false));
    auto *current_caption = &story->caption_;
    auto it = being_edited_stories_.find(story_full_id);
    if (it != being_edited_stories_.end() && it->second->edit_caption_) {
      current_caption = &it->second->caption_;
    }
    if (*current_caption == caption) {
      is_caption_edited = false;
    }
  }
  if (content == nullptr && !is_caption_edited) {
    return promise.set_value(Unit());
  }

  auto &edited_story = being_edited_stories_[story_full_id];
  if (edited_story == nullptr) {
    edited_story = make_unique<BeingEditedStory>();
  }
  if (content != nullptr) {
    edited_story->content_ = std::move(content);
    story->edit_generation_++;
  }
  if (is_caption_edited) {
    edited_story->caption_ = std::move(caption);
    edited_story->edit_caption_ = true;
    story->edit_generation_++;
  }
  edited_story->promises_.push_back(std::move(promise));

  auto new_story = make_unique<Story>();
  new_story->content_ = dup_story_content(td_, edited_story->content_.get());

  auto pending_story = td::make_unique<PendingStory>(dialog_id, story_id, 0 /*log_event_id*/,
                                                     std::numeric_limits<uint32>::max() - (++send_story_count_),
                                                     story->edit_generation_, std::move(new_story));

  on_story_changed(story_full_id, story, true, true);

  if (edited_story->content_ == nullptr) {
    return do_edit_story(FileId(), std::move(pending_story), nullptr);
  }

  do_send_story(std::move(pending_story), {});
}

void StoryManager::do_edit_story(FileId file_id, unique_ptr<PendingStory> &&pending_story,
                                 telegram_api::object_ptr<telegram_api::InputFile> input_file) {
  StoryFullId story_full_id{pending_story->dialog_id_, pending_story->story_id_};
  const Story *story = get_story(story_full_id);
  auto it = being_edited_stories_.find(story_full_id);
  if (story == nullptr || story->edit_generation_ != pending_story->random_id_ || it == being_edited_stories_.end()) {
    LOG(INFO) << "Skip outdated edit of " << story_full_id;
    if (file_id.is_valid()) {
      td_->file_manager_->cancel_upload(file_id);
    }
    return;
  }
  td_->create_handler<EditStoryQuery>()->send(file_id, std::move(pending_story), std::move(input_file),
                                              it->second.get());
}

void StoryManager::on_story_edited(FileId file_id, unique_ptr<PendingStory> pending_story, Result<Unit> result) {
  G()->ignore_result_if_closing(result);

  if (file_id.is_valid()) {
    td_->file_manager_->delete_partial_remote_location(file_id);
  }

  StoryFullId story_full_id{pending_story->dialog_id_, pending_story->story_id_};
  const Story *story = get_story(story_full_id);
  auto it = being_edited_stories_.find(story_full_id);
  if (story == nullptr || story->edit_generation_ != pending_story->random_id_ || it == being_edited_stories_.end()) {
    LOG(INFO) << "Ignore outdated edit of " << story_full_id;
    return;
  }
  if (pending_story->log_event_id_ != 0) {
    binlog_erase(G()->td_db()->get_binlog(), pending_story->log_event_id_);
  }
  auto promises = std::move(it->second->promises_);
  bool is_changed =
      it->second->content_ != nullptr || (it->second->edit_caption_ && it->second->caption_ != story->caption_);
  being_edited_stories_.erase(it);

  on_story_changed(story_full_id, story, is_changed, true);

  if (result.is_ok()) {
    set_promises(promises);
  } else {
    fail_promises(promises, result.move_as_error());
  }
}

void StoryManager::set_story_privacy_rules(StoryId story_id,
                                           td_api::object_ptr<td_api::userPrivacySettingRules> &&rules,
                                           Promise<Unit> &&promise) {
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  const Story *story = get_story({dialog_id, story_id});
  if (story == nullptr) {
    return promise.set_error(Status::Error(400, "Story not found"));
  }
  TRY_RESULT_PROMISE(promise, privacy_rules,
                     UserPrivacySettingRules::get_user_privacy_setting_rules(td_, std::move(rules)));
  td_->create_handler<EditStoryPrivacyQuery>(std::move(promise))->send(story_id, std::move(privacy_rules));
}

void StoryManager::toggle_story_is_pinned(StoryId story_id, bool is_pinned, Promise<Unit> &&promise) {
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  const Story *story = get_story({dialog_id, story_id});
  if (story == nullptr) {
    return promise.set_error(Status::Error(400, "Story not found"));
  }
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), story_id, is_pinned, promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          return promise.set_error(result.move_as_error());
        }
        send_closure(actor_id, &StoryManager::on_toggle_story_is_pinned, story_id, is_pinned, std::move(promise));
      });
  td_->create_handler<ToggleStoryPinnedQuery>(std::move(query_promise))->send(story_id, is_pinned);
}

void StoryManager::on_toggle_story_is_pinned(StoryId story_id, bool is_pinned, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  Story *story = get_story_editable({dialog_id, story_id});
  if (story != nullptr) {
    story->is_pinned_ = is_pinned;
    on_story_changed({dialog_id, story_id}, story, true, true);
  }
  promise.set_value(Unit());
}

void StoryManager::delete_story(StoryId story_id, Promise<Unit> &&promise) {
  DialogId dialog_id(td_->contacts_manager_->get_my_id());
  const Story *story = get_story({dialog_id, story_id});
  if (story == nullptr) {
    return promise.set_error(Status::Error(400, "Story not found"));
  }
  if (!story_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid story identifier"));
  }

  delete_story_on_server(dialog_id, story_id, 0, std::move(promise));

  on_delete_story(dialog_id, story_id);
}

class StoryManager::DeleteStoryOnServerLogEvent {
 public:
  DialogId dialog_id_;
  StoryId story_id_;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(dialog_id_, storer);
    td::store(story_id_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(dialog_id_, parser);
    td::parse(story_id_, parser);
  }
};

uint64 StoryManager::save_delete_story_on_server_log_event(DialogId dialog_id, StoryId story_id) {
  DeleteStoryOnServerLogEvent log_event{dialog_id, story_id};
  return binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::DeleteStoryOnServer,
                    get_log_event_storer(log_event));
}

void StoryManager::delete_story_on_server(DialogId dialog_id, StoryId story_id, uint64 log_event_id,
                                          Promise<Unit> &&promise) {
  LOG(INFO) << "Delete " << story_id << " in " << dialog_id << " from server";

  if (log_event_id == 0) {
    log_event_id = save_delete_story_on_server_log_event(dialog_id, story_id);
  }

  auto new_promise = get_erase_log_event_promise(log_event_id, std::move(promise));
  promise = std::move(new_promise);  // to prevent self-move

  deleted_story_full_ids_.insert({dialog_id, story_id});

  td_->create_handler<DeleteStoriesQuery>(std::move(promise))->send({story_id.get()});
}

telegram_api::object_ptr<telegram_api::InputMedia> StoryManager::get_input_media(StoryFullId story_full_id) const {
  auto dialog_id = story_full_id.get_dialog_id();
  CHECK(dialog_id.get_type() == DialogType::User);
  auto r_input_user = td_->contacts_manager_->get_input_user(dialog_id.get_user_id());
  if (r_input_user.is_error()) {
    return nullptr;
  }
  return telegram_api::make_object<telegram_api::inputMediaStory>(r_input_user.move_as_ok(),
                                                                  story_full_id.get_story_id().get());
}

void StoryManager::on_binlog_events(vector<BinlogEvent> &&events) {
  if (G()->close_flag()) {
    return;
  }
  for (auto &event : events) {
    CHECK(event.id_ != 0);
    switch (event.type_) {
      case LogEvent::HandlerType::DeleteStoryOnServer: {
        DeleteStoryOnServerLogEvent log_event;
        log_event_parse(log_event, event.get_data()).ensure();

        auto dialog_id = log_event.dialog_id_;
        if (dialog_id != DialogId(td_->contacts_manager_->get_my_id())) {
          binlog_erase(G()->td_db()->get_binlog(), event.id_);
          break;
        }

        delete_story_on_server(dialog_id, log_event.story_id_, event.id_, Auto());
        break;
      }
      default:
        LOG(FATAL) << "Unsupported log event type " << event.type_;
    }
  }
}

}  // namespace td
