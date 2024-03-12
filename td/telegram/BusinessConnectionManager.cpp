//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/BusinessConnectionManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageCopyOptions.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

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
    return td_api::make_object<td_api::businessConnection>(
        connection_id_.get(), td->contacts_manager_->get_user_id_object(user_id_, "businessConnection"),
        connection_date_, can_reply_, is_disabled_);
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
  int64 media_album_id_ = 0;
  int64 random_id_ = 0;
  bool noforwards_ = false;
  bool disable_notification_ = false;
  bool invert_media_ = false;
  bool disable_web_page_preview_ = false;
};

class BusinessConnectionManager::SendBusinessMessageQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::message>> promise_;
  unique_ptr<PendingMessage> message_;

 public:
  explicit SendBusinessMessageQuery(Promise<td_api::object_ptr<td_api::message>> &&promise)
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
    auto entities = get_input_message_entities(td_->contacts_manager_.get(), message_text, "SendBusinessMessageQuery");
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
            message_->random_id_, get_input_reply_markup(td_->contacts_manager_.get(), message_->reply_markup_),
            std::move(entities), 0, nullptr, nullptr),
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
    promise_.set_value(nullptr);  // TODO
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SendBusinessMessageQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

class BusinessConnectionManager::SendBusinessMediaQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::message>> promise_;
  unique_ptr<PendingMessage> message_;

 public:
  explicit SendBusinessMediaQuery(Promise<td_api::object_ptr<td_api::message>> &&promise)
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
    auto entities = get_input_message_entities(td_->contacts_manager_.get(), message_text, "SendBusinessMediaQuery");
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
                                         get_input_reply_markup(td_->contacts_manager_.get(), message_->reply_markup_),
                                         std::move(entities), 0, nullptr, nullptr),
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
    promise_.set_value(nullptr);  // TODO
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for SendBusinessMediaQuery: " << status;
    promise_.set_error(std::move(status));
  }
};

BusinessConnectionManager::BusinessConnectionManager(Td *td, ActorShared<> parent)
    : td_(td), parent_(std::move(parent)) {
}

BusinessConnectionManager::~BusinessConnectionManager() = default;

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
  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateBusinessConnection>(stored_connection->get_business_connection_object(td_)));
}

void BusinessConnectionManager::on_update_bot_new_business_message(
    const BusinessConnectionId &connection_id, telegram_api::object_ptr<telegram_api::Message> &&message) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(message);
    return;
  }
  auto message_object = td_->messages_manager_->get_business_message_object(std::move(message));
  if (message_object == nullptr) {
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateNewBusinessMessage>(connection_id.get(), std::move(message_object)));
}

void BusinessConnectionManager::on_update_bot_edit_business_message(
    const BusinessConnectionId &connection_id, telegram_api::object_ptr<telegram_api::Message> &&message) {
  if (!td_->auth_manager_->is_bot() || !connection_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(message);
    return;
  }
  auto message_object = td_->messages_manager_->get_business_message_object(std::move(message));
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
      return fail_promises(promises, Status::Error(400, "Business connnection not found"));
    }
    LOG(ERROR) << "Receive " << to_string(updates);
    return fail_promises(promises, Status::Error(500, "Receive invalid business connection info"));
  }
  auto update = telegram_api::move_object_as<telegram_api::updateBotBusinessConnect>(updates->updates_[0]);

  td_->contacts_manager_->on_get_users(std::move(updates->users_), "on_get_business_connection");
  td_->contacts_manager_->on_get_chats(std::move(updates->chats_), "on_get_business_connection");

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
      FormattedText quote;
      int32 quote_position = 0;
      if (reply_to_message->quote_ != nullptr) {
        int32 ltrim_count = 0;
        auto r_quote = get_formatted_text(td_, td_->dialog_manager_->get_my_dialog_id(),
                                          std::move(reply_to_message->quote_->text_), td_->auth_manager_->is_bot(),
                                          true, true, false, &ltrim_count);
        if (r_quote.is_ok() && !r_quote.ok().text.empty()) {
          quote = r_quote.move_as_ok();
          quote_position = reply_to_message->quote_->position_;
          if (0 <= quote_position && quote_position <= 1000000) {  // some unreasonably big bound
            quote_position += ltrim_count;
          } else {
            quote_position = 0;
          }
        }
      }
      return MessageInputReplyTo{message_id, DialogId(), std::move(quote), quote_position};
    }
    default:
      UNREACHABLE();
      return {};
  }
}

Result<InputMessageContent> BusinessConnectionManager::process_input_message_content(
    DialogId dialog_id, td_api::object_ptr<td_api::InputMessageContent> &&input_message_content) {
  if (input_message_content == nullptr) {
    return Status::Error(400, "Can't send message without content");
  }
  if (input_message_content->get_id() == td_api::inputMessageForwarded::ID) {
    return Status::Error(400, "Can't forward messages as business");
  }
  return get_input_message_content(dialog_id, std::move(input_message_content), td_, true);
}

unique_ptr<BusinessConnectionManager::PendingMessage> BusinessConnectionManager::create_business_message_to_send(
    BusinessConnectionId business_connection_id, DialogId dialog_id, MessageInputReplyTo &&input_reply_to,
    bool disable_notification, bool protect_content, unique_ptr<ReplyMarkup> &&reply_markup,
    InputMessageContent &&input_content) const {
  auto content = dup_message_content(td_, td_->dialog_manager_->get_my_dialog_id(), input_content.content.get(),
                                     MessageContentDupType::Send, MessageCopyOptions());
  auto message = make_unique<PendingMessage>();
  message->business_connection_id_ = business_connection_id;
  message->dialog_id_ = dialog_id;
  message->input_reply_to_ = std::move(input_reply_to);
  message->noforwards_ = protect_content;
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
                                             bool disable_notification, bool protect_content,
                                             td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                             td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                                             Promise<td_api::object_ptr<td_api::message>> &&promise) {
  TRY_STATUS_PROMISE(promise, check_business_connection(business_connection_id, dialog_id));
  TRY_RESULT_PROMISE(promise, input_content,
                     process_input_message_content(dialog_id, std::move(input_message_content)));
  auto input_reply_to = create_business_message_input_reply_to(std::move(reply_to));
  TRY_RESULT_PROMISE(promise, message_reply_markup,
                     get_reply_markup(std::move(reply_markup), DialogType::User, td_->auth_manager_->is_bot(), false));

  auto message = create_business_message_to_send(std::move(business_connection_id), dialog_id,
                                                 std::move(input_reply_to), disable_notification, protect_content,
                                                 std::move(message_reply_markup), std::move(input_content));

  do_send_message(std::move(message), std::move(promise));
}

void BusinessConnectionManager::do_send_message(unique_ptr<PendingMessage> &&message,
                                                Promise<td_api::object_ptr<td_api::message>> &&promise) {
  LOG(INFO) << "Send business message to " << message->dialog_id_;

  auto content = message->content_.get();
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
  promise.set_error(Status::Error(400, "Unsupported"));
}

}  // namespace td
