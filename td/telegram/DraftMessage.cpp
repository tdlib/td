//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DraftMessage.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"

namespace td {

class SaveDraftMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit SaveDraftMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const unique_ptr<DraftMessage> &draft_message) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->messages_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      LOG(INFO) << "Can't update draft message because have no write access to " << dialog_id;
      return on_error(Status::Error(400, "Can't save draft message"));
    }

    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputReplyTo> input_reply_to;
    vector<telegram_api::object_ptr<telegram_api::MessageEntity>> input_message_entities;
    telegram_api::object_ptr<telegram_api::InputMedia> media;
    if (draft_message != nullptr) {
      input_reply_to = draft_message->message_input_reply_to_.get_input_reply_to(td_, MessageId() /*TODO*/);
      if (input_reply_to != nullptr) {
        flags |= telegram_api::messages_saveDraft::REPLY_TO_MASK;
      }
      if (draft_message->input_message_text_.disable_web_page_preview) {
        flags |= telegram_api::messages_saveDraft::NO_WEBPAGE_MASK;
      } else if (draft_message->input_message_text_.show_above_text) {
        flags |= telegram_api::messages_saveDraft::INVERT_MEDIA_MASK;
      }
      input_message_entities = get_input_message_entities(
          td_->contacts_manager_.get(), draft_message->input_message_text_.text.entities, "SaveDraftMessageQuery");
      if (!input_message_entities.empty()) {
        flags |= telegram_api::messages_saveDraft::ENTITIES_MASK;
      }
      media = draft_message->input_message_text_.get_input_media_web_page();
      if (media != nullptr) {
        flags |= telegram_api::messages_saveDraft::MEDIA_MASK;
      }
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_saveDraft(
            flags, false /*ignored*/, false /*ignored*/, std::move(input_reply_to), std::move(input_peer),
            draft_message == nullptr ? string() : draft_message->input_message_text_.text.text,
            std::move(input_message_entities), std::move(media)),
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

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    if (status.message() == "TOPIC_CLOSED") {
      // when the draft is a reply to a message in a closed topic, server will not allow to save it
      // with the error "TOPIC_CLOSED", but the draft will be kept locally
      return promise_.set_value(Unit());
    }
    if (!td_->messages_manager_->on_get_dialog_error(dialog_id_, status, "SaveDraftMessageQuery")) {
      LOG(ERROR) << "Receive error for SaveDraftMessageQuery: " << status;
    }
    promise_.set_error(std::move(status));
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

bool DraftMessage::need_update_to(const DraftMessage &other, bool from_update) const {
  if (message_input_reply_to_ == other.message_input_reply_to_ && input_message_text_ == other.input_message_text_) {
    return date_ < other.date_;
  } else {
    return !from_update || date_ <= other.date_;
  }
}

void DraftMessage::add_dependencies(Dependencies &dependencies) const {
  message_input_reply_to_.add_dependencies(dependencies);
  input_message_text_.add_dependencies(dependencies);
}

td_api::object_ptr<td_api::draftMessage> DraftMessage::get_draft_message_object(Td *td) const {
  return td_api::make_object<td_api::draftMessage>(message_input_reply_to_.get_input_message_reply_to_object(td), date_,
                                                   input_message_text_.get_input_message_text_object());
}

DraftMessage::DraftMessage(Td *td, telegram_api::object_ptr<telegram_api::draftMessage> &&draft_message) {
  CHECK(draft_message != nullptr);
  date_ = draft_message->date_;
  message_input_reply_to_ = MessageInputReplyTo(td, std::move(draft_message->reply_to_));
  auto entities =
      get_message_entities(td->contacts_manager_.get(), std::move(draft_message->entities_), "draftMessage");
  auto status = fix_formatted_text(draft_message->message_, entities, true, true, true, true, true);
  if (status.is_error()) {
    LOG(ERROR) << "Receive error " << status << " while parsing draft " << draft_message->message_;
    if (!clean_input_string(draft_message->message_)) {
      draft_message->message_.clear();
    }
    entities = find_entities(draft_message->message_, false, true);
  }
  string web_page_url;
  bool force_small_media = false;
  bool force_large_media = false;
  if (draft_message->media_ != nullptr) {
    if (draft_message->media_->get_id() != telegram_api::inputMediaWebPage::ID) {
      LOG(ERROR) << "Receive draft message with " << to_string(draft_message->media_);
    } else {
      auto media = telegram_api::move_object_as<telegram_api::inputMediaWebPage>(draft_message->media_);
      web_page_url = std::move(media->url_);
      if (web_page_url.empty()) {
        LOG(ERROR) << "Have no URL in a draft with manual link preview";
      }
      force_small_media = media->force_small_media_;
      force_large_media = media->force_large_media_;
    }
  }
  input_message_text_ = InputMessageText(FormattedText{std::move(draft_message->message_), std::move(entities)},
                                         std::move(web_page_url), draft_message->no_webpage_, force_small_media,
                                         force_large_media, draft_message->invert_media_, false);
}

Result<unique_ptr<DraftMessage>> DraftMessage::get_draft_message(
    Td *td, DialogId dialog_id, MessageId top_thread_message_id,
    td_api::object_ptr<td_api::draftMessage> &&draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }

  auto result = make_unique<DraftMessage>();
  result->message_input_reply_to_ = td->messages_manager_->get_message_input_reply_to(
      dialog_id, top_thread_message_id, std::move(draft_message->reply_to_), true);

  auto input_message_content = std::move(draft_message->input_message_text_);
  if (input_message_content != nullptr) {
    if (input_message_content->get_id() != td_api::inputMessageText::ID) {
      return Status::Error(400, "Input message content type must be InputMessageText");
    }
    TRY_RESULT(input_message_text,
               process_input_message_text(td, dialog_id, std::move(input_message_content), false, true));
    result->input_message_text_ = std::move(input_message_text);
  }

  if (!result->message_input_reply_to_.is_valid() && result->input_message_text_.is_empty()) {
    return nullptr;
  }

  result->date_ = G()->unix_time();
  return std::move(result);
}

