//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DraftMessage.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
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
    ServerMessageId reply_to_message_id;
    vector<telegram_api::object_ptr<telegram_api::MessageEntity>> input_message_entities;
    if (draft_message != nullptr) {
      if (draft_message->reply_to_message_id_.is_valid() && draft_message->reply_to_message_id_.is_server()) {
        reply_to_message_id = draft_message->reply_to_message_id_.get_server_message_id();
        flags |= telegram_api::messages_saveDraft::REPLY_TO_MSG_ID_MASK;
      }
      if (draft_message->input_message_text_.disable_web_page_preview) {
        flags |= telegram_api::messages_saveDraft::NO_WEBPAGE_MASK;
      }
      input_message_entities = get_input_message_entities(
          td_->contacts_manager_.get(), draft_message->input_message_text_.text.entities, "SaveDraftMessageQuery");
      if (!input_message_entities.empty()) {
        flags |= telegram_api::messages_saveDraft::ENTITIES_MASK;
      }
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_saveDraft(
            flags, false /*ignored*/, reply_to_message_id.get(), 0, std::move(input_peer),
            draft_message == nullptr ? string() : draft_message->input_message_text_.text.text,
            std::move(input_message_entities)),
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
  if (reply_to_message_id_ == other.reply_to_message_id_ && input_message_text_ == other.input_message_text_) {
    return date_ < other.date_;
  } else {
    return !from_update || date_ <= other.date_;
  }
}

void DraftMessage::add_dependencies(Dependencies &dependencies) const {
  add_formatted_text_dependencies(dependencies, &input_message_text_.text);
}

td_api::object_ptr<td_api::draftMessage> DraftMessage::get_draft_message_object() const {
  return td_api::make_object<td_api::draftMessage>(reply_to_message_id_.get(), date_,
                                                   get_input_message_text_object(input_message_text_));
}

DraftMessage::DraftMessage(ContactsManager *contacts_manager,
                           telegram_api::object_ptr<telegram_api::draftMessage> &&draft_message) {
  CHECK(draft_message != nullptr);
  auto flags = draft_message->flags_;
  date_ = draft_message->date_;
  if ((flags & telegram_api::draftMessage::REPLY_TO_MSG_ID_MASK) != 0) {
    reply_to_message_id_ = MessageId(ServerMessageId(draft_message->reply_to_msg_id_));
    if (!reply_to_message_id_.is_valid()) {
      LOG(ERROR) << "Receive " << reply_to_message_id_ << " as reply_to_message_id in the draft message";
      reply_to_message_id_ = MessageId();
    }
  }

  auto entities = get_message_entities(contacts_manager, std::move(draft_message->entities_), "draftMessage");
  auto status = fix_formatted_text(draft_message->message_, entities, true, true, true, true, true);
  if (status.is_error()) {
    LOG(ERROR) << "Receive error " << status << " while parsing draft " << draft_message->message_;
    if (!clean_input_string(draft_message->message_)) {
      draft_message->message_.clear();
    }
    entities = find_entities(draft_message->message_, false, true);
  }
  input_message_text_.text = FormattedText{std::move(draft_message->message_), std::move(entities)};
  input_message_text_.disable_web_page_preview = draft_message->no_webpage_;
  input_message_text_.clear_draft = false;
}

Result<unique_ptr<DraftMessage>> DraftMessage::get_draft_message(
    Td *td, DialogId dialog_id, MessageId top_thread_message_id,
    td_api::object_ptr<td_api::draftMessage> &&draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }

  auto result = make_unique<DraftMessage>();
  result->reply_to_message_id_ = MessageId(draft_message->reply_to_message_id_);
  if (result->reply_to_message_id_ != MessageId() && !result->reply_to_message_id_.is_valid()) {
    return Status::Error(400, "Invalid reply_to_message_id specified");
  }
  result->reply_to_message_id_ = td->messages_manager_->get_reply_to_message_id(dialog_id, top_thread_message_id,
                                                                                result->reply_to_message_id_, true);

  auto input_message_content = std::move(draft_message->input_message_text_);
  if (input_message_content != nullptr) {
    if (input_message_content->get_id() != td_api::inputMessageText::ID) {
      return Status::Error(400, "Input message content type must be InputMessageText");
    }
    TRY_RESULT(message_content,
               process_input_message_text(td, dialog_id, std::move(input_message_content), false, true));
    result->input_message_text_ = std::move(message_content);
  }

  if (!result->reply_to_message_id_.is_valid() && result->input_message_text_.text.text.empty()) {
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

td_api::object_ptr<td_api::draftMessage> get_draft_message_object(const unique_ptr<DraftMessage> &draft_message) {
  if (draft_message == nullptr) {
    return nullptr;
  }
  return draft_message->get_draft_message_object();
}

unique_ptr<DraftMessage> get_draft_message(ContactsManager *contacts_manager,
                                           telegram_api::object_ptr<telegram_api::DraftMessage> &&draft_message_ptr) {
  if (draft_message_ptr == nullptr) {
    return nullptr;
  }
  auto constructor_id = draft_message_ptr->get_id();
  switch (constructor_id) {
    case telegram_api::draftMessageEmpty::ID:
      return nullptr;
    case telegram_api::draftMessage::ID:
      return td::make_unique<DraftMessage>(contacts_manager,
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
