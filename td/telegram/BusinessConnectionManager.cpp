//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessConnectionManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/MessageCopyOptions.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageQuote.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

namespace td {

class GetBotBusinessConnectionQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::Updates>> promise_;

 public:
  explicit GetBotBusinessConnectionQuery(Promise<telegram_api::object_ptr<telegram_api::Updates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const BusinessConnectionId &connection_id) {
    send_query(G()->net_query_creator().create(telegram_api::account_getBotBusinessConnection(connection_id.get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getBotBusinessConnection>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetBotBusinessConnectionQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

struct BusinessConnectionManager::BusinessConnection {
  BusinessConnectionId connection_id_;
  UserId user_id_;
  DcId dc_id_;
  int32 connection_date_ = 0;
  bool can_reply_ = false;
  bool is_disabled_ = false;

  explicit BusinessConnection(const telegram_api::object_ptr<telegram_api::botBusinessConnection> &connection)
      : connection_id_(connection->connection_id_)
      , user_id_(connection->user_id_)
      , dc_id_(DcId::create(connection->dc_id_))
      , connection_date_(connection->date_)
      , can_reply_(connection->can_reply_)
      , is_disabled_(connection->disabled_) {
  }

  BusinessConnection(const BusinessConnection &) = delete;
  BusinessConnection &operator=(const BusinessConnection &) = delete;
  BusinessConnection(BusinessConnection &&) = delete;
  BusinessConnection &operator=(BusinessConnection &&) = delete;
  ~BusinessConnection() = default;

  bool is_valid() const {
    return connection_id_.is_valid() && user_id_.is_valid() && !dc_id_.is_empty() && connection_date_ > 0;
  }

  td_api::object_ptr<td_api::businessConnection> get_business_connection_object(Td *td) const {
    DialogId user_dialog_id(user_id_);
    td->dialog_manager_->force_create_dialog(user_dialog_id, "get_business_connection_object");
    return td_api::make_object<td_api::businessConnection>(
        connection_id_.get(), td->user_manager_->get_user_id_object(user_id_, "businessConnection"),
        td->dialog_manager_->get_chat_id_object(user_dialog_id, "businessConnection"), connection_date_, can_reply_,
        !is_disabled_);
  }
};

struct BusinessConnectionManager::PendingMessage {
  BusinessConnectionId business_connection_id_;
  DialogId dialog_id_;
  MessageInputReplyTo input_reply_to_;
  string send_emoji_;
  MessageSelfDestructType ttl_;
  unique_ptr<MessageContent> content_;
  unique_ptr<ReplyMarkup> reply_markup_;
  int64 random_id_ = 0;
  int64 effect_id_ = 0;
  bool noforwards_ = false;
  bool disable_notification_ = false;
  bool invert_media_ = false;
  bool disable_web_page_preview_ = false;
};

class BusinessConnectionManager::SendBusinessMessageQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::businessMessage>> promise_;
  unique_ptr<PendingMessage> message_;

 public:
  explicit SendBusinessMessageQuery(Promise<td_api::object_ptr<td_api::businessMessage>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(unique_ptr<PendingMessage> message) {
    message_ = std::move(message);

    int32 flags = 0;
    if (message_->disable_web_page_preview_) {
      flags |= telegram_api::messages_sendMessage::NO_WEBPAGE_MASK;
    }
    if (message_->disable_notification_) {
      flags |= telegram_api::messages_sendMessage::SILENT_MASK;
    }
    if (message_->noforwards_) {
      flags |= telegram_api::messages_sendMessage::NOFORWARDS_MASK;
    }
    if (message_->effect_id_) {
      flags |= telegram_api::messages_sendMessage::EFFECT_MASK;
    }
    if (message_->invert_media_) {
      flags |= telegram_api::messages_sendMessage::INVERT_MEDIA_MASK;
    }

    auto input_peer = td_->dialog_manager_->get_input_peer(message_->dialog_id_, AccessRights::Know);
    CHECK(input_peer != nullptr);

    auto reply_to = message_->input_reply_to_.get_input_reply_to(td_, MessageId());
    if (reply_to != nullptr) {
      flags |= telegram_api::messages_sendMessage::REPLY_TO_MASK;
    }

    const FormattedText *message_text = get_message_content_text(message_->content_.get());
    CHECK(message_text != nullptr);
    auto entities = get_input_message_entities(td_->user_manager_.get(), message_text, "SendBusinessMessageQuery");
    if (!entities.empty()) {
      flags |= telegram_api::messages_sendMessage::ENTITIES_MASK;
    }

    if (message_->reply_markup_ != nullptr) {
      flags |= telegram_api::messages_sendMessage::REPLY_MARKUP_MASK;
    }

    send_query(G()->net_query_creator().create_with_prefix(
        message_->business_connection_id_.get_invoke_prefix(),
        telegram_api::messages_sendMessage(
            flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/, false /*ignored*/,
            false /*ignored*/, false /*ignored*/, std::move(input_peer), std::move(reply_to), message_text->text,
            message_->random_id_, get_input_reply_markup(td_->user_manager_.get(), message_->reply_markup_),
            std::move(entities), 0, nullptr, nullptr, message_->effect_id_),
        td_->business_connection_manager_->get_business_connection_dc_id(message_->business_connection_id_),
        {{message_->dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_sendMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendBusinessMessageQuery: " << to_string(ptr);
    td_->business_connection_manager_->process_sent_business_message(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SendBusinessMessageQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class BusinessConnectionManager::SendBusinessMediaQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::businessMessage>> promise_;
  unique_ptr<PendingMessage> message_;

 public:
  explicit SendBusinessMediaQuery(Promise<td_api::object_ptr<td_api::businessMessage>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(unique_ptr<PendingMessage> message, telegram_api::object_ptr<telegram_api::InputMedia> &&input_media) {
    CHECK(input_media != nullptr);
    message_ = std::move(message);

    int32 flags = 0;
    if (message_->disable_notification_) {
      flags |= telegram_api::messages_sendMedia::SILENT_MASK;
    }
    if (message_->noforwards_) {
      flags |= telegram_api::messages_sendMedia::NOFORWARDS_MASK;
    }
    if (message_->effect_id_) {
      flags |= telegram_api::messages_sendMedia::EFFECT_MASK;
    }
    if (message_->invert_media_) {
      flags |= telegram_api::messages_sendMedia::INVERT_MEDIA_MASK;
    }

    auto input_peer = td_->dialog_manager_->get_input_peer(message_->dialog_id_, AccessRights::Know);
    CHECK(input_peer != nullptr);

    auto reply_to = message_->input_reply_to_.get_input_reply_to(td_, MessageId());
    if (reply_to != nullptr) {
      flags |= telegram_api::messages_sendMedia::REPLY_TO_MASK;
    }

    const FormattedText *message_text = get_message_content_text(message_->content_.get());
    auto entities = get_input_message_entities(td_->user_manager_.get(), message_text, "SendBusinessMediaQuery");
    if (!entities.empty()) {
      flags |= telegram_api::messages_sendMedia::ENTITIES_MASK;
    }

    if (message_->reply_markup_ != nullptr) {
      flags |= telegram_api::messages_sendMedia::REPLY_MARKUP_MASK;
    }

    send_query(G()->net_query_creator().create_with_prefix(
        message_->business_connection_id_.get_invoke_prefix(),
        telegram_api::messages_sendMedia(flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                         false /*ignored*/, false /*ignored*/, false /*ignored*/, std::move(input_peer),
                                         std::move(reply_to), std::move(input_media),
                                         message_text == nullptr ? string() : message_text->text, message_->random_id_,
                                         get_input_reply_markup(td_->user_manager_.get(), message_->reply_markup_),
                                         std::move(entities), 0, nullptr, nullptr, message_->effect_id_),
        td_->business_connection_manager_->get_business_connection_dc_id(message_->business_connection_id_),
        {{message_->dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_sendMedia>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendBusinessMediaQuery: " << to_string(ptr);
    td_->business_connection_manager_->process_sent_business_message(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SendBusinessMediaQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class BusinessConnectionManager::SendBusinessMultiMediaQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::businessMessages>> promise_;
  vector<unique_ptr<PendingMessage>> messages_;

 public:
  explicit SendBusinessMultiMediaQuery(Promise<td_api::object_ptr<td_api::businessMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(vector<unique_ptr<PendingMessage>> &&messages,
            vector<telegram_api::object_ptr<telegram_api::inputSingleMedia>> &&input_single_media) {
    CHECK(!messages.empty());
    messages_ = std::move(messages);

    int32 flags = 0;
    if (messages_[0]->disable_notification_) {
      flags |= telegram_api::messages_sendMultiMedia::SILENT_MASK;
    }
    if (messages_[0]->noforwards_) {
      flags |= telegram_api::messages_sendMultiMedia::NOFORWARDS_MASK;
    }
    if (messages_[0]->effect_id_) {
      flags |= telegram_api::messages_sendMultiMedia::EFFECT_MASK;
    }
    if (messages_[0]->invert_media_) {
      flags |= telegram_api::messages_sendMultiMedia::INVERT_MEDIA_MASK;
    }

    auto input_peer = td_->dialog_manager_->get_input_peer(messages_[0]->dialog_id_, AccessRights::Know);
    CHECK(input_peer != nullptr);

    auto reply_to = messages_[0]->input_reply_to_.get_input_reply_to(td_, MessageId());
    if (reply_to != nullptr) {
      flags |= telegram_api::messages_sendMultiMedia::REPLY_TO_MASK;
    }

    send_query(G()->net_query_creator().create_with_prefix(
        messages_[0]->business_connection_id_.get_invoke_prefix(),
        telegram_api::messages_sendMultiMedia(flags, false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                              false /*ignored*/, false /*ignored*/, false /*ignored*/,
                                              std::move(input_peer), std::move(reply_to), std::move(input_single_media),
                                              0, nullptr, nullptr, messages_[0]->effect_id_),
        td_->business_connection_manager_->get_business_connection_dc_id(messages_[0]->business_connection_id_),
        {{messages_[0]->dialog_id_}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_sendMultiMedia>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendBusinessMultiMediaQuery: " << to_string(ptr);
    td_->business_connection_manager_->process_sent_business_message_album(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SendBusinessMultiMediaQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class BusinessConnectionManager::UploadBusinessMediaQuery final : public Td::ResultHandler {
  Promise<UploadMediaResult> promise_;
  unique_ptr<PendingMessage> message_;
  bool was_uploaded_ = false;
  bool was_thumbnail_uploaded_ = false;

  void delete_thumbnail() {
    if (!was_thumbnail_uploaded_) {
      return;
    }

    auto file_id = get_message_file_id(message_);
    CHECK(file_id.is_valid());
    auto thumbnail_file_id = td_->business_connection_manager_->get_message_thumbnail_file_id(message_, file_id);
    CHECK(thumbnail_file_id.is_valid());
    // always delete partial remote location for the thumbnail, because it can't be reused anyway
    td_->file_manager_->delete_partial_remote_location(thumbnail_file_id);
  }

 public:
  explicit UploadBusinessMediaQuery(Promise<UploadMediaResult> &&promise) : promise_(std::move(promise)) {
  }

  void send(unique_ptr<PendingMessage> message, telegram_api::object_ptr<telegram_api::InputMedia> &&input_media) {
    CHECK(input_media != nullptr);
    message_ = std::move(message);
    was_uploaded_ = FileManager::extract_was_uploaded(input_media);
    was_thumbnail_uploaded_ = FileManager::extract_was_thumbnail_uploaded(input_media);

    if (was_uploaded_ && false) {
      return on_error(Status::Error(400, "FILE_PART_1_MISSING"));
    }

    int32 flags = telegram_api::messages_uploadMedia::BUSINESS_CONNECTION_ID_MASK;
    auto input_peer = td_->dialog_manager_->get_input_peer(message_->dialog_id_, AccessRights::Know);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(telegram_api::messages_uploadMedia(
        flags, message_->business_connection_id_.get(), std::move(input_peer), std::move(input_media))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_uploadMedia>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    delete_thumbnail();

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for UploadBusinessMediaQuery: " << to_string(ptr);
    td_->business_connection_manager_->complete_upload_media(std::move(message_), std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for UploadBusinessMediaQuery: " << status;

    if (was_uploaded_) {
      delete_thumbnail();

      auto file_id = get_message_file_id(message_);
      auto bad_parts = FileManager::get_missing_file_parts(status);
      if (!bad_parts.empty()) {
        td_->business_connection_manager_->upload_media(std::move(message_), std::move(promise_), std::move(bad_parts));
        return;
      } else {
        td_->file_manager_->delete_partial_remote_location_if_needed(file_id, status);
      }
    }
    promise_.set_error(std::move(status));
  }
};

class BusinessConnectionManager::UploadMediaCallback final : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file) final {
    send_closure_later(G()->business_connection_manager(), &BusinessConnectionManager::on_upload_media, file_id,
                       std::move(input_file));
  }
  void on_upload_encrypted_ok(FileId file_id,
                              telegram_api::object_ptr<telegram_api::InputEncryptedFile> input_file) final {
    UNREACHABLE();
  }
  void on_upload_secure_ok(FileId file_id, telegram_api::object_ptr<telegram_api::InputSecureFile> input_file) final {
    UNREACHABLE();
  }
  void on_upload_error(FileId file_id, Status error) final {
    send_closure_later(G()->business_connection_manager(), &BusinessConnectionManager::on_upload_media_error, file_id,
                       std::move(error));
  }
};

class BusinessConnectionManager::UploadThumbnailCallback final : public FileManager::UploadCallback {
 public:
  void on_upload_ok(FileId file_id, telegram_api::object_ptr<telegram_api::InputFile> input_file) final {
    send_closure_later(G()->business_connection_manager(), &BusinessConnectionManager::on_upload_thumbnail, file_id,
                       std::move(input_file));
  }
  void on_upload_encrypted_ok(FileId file_id,
                              telegram_api::object_ptr<telegram_api::InputEncryptedFile> input_file) final {
    UNREACHABLE();
  }
  void on_upload_secure_ok(FileId file_id, telegram_api::object_ptr<telegram_api::InputSecureFile> input_file) final {
    UNREACHABLE();
  }
  void on_upload_error(FileId file_id, Status error) final {
    send_closure_later(G()->business_connection_manager(), &BusinessConnectionManager::on_upload_thumbnail, file_id,
                       nullptr);
  }
};

BusinessConnectionManager::BusinessConnectionManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
  upload_media_callback_ = std::make_shared<UploadMediaCallback>();
  upload_thumbnail_callback_ = std::make_shared<UploadThumbnailCallback>();
}

BusinessConnectionManager::~BusinessConnectionManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), business_connections_);
}

void BusinessConnectionManager::tear_down() {
  parent_.reset();
}

Status BusinessConnectionManager::check_business_connection(const BusinessConnectionId &connection_id,
                                                            DialogId dialog_id) const {
  auto connection = business_connections_.get_pointer(connection_id);
  if (connection == nullptr) {
    return Status::Error(400, "Business connection not found");
  }
  if (dialog_id.get_type() != DialogType::User) {
    return Status::Error(400, "Chat must be a private chat");
  }
  if (dialog_id == DialogId(connection->user_id_)) {
    return Status::Error(400, "Messages must not be sent to self");
  }
  // no need to check connection->can_reply_ and connection->is_disabled_
  return Status::OK();
}

DcId BusinessConnectionManager::get_business_connection_dc_id(const BusinessConnectionId &connection_id) const {
  if (connection_id.is_empty()) {
    return DcId::main();
  }
  auto connection = business_connections_.get_pointer(connection_id);
  CHECK(connection != nullptr);
  return connection->dc_id_;
}

void BusinessConnectionManager::on_update_bot_business_connect(
    telegram_api::object_ptr<telegram_api::botBusinessConnection> &&connection) {
  CHECK(connection != nullptr);
  auto business_connection = make_unique<BusinessConnection>(connection);
  if (!business_connection->is_valid()) {
    LOG(ERROR) << "Receive invalid " << to_string(connection);
    return;
  }
  if (!td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive " << to_string(connection);
    return;
  }

  auto &stored_connection = business_connections_[business_connection->connection_id_];
  stored_connection = std::move(business_connection);
  send_closure(G()->td(), &Td::send_update, get_update_business_connection(stored_connection.get()));
}

void BusinessConnectionManager::on_update_bot_new_business_message(
    const BusinessConnectionId &connection_id, telegram_api::object_ptr<telegram_api::Message> &&message,
    telegram_api::object_ptr<telegram_api::Message> &&reply_to_message) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(message);
    return;
  }
  auto message_object =
      td_->messages_manager_->get_business_message_object(std::move(message), std::move(reply_to_message));
  if (message_object == nullptr) {
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateNewBusinessMessage>(connection_id.get(), std::move(message_object)));
}

void BusinessConnectionManager::on_update_bot_edit_business_message(
    const BusinessConnectionId &connection_id, telegram_api::object_ptr<telegram_api::Message> &&message,
    telegram_api::object_ptr<telegram_api::Message> &&reply_to_message) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(message);
    return;
  }
  auto message_object =
      td_->messages_manager_->get_business_message_object(std::move(message), std::move(reply_to_message));
  if (message_object == nullptr) {
    return;
  }
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessMessageEdited>(connection_id.get(), std::move(message_object)));
}

void BusinessConnectionManager::on_update_bot_delete_business_messages(const BusinessConnectionId &connection_id,
                                                                       DialogId dialog_id, vector<int32> &&messages) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid() || dialog_id.get_type() != DialogType::User) {
    LOG(ERROR) << "Receive deletion of messages " << messages << " in " << dialog_id;
    return;
  }
  vector<int64> message_ids;
  for (auto message : messages) {
    message_ids.push_back(MessageId(ServerMessageId(message)).get());
  }
  td_->dialog_manager_->force_create_dialog(dialog_id, "on_update_bot_delete_business_messages", true);
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessMessagesDeleted>(
          connection_id.get(), td_->dialog_manager_->get_chat_id_object(dialog_id, "updateBusinessMessageDeleted"),
          std::move(message_ids)));
}

void BusinessConnectionManager::get_business_connection(
    const BusinessConnectionId &connection_id, Promise<td_api::object_ptr<td_api::businessConnection>> &&promise) {
  auto connection = business_connections_.get_pointer(connection_id);
  if (connection != nullptr) {
    return promise.set_value(connection->get_business_connection_object(td_));
  }

  if (connection_id.is_empty()) {
    return promise.set_error(Status::Error(400, "Connection iedntifier must be non-empty"));
  }

  auto &queries = get_business_connection_queries_[connection_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), connection_id](Result<telegram_api::object_ptr<telegram_api::Updates>> r_updates) {
          send_closure(actor_id, &BusinessConnectionManager::on_get_business_connection, connection_id,
                       std::move(r_updates));
        });
    td_->create_handler<GetBotBusinessConnectionQuery>(std::move(query_promise))->send(connection_id);
  }
}

void BusinessConnectionManager::on_get_business_connection(
    const BusinessConnectionId &connection_id, Result<telegram_api::object_ptr<telegram_api::Updates>> r_updates) {
  G()->ignore_result_if_closing(r_updates);
  auto queries_it = get_business_connection_queries_.find(connection_id);
  CHECK(queries_it != get_business_connection_queries_.end());
  CHECK(!queries_it->second.empty());
  auto promises = std::move(queries_it->second);
  get_business_connection_queries_.erase(queries_it);
  if (r_updates.is_error()) {
    return fail_promises(promises, r_updates.move_as_error());
  }
  auto connection = business_connections_.get_pointer(connection_id);
  if (connection != nullptr) {
    for (auto &promise : promises) {
      promise.set_value(connection->get_business_connection_object(td_));
    }
    return;
  }

  auto updates_ptr = r_updates.move_as_ok();
  if (updates_ptr->get_id() != telegram_api::updates::ID) {
    LOG(ERROR) << "Receive " << to_string(updates_ptr);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }
  auto updates = telegram_api::move_object_as<telegram_api::updates>(updates_ptr);
  if (updates->updates_.size() != 1 || updates->updates_[0]->get_id() != telegram_api::updateBotBusinessConnect::ID) {
    if (updates->updates_.empty()) {
      return fail_promises(promises, Status::Error(400, "Business connection not found"));
    }
    LOG(ERROR) << "Receive " << to_string(updates);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }
  auto update = telegram_api::move_object_as<telegram_api::updateBotBusinessConnect>(updates->updates_[0]);

  td_->user_manager_->on_get_users(std::move(updates->users_), "on_get_business_connection");
  td_->chat_manager_->on_get_chats(std::move(updates->chats_), "on_get_business_connection");

  auto business_connection = make_unique<BusinessConnection>(update->connection_);
  if (!business_connection->is_valid() || connection_id != business_connection->connection_id_) {
    LOG(ERROR) << "Receive for " << connection_id << ": " << to_string(update->connection_);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }

  auto &stored_connection = business_connections_[connection_id];
  CHECK(stored_connection == nullptr);
  stored_connection = std::move(business_connection);
  for (auto &promise : promises) {
    promise.set_value(stored_connection->get_business_connection_object(td_));
  }
}

MessageInputReplyTo BusinessConnectionManager::create_business_message_input_reply_to(
    td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to) {
  if (reply_to == nullptr) {
    return {};
  }
  switch (reply_to->get_id()) {
    case td_api::inputMessageReplyToStory::ID:
      return {};
    case td_api::inputMessageReplyToMessage::ID: {
      auto reply_to_message = td_api::move_object_as<td_api::inputMessageReplyToMessage>(reply_to);
      auto message_id = MessageId(reply_to_message->message_id_);
      if (!message_id.is_valid() || !message_id.is_server()) {
        return {};
      }
      if (reply_to_message->chat_id_ != 0) {
        return {};
      }
      return MessageInputReplyTo{message_id, DialogId(), MessageQuote(td_, std::move(reply_to_message->quote_))};
    }
    default:
      UNREACHABLE();
      return {};
  }
}

Result<InputMessageContent> BusinessConnectionManager::process_input_message_content(
    td_api::object_ptr<td_api::InputMessageContent> &&input_message_content) {
  if (input_message_content == nullptr) {
    return Status::Error(400, "Can't send message without content");
  }
  if (input_message_content->get_id() == td_api::inputMessageForwarded::ID) {
    return Status::Error(400, "Can't forward messages as business");
  }
  return get_input_message_content(DialogId(), std::move(input_message_content), td_, true);
}

unique_ptr<BusinessConnectionManager::PendingMessage> BusinessConnectionManager::create_business_message_to_send(
    BusinessConnectionId business_connection_id, DialogId dialog_id, MessageInputReplyTo &&input_reply_to,
    bool disable_notification, bool protect_content, int64 effect_id, unique_ptr<ReplyMarkup> &&reply_markup,
    InputMessageContent &&input_content) const {
  auto content = dup_message_content(td_, td_->dialog_manager_->get_my_dialog_id(), input_content.content.get(),
                                     MessageContentDupType::Send, MessageCopyOptions());
  auto message = make_unique<PendingMessage>();
  message->business_connection_id_ = business_connection_id;
  message->dialog_id_ = dialog_id;
  message->input_reply_to_ = std::move(input_reply_to);
  message->noforwards_ = protect_content;
  message->effect_id_ = effect_id;
  message->content_ = std::move(content);
  message->reply_markup_ = std::move(reply_markup);
  message->disable_notification_ = disable_notification;
  message->invert_media_ = input_content.invert_media;
  message->disable_web_page_preview_ = input_content.disable_web_page_preview;
  message->ttl_ = input_content.ttl;
  message->send_emoji_ = std::move(input_content.emoji);
  message->random_id_ = Random::secure_int64();
  return message;
}

void BusinessConnectionManager::send_message(BusinessConnectionId business_connection_id, DialogId dialog_id,
                                             td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to,
                                             bool disable_notification, bool protect_content, int64 effect_id,
                                             td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                             td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                                             Promise<td_api::object_ptr<td_api::businessMessage>> &&promise) {
  TRY_STATUS_PROMISE(promise, check_business_connection(business_connection_id, dialog_id));
  TRY_RESULT_PROMISE(promise, input_content, process_input_message_content(std::move(input_message_content)));
  auto input_reply_to = create_business_message_input_reply_to(std::move(reply_to));
  TRY_RESULT_PROMISE(promise, message_reply_markup,
                     get_reply_markup(std::move(reply_markup), DialogType::User, true, false));

  auto message = create_business_message_to_send(std::move(business_connection_id), dialog_id,
                                                 std::move(input_reply_to), disable_notification, protect_content,
                                                 effect_id, std::move(message_reply_markup), std::move(input_content));

  do_send_message(std::move(message), std::move(promise));
}

void BusinessConnectionManager::do_send_message(unique_ptr<PendingMessage> &&message,
                                                Promise<td_api::object_ptr<td_api::businessMessage>> &&promise) {
  LOG(INFO) << "Send business message to " << message->dialog_id_;

  const auto *content = message->content_.get();
  CHECK(content != nullptr);
  auto content_type = content->get_type();
  if (content_type == MessageContentType::Text) {
    auto input_media = get_message_content_input_media_web_page(td_, content);
    if (input_media == nullptr) {
      td_->create_handler<SendBusinessMessageQuery>(std::move(promise))->send(std::move(message));
    } else {
      td_->create_handler<SendBusinessMediaQuery>(std::move(promise))->send(std::move(message), std::move(input_media));
    }
    return;
  }

  auto input_media = get_input_media(content, td_, message->ttl_, message->send_emoji_, td_->auth_manager_->is_bot());
  if (input_media != nullptr) {
    td_->create_handler<SendBusinessMediaQuery>(std::move(promise))->send(std::move(message), std::move(input_media));
    return;
  }
  if (content_type == MessageContentType::Game || content_type == MessageContentType::Poll ||
      content_type == MessageContentType::Story) {
    return promise.set_error(Status::Error(400, "Message has no file"));
  }
  upload_media(std::move(message), PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise)](
                                                              Result<UploadMediaResult> &&result) mutable {
                 if (result.is_error()) {
                   return promise.set_error(result.move_as_error());
                 }
                 auto message_input_media = result.move_as_ok();
                 send_closure(actor_id, &BusinessConnectionManager::complete_send_media,
                              std::move(message_input_media.message_), std::move(message_input_media.input_media_),
                              std::move(promise));
               }));
}

void BusinessConnectionManager::process_sent_business_message(
    telegram_api::object_ptr<telegram_api::Updates> &&updates_ptr,
    Promise<td_api::object_ptr<td_api::businessMessage>> &&promise) {
  if (updates_ptr->get_id() != telegram_api::updates::ID) {
    LOG(ERROR) << "Receive " << to_string(updates_ptr);
    return promise.set_error(Status::Error(500, "Receive invalid business connection messages"));
  }
  auto updates = telegram_api::move_object_as<telegram_api::updates>(updates_ptr);
  if (updates->updates_.size() != 1 ||
      updates->updates_[0]->get_id() != telegram_api::updateBotNewBusinessMessage::ID) {
    LOG(ERROR) << "Receive " << to_string(updates);
    return promise.set_error(Status::Error(500, "Receive invalid business connection messages"));
  }
  auto update = telegram_api::move_object_as<telegram_api::updateBotNewBusinessMessage>(updates->updates_[0]);

  td_->user_manager_->on_get_users(std::move(updates->users_), "SendBusinessMediaQuery");
  td_->chat_manager_->on_get_chats(std::move(updates->chats_), "SendBusinessMediaQuery");

  promise.set_value(td_->messages_manager_->get_business_message_object(std::move(update->message_),
                                                                        std::move(update->reply_to_message_)));
}

FileId BusinessConnectionManager::get_message_file_id(const unique_ptr<PendingMessage> &message) {
  CHECK(message != nullptr);
  return get_message_content_any_file_id(
      message->content_.get());  // any_file_id, because it could be a photo sent by ID
}

FileId BusinessConnectionManager::get_message_thumbnail_file_id(const unique_ptr<PendingMessage> &message,
                                                                FileId file_id) const {
  FileView file_view = td_->file_manager_->get_file_view(file_id);
  if (get_main_file_type(file_view.get_type()) == FileType::Photo) {
    return FileId();
  }
  CHECK(message != nullptr);
  return get_message_content_thumbnail_file_id(message->content_.get(), td_);
}

void BusinessConnectionManager::upload_media(unique_ptr<PendingMessage> &&message, Promise<UploadMediaResult> &&promise,
                                             vector<int> bad_parts) {
  auto file_id = get_message_file_id(message);
  CHECK(file_id.is_valid());
  FileView file_view = td_->file_manager_->get_file_view(file_id);
  if (file_view.is_encrypted()) {
    return promise.set_error(Status::Error(400, "Can't use encrypted file"));
  }
  if (file_view.has_remote_location() && file_view.main_remote_location().is_web()) {
    return promise.set_error(Status::Error(400, "Can't use a web file"));
  }

  BeingUploadedMedia media;
  media.message_ = std::move(message);
  media.promise_ = std::move(promise);

  if (!file_view.has_remote_location() && file_view.has_url()) {
    return do_upload_media(std::move(media), nullptr);
  }

  LOG(INFO) << "Ask to upload file " << file_id << " with bad parts " << bad_parts;
  CHECK(file_id.is_valid());
  bool is_inserted = being_uploaded_files_.emplace(file_id, std::move(media)).second;
  CHECK(is_inserted);
  // need to call resume_upload synchronously to make upload process consistent with being_uploaded_files_
  // and to send is_uploading_active == true in the updates
  td_->file_manager_->resume_upload(file_id, std::move(bad_parts), upload_media_callback_, 1, 0);
}

void BusinessConnectionManager::complete_send_media(unique_ptr<PendingMessage> &&message,
                                                    telegram_api::object_ptr<telegram_api::InputMedia> &&input_media,
                                                    Promise<td_api::object_ptr<td_api::businessMessage>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  CHECK(message != nullptr);
  CHECK(input_media != nullptr);
  td_->create_handler<SendBusinessMediaQuery>(std::move(promise))->send(std::move(message), std::move(input_media));
}

void BusinessConnectionManager::on_upload_media(FileId file_id,
                                                telegram_api::object_ptr<telegram_api::InputFile> input_file) {
  LOG(INFO) << "File " << file_id << " has been uploaded";

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());
  auto being_uploaded_media = std::move(it->second);
  being_uploaded_files_.erase(it);

  being_uploaded_media.input_file_ = std::move(input_file);
  auto thumbnail_file_id = get_message_thumbnail_file_id(being_uploaded_media.message_, file_id);
  if (being_uploaded_media.input_file_ != nullptr && thumbnail_file_id.is_valid()) {
    // TODO: download thumbnail if needed (like in secret chats)
    LOG(INFO) << "Ask to upload thumbnail " << thumbnail_file_id;
    bool is_inserted = being_uploaded_thumbnails_.emplace(thumbnail_file_id, std::move(being_uploaded_media)).second;
    CHECK(is_inserted);
    td_->file_manager_->upload(thumbnail_file_id, upload_thumbnail_callback_, 1, 0);
  } else {
    do_upload_media(std::move(being_uploaded_media), nullptr);
  }
}

void BusinessConnectionManager::on_upload_media_error(FileId file_id, Status status) {
  CHECK(status.is_error());

  auto it = being_uploaded_files_.find(file_id);
  CHECK(it != being_uploaded_files_.end());
  auto being_uploaded_media = std::move(it->second);
  being_uploaded_files_.erase(it);

  being_uploaded_media.promise_.set_error(std::move(status));
}

void BusinessConnectionManager::on_upload_thumbnail(
    FileId thumbnail_file_id, telegram_api::object_ptr<telegram_api::InputFile> thumbnail_input_file) {
  LOG(INFO) << "Thumbnail " << thumbnail_file_id << " has been uploaded as " << to_string(thumbnail_input_file);

  auto it = being_uploaded_thumbnails_.find(thumbnail_file_id);
  CHECK(it != being_uploaded_thumbnails_.end());
  auto being_uploaded_media = std::move(it->second);
  being_uploaded_thumbnails_.erase(it);

  if (thumbnail_input_file == nullptr) {
    delete_message_content_thumbnail(being_uploaded_media.message_->content_.get(), td_);
  }

  do_upload_media(std::move(being_uploaded_media), std::move(thumbnail_input_file));
}

void BusinessConnectionManager::do_upload_media(BeingUploadedMedia &&being_uploaded_media,
                                                telegram_api::object_ptr<telegram_api::InputFile> input_thumbnail) {
  auto file_id = get_message_file_id(being_uploaded_media.message_);
  auto thumbnail_file_id = get_message_thumbnail_file_id(being_uploaded_media.message_, file_id);
  auto input_file = std::move(being_uploaded_media.input_file_);
  bool have_input_file = input_file != nullptr;
  bool have_input_thumbnail = input_thumbnail != nullptr;
  LOG(INFO) << "Do upload media file " << file_id << " with thumbnail " << thumbnail_file_id
            << ", have_input_file = " << have_input_file << ", have_input_thumbnail = " << have_input_thumbnail;

  const auto *message = being_uploaded_media.message_.get();
  auto input_media = get_input_media(message->content_.get(), td_, std::move(input_file), std::move(input_thumbnail),
                                     file_id, thumbnail_file_id, message->ttl_, message->send_emoji_, true);
  CHECK(input_media != nullptr);
  auto input_media_id = input_media->get_id();
  if (input_media_id == telegram_api::inputMediaDocument::ID || input_media_id == telegram_api::inputMediaPhoto::ID) {
    // can use input media directly
    UploadMediaResult result;
    result.message_ = std::move(being_uploaded_media.message_);
    result.input_media_ = std::move(input_media);
    return being_uploaded_media.promise_.set_value(std::move(result));
  }

  switch (input_media->get_id()) {
    case telegram_api::inputMediaUploadedDocument::ID:
      if (message->content_->get_type() != MessageContentType::Animation) {
        static_cast<telegram_api::inputMediaUploadedDocument *>(input_media.get())->flags_ |=
            telegram_api::inputMediaUploadedDocument::NOSOUND_VIDEO_MASK;
      }
    // fallthrough
    case telegram_api::inputMediaUploadedPhoto::ID:
    case telegram_api::inputMediaDocumentExternal::ID:
    case telegram_api::inputMediaPhotoExternal::ID:
      td_->create_handler<UploadBusinessMediaQuery>(std::move(being_uploaded_media.promise_))
          ->send(std::move(being_uploaded_media.message_), std::move(input_media));
      break;
    default:
      LOG(ERROR) << "Have wrong input media " << to_string(input_media);
      being_uploaded_media.promise_.set_error(Status::Error(400, "Invalid input media"));
  }
}

void BusinessConnectionManager::complete_upload_media(unique_ptr<PendingMessage> &&message,
                                                      telegram_api::object_ptr<telegram_api::MessageMedia> &&media,
                                                      Promise<UploadMediaResult> &&promise) {
  auto *content = message->content_.get();
  auto *caption = get_message_content_caption(content);
  auto has_spoiler = get_message_content_has_spoiler(content);
  auto new_content = get_message_content(td_, caption == nullptr ? FormattedText() : *caption, std::move(media),
                                         td_->dialog_manager_->get_my_dialog_id(), G()->unix_time(), false, UserId(),
                                         nullptr, nullptr, "complete_upload_media");
  set_message_content_has_spoiler(new_content.get(), has_spoiler);

  bool is_content_changed = false;
  bool need_update = false;

  unique_ptr<MessageContent> &old_content = message->content_;
  MessageContentType old_content_type = old_content->get_type();
  MessageContentType new_content_type = new_content->get_type();

  auto old_file_id = get_message_file_id(message);
  if (old_content_type != new_content_type) {
    need_update = true;

    td_->file_manager_->try_merge_documents(old_file_id, get_message_content_any_file_id(new_content.get()));
  } else {
    merge_message_contents(td_, old_content.get(), new_content.get(), false, DialogId(), true, is_content_changed,
                           need_update);
    compare_message_contents(td_, old_content.get(), new_content.get(), is_content_changed, need_update);
  }
  send_closure_later(G()->file_manager(), &FileManager::cancel_upload, old_file_id);

  if (is_content_changed || need_update) {
    old_content = std::move(new_content);
    update_message_content_file_id_remote(old_content.get(), old_file_id);
  } else {
    update_message_content_file_id_remote(old_content.get(), get_message_content_any_file_id(new_content.get()));
  }

  auto input_media = get_input_media(message->content_.get(), td_, message->ttl_, message->send_emoji_, true);
  if (input_media == nullptr) {
    return promise.set_error(Status::Error(400, "Failed to upload file"));
  }
  UploadMediaResult result;
  result.message_ = std::move(message);
  result.input_media_ = std::move(input_media);
  promise.set_value(std::move(result));
}

void BusinessConnectionManager::send_message_album(
    BusinessConnectionId business_connection_id, DialogId dialog_id,
    td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to, bool disable_notification, bool protect_content,
    int64 effect_id, vector<td_api::object_ptr<td_api::InputMessageContent>> &&input_message_contents,
    Promise<td_api::object_ptr<td_api::businessMessages>> &&promise) {
  TRY_STATUS_PROMISE(promise, check_business_connection(business_connection_id, dialog_id));

  vector<InputMessageContent> message_contents;
  for (auto &input_message_content : input_message_contents) {
    TRY_RESULT_PROMISE(promise, message_content, process_input_message_content(std::move(input_message_content)));
    message_contents.push_back(std::move(message_content));
  }
  TRY_STATUS_PROMISE(promise, check_message_group_message_contents(message_contents));

  auto input_reply_to = create_business_message_input_reply_to(std::move(reply_to));

  auto request_id = ++current_media_group_send_request_id_;
  auto &request = media_group_send_requests_[request_id];
  request.upload_results_.resize(message_contents.size());
  request.promise_ = std::move(promise);

  for (size_t media_pos = 0; media_pos < message_contents.size(); media_pos++) {
    auto &message_content = message_contents[media_pos];
    auto message =
        create_business_message_to_send(business_connection_id, dialog_id, input_reply_to.clone(), disable_notification,
                                        protect_content, effect_id, nullptr, std::move(message_content));
    auto input_media = get_input_media(message->content_.get(), td_, message->ttl_, message->send_emoji_,
                                       td_->auth_manager_->is_bot());
    if (input_media != nullptr) {
      auto file_id = get_message_file_id(message);
      CHECK(file_id.is_valid());
      FileView file_view = td_->file_manager_->get_file_view(file_id);
      if (file_view.has_remote_location()) {
        UploadMediaResult result;
        result.message_ = std::move(message);
        result.input_media_ = std::move(input_media);
        on_upload_message_album_media(request_id, media_pos, std::move(result));
        continue;
      }
    }
    upload_media(std::move(message), PromiseCreator::lambda([actor_id = actor_id(this), request_id,
                                                             media_pos](Result<UploadMediaResult> &&result) mutable {
                   send_closure(actor_id, &BusinessConnectionManager::on_upload_message_album_media, request_id,
                                media_pos, std::move(result));
                 }));
  }
}

void BusinessConnectionManager::on_upload_message_album_media(int64 request_id, size_t media_pos,
                                                              Result<UploadMediaResult> &&result) {
  G()->ignore_result_if_closing(result);
  auto it = media_group_send_requests_.find(request_id);
  CHECK(it != media_group_send_requests_.end());
  auto &request = it->second;

  request.upload_results_[media_pos] = std::move(result);
  request.finished_count_++;

  LOG(INFO) << "Receive uploaded media " << media_pos << " for request " << request_id;
  if (request.finished_count_ != request.upload_results_.size()) {
    return;
  }

  auto upload_results = std::move(request.upload_results_);
  auto promise = std::move(request.promise_);
  media_group_send_requests_.erase(it);

  for (auto &r_upload_result : upload_results) {
    if (r_upload_result.is_error()) {
      return promise.set_error(r_upload_result.move_as_error());
    }
  }
  vector<unique_ptr<PendingMessage>> messages;
  vector<telegram_api::object_ptr<telegram_api::inputSingleMedia>> input_single_media;
  for (auto &r_upload_result : upload_results) {
    auto upload_result = r_upload_result.move_as_ok();
    auto message = std::move(upload_result.message_);
    int32 flags = 0;
    const FormattedText *caption = get_message_content_text(message->content_.get());
    auto entities = get_input_message_entities(td_->user_manager_.get(), caption, "on_upload_message_album_media");
    if (!entities.empty()) {
      flags |= telegram_api::inputSingleMedia::ENTITIES_MASK;
    }
    input_single_media.push_back(telegram_api::make_object<telegram_api::inputSingleMedia>(
        flags, std::move(upload_result.input_media_), message->random_id_,
        caption == nullptr ? string() : caption->text, std::move(entities)));
    messages.push_back(std::move(message));
  }

  td_->create_handler<SendBusinessMultiMediaQuery>(std::move(promise))
      ->send(std::move(messages), std::move(input_single_media));
}

void BusinessConnectionManager::process_sent_business_message_album(
    telegram_api::object_ptr<telegram_api::Updates> &&updates_ptr,
    Promise<td_api::object_ptr<td_api::businessMessages>> &&promise) {
  if (updates_ptr->get_id() != telegram_api::updates::ID) {
    LOG(ERROR) << "Receive " << to_string(updates_ptr);
    return promise.set_error(Status::Error(500, "Receive invalid business connection messages"));
  }
  auto updates = telegram_api::move_object_as<telegram_api::updates>(updates_ptr);
  for (auto &update : updates->updates_) {
    if (update->get_id() != telegram_api::updateBotNewBusinessMessage::ID) {
      LOG(ERROR) << "Receive " << to_string(updates);
      return promise.set_error(Status::Error(500, "Receive invalid business connection messages"));
    }
  }
  td_->user_manager_->on_get_users(std::move(updates->users_), "process_sent_business_message_album");
  td_->chat_manager_->on_get_chats(std::move(updates->chats_), "process_sent_business_message_album");

  auto messages = td_api::make_object<td_api::businessMessages>();
  for (auto &update_ptr : updates->updates_) {
    auto update = telegram_api::move_object_as<telegram_api::updateBotNewBusinessMessage>(update_ptr);
    messages->messages_.push_back(td_->messages_manager_->get_business_message_object(
        std::move(update->message_), std::move(update->reply_to_message_)));
  }
  promise.set_value(std::move(messages));
}

td_api::object_ptr<td_api::updateBusinessConnection> BusinessConnectionManager::get_update_business_connection(
    const BusinessConnection *connection) const {
  return td_api::make_object<td_api::updateBusinessConnection>(connection->get_business_connection_object(td_));
}

void BusinessConnectionManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  business_connections_.foreach([&](const BusinessConnectionId &business_connection_id,
                                    const unique_ptr<BusinessConnection> &business_connection) {
    updates.push_back(get_update_business_connection(business_connection.get()));
  });
}

}  // namespace td
