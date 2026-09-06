//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DraftMessageManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DraftMessage.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileUploadId.h"
#include "td/telegram/ForumTopicManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/MessageQueryManager.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/SavedMessagesManager.h"
#include "td/telegram/SuggestedPost.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class SaveDraftMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;
  MessageContentUploadId upload_id_;

 public:
  explicit SaveDraftMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const MessageTopic &message_topic, const unique_ptr<DraftMessage> &draft_message,
            MessageContentUploadId upload_id, InputMedia &&input_media) {
    CHECK(input_media.media_ == nullptr);
    dialog_id_ = dialog_id;
    upload_id_ = upload_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      LOG(INFO) << "Can't update draft message because have no write access to " << dialog_id;
      return on_error(Status::Error(400, "PEER_ID_INVALID"));
    }

    td_->message_query_manager_->on_start_sending_message_content(upload_id_, input_media);
    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputReplyTo> input_reply_to;
    telegram_api::object_ptr<telegram_api::InputRichMessage> input_rich_message;
    vector<telegram_api::object_ptr<telegram_api::MessageEntity>> input_message_entities;
    telegram_api::object_ptr<telegram_api::InputMedia> media;
    int64 message_effect_id = 0;
    telegram_api::object_ptr<telegram_api::suggestedPost> suggested_post;
    bool disable_web_page_preview = false;
    bool invert_media = false;
    if (draft_message != nullptr) {
      CHECK(!draft_message->is_local());
      input_reply_to = draft_message->message_input_reply_to_.get_input_reply_to(td_, message_topic, true);
      if (draft_message->rich_message_content_ != nullptr) {
        CHECK(input_media.rich_message_ != nullptr);
        flags |= telegram_api::messages_saveDraft::RICH_MESSAGE_MASK;
      } else {
        CHECK(upload_id == MessageContentUploadId());
        CHECK(input_media.rich_message_ == nullptr);
        if (draft_message->input_message_text_.disable_web_page_preview) {
          disable_web_page_preview = true;
        } else if (draft_message->input_message_text_.show_above_text) {
          invert_media = true;
        }
        input_message_entities = get_input_message_entities(
            td_->user_manager_.get(), draft_message->input_message_text_.text.entities, "SaveDraftMessageQuery");
        if (!input_message_entities.empty()) {
          flags |= telegram_api::messages_saveDraft::ENTITIES_MASK;
        }
        media = draft_message->input_message_text_.get_input_media_web_page();
        if (media != nullptr) {
          flags |= telegram_api::messages_saveDraft::MEDIA_MASK;
        }
      }
      if (draft_message->message_effect_id_.is_valid()) {
        flags |= telegram_api::messages_saveDraft::EFFECT_MASK;
        message_effect_id = draft_message->message_effect_id_.get();
      }
      if (draft_message->suggested_post_ != nullptr) {
        flags |= telegram_api::messages_saveDraft::SUGGESTED_POST_MASK;
        suggested_post = draft_message->suggested_post_->get_input_suggested_post();
      }
    } else {
      CHECK(upload_id == MessageContentUploadId());
      input_reply_to = MessageInputReplyTo().get_input_reply_to(td_, message_topic, true);
    }
    if (input_reply_to != nullptr) {
      flags |= telegram_api::messages_saveDraft::REPLY_TO_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_saveDraft(
            flags, disable_web_page_preview, invert_media, std::move(input_reply_to), std::move(input_peer),
            draft_message == nullptr ? string() : draft_message->input_message_text_.text.text,
            std::move(input_message_entities), std::move(media), message_effect_id, std::move(suggested_post),
            std::move(input_media.rich_message_)),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_saveDraft>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.ok();
    if (!result) {
      return on_error(Status::Error(400, "Save draft failed"));
    }

    if (upload_id_ != MessageContentUploadId()) {
      td_->draft_message_manager_->cancel_save_draft_message(upload_id_, Status::OK());
    } else {
      promise_.set_value(Unit());
    }
  }

  void on_error(Status status) final {
    if (status.message() == "TOPIC_CLOSED") {
      // when the draft is a reply to a message in a closed topic, server will not allow to save it
      // with the error "TOPIC_CLOSED", but the draft will be kept locally
      if (upload_id_ != MessageContentUploadId()) {
        td_->draft_message_manager_->cancel_save_draft_message(upload_id_, Status::OK());
      } else {
        promise_.set_error(std::move(status));
      }
      return promise_.set_value(Unit());
    }
    if (!td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "SaveDraftMessageQuery") &&
        status.message() != "PEER_ID_INVALID") {
      LOG(ERROR) << "Receive error for SaveDraftMessageQuery: " << status;
    }
    if (upload_id_ != MessageContentUploadId()) {
      td_->message_query_manager_->process_send_message_content_error(upload_id_, std::move(status));
    } else {
      promise_.set_error(std::move(status));
    }
  }
};