bool need_update_draft_message(const unique_ptr<DraftMessage> &old_draft_message,
                               const unique_ptr<DraftMessage> &new_draft_message, bool from_update) {
  if (new_draft_message == nullptr) {
    return old_draft_message != nullptr;
  }
  if (old_draft_message == nullptr) {
    return true;
  }
  return old_draft_message->need_update_to(*new_draft_message, from_update);
}

void add_draft_message_dependencies(Dependencies &dependencies, const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return;
  }
  draft_message->add_dependencies(dependencies);
}

td_api::object_ptr<td_api::draftMessage> get_draft_message_object(Td *td,
                                                                  const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }
  return draft_message->get_draft_message_object(td);
}

unique_ptr<DraftMessage> get_draft_message(Td *td,
                                           telegram_api::object_ptr<telegram_api::DraftMessage> &&draft_message_ptr) {
  if (draft_message_ptr == nullptr) {
    return nullptr;
  }
  auto constructor_id = draft_message_ptr->get_id();
  switch (constructor_id) {
    case telegram_api::draftMessageEmpty::ID:
      return nullptr;
    case telegram_api::draftMessage::ID:
      return td::make_unique<DraftMessage>(td,
                                           telegram_api::move_object_as<telegram_api::draftMessage>(draft_message_ptr));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

void save_draft_message(Td *td, DialogId dialog_id, const unique_ptr<DraftMessage> &draft_message,
                        Promise<Unit> &&promise) {
  td->create_handler<SaveDraftMessageQuery>(std::move(promise))->send(dialog_id, draft_message);
}

void load_all_draft_messages(Td *td) {
  td->create_handler<GetAllDraftsQuery>()->send();
}

void clear_all_draft_messages(Td *td, Promise<Unit> &&promise) {
  td->create_handler<ClearAllDraftsQuery>(std::move(promise))->send();
}

}  // namespace td