class GetAllDraftsQuery final : public Td::ResultHandler {
 public:
  void send() {
    send_query(G()->net_query_creator().create(telegram_api::messages_getAllDrafts()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getAllDrafts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetAllDraftsQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), Promise<Unit>());
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for GetAllDraftsQuery: " << status;
    }
    status.ignore();
  }
};

class ClearAllDraftsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ClearAllDraftsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::messages_clearAllDrafts()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_clearAllDrafts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    LOG(INFO) << "Receive result for ClearAllDraftsQuery: " << result_ptr.ok();
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (!G()->is_expected_error(status)) {
      LOG(ERROR) << "Receive error for ClearAllDraftsQuery: " << status;
    }
    promise_.set_error(std::move(status));
  }
};

class DraftMessageManager::UploadDraftMessageCallback final : public MessageQueryManager::UploadMessageContentCallback {
  DraftMessageManager *manager_;

 public:
  explicit UploadDraftMessageCallback(DraftMessageManager *draft_message_manager) : manager_(draft_message_manager) {
  }

  void on_message_content_uploaded(MessageContentUploadId upload_id, InputMedia &&input_media) final {
    auto &query = manager_->save_draft_message_queries_[upload_id];
    manager_->td_->create_handler<SaveDraftMessageQuery>(Promise<Unit>())
        ->send(query.dialog_id_, query.message_topic_, query.draft_message_, upload_id, std::move(input_media));
  }

  void on_message_content_force_uploaded(MessageContentUploadId upload_id, Status status) final {
    if (status.is_error()) {
      return on_failed_to_upload_message_content(upload_id, std::move(status));
    }
    auto &query = manager_->save_draft_message_queries_[upload_id];
    auto input_media = get_message_content_input_media(query.draft_message_->rich_message_content_.get(), manager_->td_,
                                                       {}, string(), true, -1);
    CHECK(!input_media.is_empty());
    manager_->td_->create_handler<SaveDraftMessageQuery>(Promise<Unit>())
        ->send(query.dialog_id_, query.message_topic_, query.draft_message_, upload_id, std::move(input_media));
  }

  void on_uploaded_message_content_updated(MessageContentUploadId upload_id, unique_ptr<MessageContent> &&content,
                                           bool need_merge_files, bool is_content_changed, bool need_update) final {
    auto &query = manager_->save_draft_message_queries_[upload_id];
    merge_and_compare_message_contents(manager_->td_, query.draft_message_->rich_message_content_.get(), content.get(),
                                       true, query.dialog_id_, need_merge_files, vector<FileUploadId>(),
                                       MessageSelfDestructType(), 0.0, nullptr, is_content_changed, need_update);
    query.draft_message_->rich_message_content_ = std::move(content);
  }

  void on_failed_to_upload_message_content(MessageContentUploadId upload_id, Status error) final {
    manager_->cancel_save_draft_message(upload_id, std::move(error));
  }

  void on_failed_to_upload_message_content_thumbnail(MessageContentUploadId upload_id, int32 media_pos) final {
    auto &query = manager_->save_draft_message_queries_[upload_id];
    delete_message_content_thumbnail(manager_->td_, query.draft_message_->rich_message_content_.get(), media_pos);
  }
};

DraftMessageManager::DraftMessageManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_draft_message_callback_ = std::make_shared<UploadDraftMessageCallback>(this);
}

void DraftMessageManager::tear_down() {
  parent_.reset();
}

void DraftMessageManager::save_draft_message(DialogId dialog_id, const MessageTopic &message_topic,
                                             const unique_ptr<DraftMessage> &draft_message, Promise<Unit> &&promise) {
  if (dialog_id.get_type() == DialogType::SecretChat || is_local_draft_message(draft_message)) {
    return promise.set_value(Unit());
  }
  MessageContentUploadId upload_id;
  if (message_topic.is_empty()) {
    auto it = dialog_draft_message_upload_ids_.find(dialog_id);
    if (it != dialog_draft_message_upload_ids_.end()) {
      upload_id = it->second;
    }
  } else {
    auto it = topic_draft_message_upload_ids_.find(message_topic);
    if (it != topic_draft_message_upload_ids_.end()) {
      upload_id = it->second;
    }
  }
  if (upload_id != MessageContentUploadId()) {
    cancel_save_draft_message(upload_id, Status::Error(200, "Canceled by another request"));
  }
  if (draft_message == nullptr || draft_message->get_rich_message_content() == nullptr) {
    td_->create_handler<SaveDraftMessageQuery>(std::move(promise))
        ->send(dialog_id, message_topic, draft_message, MessageContentUploadId(), InputMedia());
    return;
  }

  upload_id = td_->message_query_manager_->create_upload_message_content_query(
      dialog_id, draft_message->get_rich_message_content(), MessageSelfDestructType(), string(), false, false,
      upload_draft_message_callback_);
  if (message_topic.is_empty()) {
    LOG(ERROR) << dialog_draft_message_upload_ids_.size();
    dialog_draft_message_upload_ids_[dialog_id] = upload_id;
  } else {
    LOG(ERROR) << topic_draft_message_upload_ids_.size();
    topic_draft_message_upload_ids_[message_topic] = upload_id;
  }
  auto &query = save_draft_message_queries_[upload_id];
  query.dialog_id_ = dialog_id;
  query.message_topic_ = message_topic;
  query.draft_message_ = DraftMessage::clone(td_, draft_message, dialog_id);
  query.promise_ = std::move(promise);
  td_->message_query_manager_->start_upload_message_content(upload_id);
}

void DraftMessageManager::cancel_save_draft_message(MessageContentUploadId upload_id, Status status) {
  auto it = save_draft_message_queries_.find(upload_id);
  if (it == save_draft_message_queries_.end()) {
    return;
  }
  auto promise = std::move(it->second.promise_);
  if (it->second.message_topic_.is_empty()) {
    dialog_draft_message_upload_ids_.erase(it->second.dialog_id_);
  } else {
    topic_draft_message_upload_ids_.erase(it->second.message_topic_);
  }
  save_draft_message_queries_.erase(upload_id);

  td_->message_query_manager_->cancel_upload_message_content(upload_id);
  if (status.is_error()) {
    promise.set_error(std::move(status));
  } else {
    promise.set_value(Unit());
  }
}

void DraftMessageManager::reload_draft_message(DialogId dialog_id, const MessageTopic &message_topic,
                                               Promise<Unit> &&promise) {
  if (message_topic.is_empty()) {
    return td_->messages_manager_->reload_dialog(dialog_id, std::move(promise));
  }
  if (message_topic.is_forum()) {
    return td_->forum_topic_manager_->reload_forum_topic(dialog_id, message_topic.get_forum_topic_id(),
                                                         std::move(promise));
  }
  if (message_topic.is_monoforum()) {
    auto query_promise = PromiseCreator::lambda(
        [promise = std::move(promise)](Result<td_api::object_ptr<td_api::directMessagesChatTopic>> r_topic) mutable {
          if (r_topic.is_error()) {
            promise.set_error(r_topic.move_as_error());
          } else {
            promise.set_value(Unit());
          }
        });
    return td_->saved_messages_manager_->reload_monoforum_topic(
        dialog_id, message_topic.get_monoforum_saved_messages_topic_id(), std::move(query_promise));
  }
  LOG(ERROR) << "Tried to reload draft in " << message_topic << " of " << dialog_id;
  return promise.set_error(400, "Topic has no drafts");
}

void DraftMessageManager::load_all_draft_messages() {
  td_->create_handler<GetAllDraftsQuery>()->send();
}

void DraftMessageManager::clear_all_draft_messages(Promise<Unit> &&promise) {
  td_->create_handler<ClearAllDraftsQuery>(std::move(promise))->send();
}

FileSourceId *DraftMessageManager::get_file_source_id(DialogId dialog_id, const MessageTopic &message_topic) {
  if (message_topic.is_empty()) {
    return &dialog_draft_message_file_source_ids_[dialog_id];
  }
  if (!message_topic.is_valid()) {
    return nullptr;
  }
  return &topic_draft_message_file_source_ids_[message_topic];
}

FileSourceId DraftMessageManager::get_draft_message_file_source_id(DialogId dialog_id,
                                                                   const MessageTopic &message_topic) {
  if (!dialog_id.is_valid()) {
    return FileSourceId();
  }

  FileSourceId *source_id = get_file_source_id(dialog_id, message_topic);
  if (source_id == nullptr) {
    return FileSourceId();
  }
  if (!source_id->is_valid()) {
    *source_id = td_->file_reference_manager_->create_draft_message_file_source(dialog_id, message_topic);
  }
  VLOG(file_references) << "Return " << *source_id << " for draft in " << message_topic << " of " << dialog_id;
  return *source_id;
}

void DraftMessageManager::change_draft_message_files(DialogId dialog_id, const MessageTopic &message_topic,
                                                     const vector<FileId> &old_file_ids,
                                                     const vector<FileId> &new_file_ids, bool need_delete_files) {
  if (new_file_ids == old_file_ids) {
    return;
  }

  if (need_delete_files) {
    for (auto file_id : old_file_ids) {
      if (!td::contains(new_file_ids, file_id)) {
        send_closure(G()->file_manager(), &FileManager::delete_file, file_id, Promise<Unit>(),
                     "change_draft_message_files");
      }
    }
  }

  auto file_source_id = get_draft_message_file_source_id(dialog_id, message_topic);
  if (file_source_id.is_valid()) {
    td_->file_manager_->change_files_source(file_source_id, old_file_ids, new_file_ids, "change_draft_message_files");
  }
}

}  // namespace td
