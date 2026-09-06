//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WelcomeMessageManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/FileReferenceManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileUploadId.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageQueryManager.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/tl_helpers.h"

namespace td {

class GetWelcomeMessagesQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::ephemeral_WelcomeMessages>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetWelcomeMessagesQuery(Promise<telegram_api::object_ptr<telegram_api::ephemeral_WelcomeMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Chat not found"));
    }
    send_query(G()->net_query_creator().create(telegram_api::ephemeral_getWelcomeMessages(std::move(input_peer), 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::ephemeral_getWelcomeMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetWelcomeMessagesQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetWelcomeMessagesQuery");
    promise_.set_error(std::move(status));
  }
};

class AddWelcomeMessageQuery final : public Td::ResultHandler {
  DialogId dialog_id_;
  MessageContentUploadId upload_id_;

 public:
  void send(DialogId dialog_id, const MessageContent *content, MessageContentUploadId upload_id, bool invert_media,
            InputMedia &&input_media) {
    dialog_id_ = dialog_id;
    upload_id_ = upload_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Chat not found"));
    }
    td_->message_query_manager_->on_start_sending_message_content(upload_id_, input_media);

    int32 flags = telegram_api::ephemeral_sendMessage::PEER_MASK;
    auto *text = get_message_content_text(content);
    auto entities = get_input_message_entities(td_->user_manager_.get(), text, "SendMediaQuery");
    if (!entities.empty()) {
      flags |= telegram_api::ephemeral_sendMessage::ENTITIES_MASK;
    }
    if (input_media.rich_message_ != nullptr) {
      flags |= telegram_api::ephemeral_sendMessage::RICH_MESSAGE_MASK;
    }
    if (input_media.media_ != nullptr) {
      flags |= telegram_api::ephemeral_sendMessage::MEDIA_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::ephemeral_sendMessage(flags, invert_media, true, false, false, std::move(input_peer),
                                            telegram_api::make_object<telegram_api::inputUserEmpty>(), 0,
                                            text == nullptr ? string() : text->text, std::move(entities),
                                            std::move(input_media.media_), nullptr,
                                            std::move(input_media.rich_message_), Random::secure_int64(), nullptr),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::ephemeral_sendMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for AddWelcomeMessageQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(
        std::move(ptr),
        PromiseCreator::lambda([actor_id = G()->welcome_message_manager(), upload_id = upload_id_](Unit) {
          send_closure(actor_id, &WelcomeMessageManager::cancel_upload_welcome_message_content, upload_id,
                       Status::OK());
        }));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "AddWelcomeMessageQuery");
    td_->message_query_manager_->process_send_message_content_error(upload_id_, std::move(status));
  }
};

class EditWelcomeMessageQuery final : public Td::ResultHandler {
  MessageContentUploadId upload_id_;

 public:
  void send(DialogId dialog_id, EphemeralMessageId ephemeral_message_id, const MessageContent *content,
            MessageContentUploadId upload_id, bool invert_media, InputMedia &&input_media) {
    upload_id_ = upload_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    int32 flags = telegram_api::ephemeral_editMessage::MESSAGE_MASK | telegram_api::ephemeral_editMessage::PEER_MASK;
    vector<telegram_api::object_ptr<telegram_api::MessageEntity>> entities;
    auto *text = get_message_content_text(content);
    if (text != nullptr) {
      entities = get_input_message_entities(td_->user_manager_.get(), text, "EditWelcomeMessageQuery");
      if (!entities.empty()) {
        flags |= telegram_api::ephemeral_editMessage::ENTITIES_MASK;
      }
    }
    if (input_media.media_ != nullptr) {
      flags |= telegram_api::ephemeral_editMessage::MEDIA_MASK;
    }
    if (input_media.rich_message_ != nullptr) {
      flags |= telegram_api::ephemeral_editMessage::RICH_MESSAGE_MASK;
    }
    td_->message_query_manager_->on_start_sending_message_content(upload_id_, input_media);
    send_query(G()->net_query_creator().create(telegram_api::ephemeral_editMessage(
        flags, invert_media, true, std::move(input_peer), telegram_api::make_object<telegram_api::inputUserEmpty>(),
        ephemeral_message_id.get(), text == nullptr ? string() : text->text, std::move(input_media.media_),
        std::move(entities), nullptr, std::move(input_media.rich_message_))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::ephemeral_editMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditWelcomeMessageQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(
        std::move(ptr),
        PromiseCreator::lambda([actor_id = G()->welcome_message_manager(), upload_id = upload_id_](Unit) {
          send_closure(actor_id, &WelcomeMessageManager::cancel_upload_welcome_message_content, upload_id,
                       Status::OK());
        }));
  }

  void on_error(Status status) final {
    LOG(INFO) << "Receive error for EditWelcomeMessageQuery: " << status;
    td_->message_query_manager_->process_send_message_content_error(upload_id_, std::move(status));
  }
};

class DeleteWelcomeMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit DeleteWelcomeMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, EphemeralMessageId ephemeral_message_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Chat not found"));
    }
    send_query(G()->net_query_creator().create(
        telegram_api::ephemeral_deleteWelcomeMessage(std::move(input_peer), ephemeral_message_id.get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::ephemeral_deleteWelcomeMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    LOG(INFO) << "Receive result for DeleteWelcomeMessageQuery: " << result_ptr;
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "DeleteWelcomeMessageQuery");
    promise_.set_error(std::move(status));
  }
};

class DeleteAllWelcomeMessagesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit DeleteAllWelcomeMessagesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Chat not found"));
    }
    send_query(
        G()->net_query_creator().create(telegram_api::ephemeral_deleteAllWelcomeMessages(std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::ephemeral_deleteAllWelcomeMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    LOG(INFO) << "Receive result for DeleteAllWelcomeMessagesQuery: " << result_ptr;
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "DeleteAllWelcomeMessagesQuery");
    promise_.set_error(std::move(status));
  }
};

class WelcomeMessageManager::UploadWelcomeMessageContentCallback final
    : public MessageQueryManager::UploadMessageContentCallback {
  WelcomeMessageManager *manager_;

 public:
  explicit UploadWelcomeMessageContentCallback(WelcomeMessageManager *welcome_message_manager)
      : manager_(welcome_message_manager) {
  }

  void on_message_content_uploaded(MessageContentUploadId upload_id, InputMedia &&input_media) final {
    auto &query = manager_->upload_welcome_message_queries_[upload_id];
    if (query.ephemeral_message_id_.is_valid()) {
      manager_->td_->create_handler<EditWelcomeMessageQuery>()->send(query.dialog_id_, query.ephemeral_message_id_,
                                                                     query.content_.get(), upload_id,
                                                                     query.invert_media_, std::move(input_media));
    } else {
      manager_->td_->create_handler<AddWelcomeMessageQuery>()->send(query.dialog_id_, query.content_.get(), upload_id,
                                                                    query.invert_media_, std::move(input_media));
    }
  }

  void on_message_content_force_uploaded(MessageContentUploadId upload_id, Status status) final {
    if (status.is_error()) {
      return on_failed_to_upload_message_content(upload_id, std::move(status));
    }
    auto &query = manager_->upload_welcome_message_queries_[upload_id];
    auto input_media = get_message_content_input_media(query.content_.get(), manager_->td_, {}, string(), true, -1);
    CHECK(!input_media.is_empty());
    on_message_content_uploaded(upload_id, std::move(input_media));
  }

  void on_uploaded_message_content_updated(MessageContentUploadId upload_id, unique_ptr<MessageContent> &&content,
                                           bool need_merge_files, bool is_content_changed, bool need_update) final {
    auto &query = manager_->upload_welcome_message_queries_[upload_id];
    merge_and_compare_message_contents(manager_->td_, query.content_.get(), content.get(), true, query.dialog_id_,
                                       need_merge_files, vector<FileUploadId>(), MessageSelfDestructType(), 0.0,
                                       nullptr, is_content_changed, need_update);
    query.content_ = std::move(content);
  }

  void on_failed_to_upload_message_content(MessageContentUploadId upload_id, Status error) final {
    manager_->cancel_upload_welcome_message_content(upload_id, std::move(error));
  }

  void on_failed_to_upload_message_content_thumbnail(MessageContentUploadId upload_id, int32 media_pos) final {
    auto &query = manager_->upload_welcome_message_queries_[upload_id];
    delete_message_content_thumbnail(manager_->td_, query.content_.get(), media_pos);
  }
};

template <class StorerT>
void WelcomeMessageManager::WelcomeMessage::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  STORE_FLAG(invert_media_);
  STORE_FLAG(disable_web_page_preview_);
  END_STORE_FLAGS();
  td::store(ephemeral_message_id_, storer);
  store_message_content(content_.get(), storer);
}

template <class ParserT>
void WelcomeMessageManager::WelcomeMessage::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(invert_media_);
  PARSE_FLAG(disable_web_page_preview_);
  END_PARSE_FLAGS();
  td::parse(ephemeral_message_id_, parser);
  parse_message_content(content_, parser);
}

template <class StorerT>
void WelcomeMessageManager::WelcomeMessages::store(StorerT &storer) const {
  BEGIN_STORE_FLAGS();
  END_STORE_FLAGS();
  td::store(messages_, storer);
}

template <class ParserT>
void WelcomeMessageManager::WelcomeMessages::parse(ParserT &parser) {
  BEGIN_PARSE_FLAGS();
  END_PARSE_FLAGS();
  td::parse(messages_, parser);
}

WelcomeMessageManager::WelcomeMessage::~WelcomeMessage() = default;

WelcomeMessageManager::WelcomeMessageManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  upload_welcome_message_content_callback_ = std::make_shared<UploadWelcomeMessageContentCallback>(this);
}

void WelcomeMessageManager::tear_down() {
  parent_.reset();
}

Status WelcomeMessageManager::can_access_welcome_messages(DialogId dialog_id) {
  TRY_STATUS(
      td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Write, "can_access_welcome_messages"));
  if (dialog_id.get_type() == DialogType::User) {
    return Status::Error(400, "Chat can't have welcome messages");
  }
  if (!td_->dialog_manager_->get_dialog_status(dialog_id).can_manage_welcome_messages()) {
    return Status::Error(400, "Have not enough rights");
  }
  return Status::OK();
}

void WelcomeMessageManager::register_welcome_message(DialogId dialog_id, const WelcomeMessage *m, const char *source) {
  register_welcome_message_content(td_, m->content_.get(), {dialog_id, m->ephemeral_message_id_}, source);
}

void WelcomeMessageManager::unregister_welcome_message(DialogId dialog_id, const WelcomeMessage *m,
                                                       const char *source) {
  unregister_welcome_message_content(td_, m->content_.get(), {dialog_id, m->ephemeral_message_id_}, source);
}

void WelcomeMessageManager::change_welcome_message_files(DialogId dialog_id, const vector<FileId> &old_file_ids,
                                                         const vector<FileId> &new_file_ids) {
  if (new_file_ids == old_file_ids) {
    return;
  }

  LOG(INFO) << "Change files in welcome messages of " << dialog_id << " from " << old_file_ids << " to "
            << new_file_ids;
  for (auto file_id : old_file_ids) {
    if (!td::contains(new_file_ids, file_id)) {
      send_closure(G()->file_manager(), &FileManager::delete_file, file_id, Promise<Unit>(),
                   "change_welcome_message_files");
    }
  }

  auto file_source_id = get_welcome_messages_file_source_id(dialog_id);
  if (file_source_id.is_valid()) {
    td_->file_manager_->change_files_source(file_source_id, old_file_ids, new_file_ids, "change_welcome_message_files");
  }
}

string WelcomeMessageManager::get_welcome_messages_database_key(DialogId dialog_id) {
  return PSTRING() << "welcome" << dialog_id.get();
}

void WelcomeMessageManager::save_welcome_messages(DialogId dialog_id) {
  if (!G()->use_chat_info_database()) {
    return;
  }
  LOG(INFO) << "Save welcome messages in " << dialog_id;
  auto *messages = get_welcome_messages(dialog_id);
  if (messages == nullptr) {
    G()->td_db()->get_sqlite_pmc()->erase(get_welcome_messages_database_key(dialog_id), Auto());
  } else {
    G()->td_db()->get_sqlite_pmc()->set(get_welcome_messages_database_key(dialog_id),
                                        log_event_store(*messages).as_slice().str(), Auto());
  }
}

void WelcomeMessageManager::on_external_update_message_content(EphemeralMessageFullId message_full_id,
                                                               const char *source, bool expect_no_message) const {
  auto dialog_id = message_full_id.get_dialog_id();
  auto ephemeral_message_id = message_full_id.get_ephemeral_message_id();
  const auto *m = get_welcome_message(dialog_id, ephemeral_message_id);
  if (expect_no_message && m == nullptr) {
    return;
  }
  LOG_CHECK(m != nullptr) << message_full_id << ' ' << source;
  send_update_chat_welcome_messages(dialog_id);
  // must not save welcome messages, because the message itself wasn't changed
}

void WelcomeMessageManager::delete_pending_message_web_page(EphemeralMessageFullId message_full_id) {
  auto dialog_id = message_full_id.get_dialog_id();
  auto *m = get_welcome_message_editable(dialog_id, message_full_id.get_ephemeral_message_id());
  CHECK(has_message_content_web_page(m->content_.get()));
  unregister_welcome_message(dialog_id, m, "delete_pending_message_web_page");
  remove_message_content_web_page(m->content_.get());
  register_welcome_message(dialog_id, m, "delete_pending_message_web_page");
  // don't need to send updates, because the web page was pending
  save_welcome_messages(dialog_id);
}

WelcomeMessageManager::WelcomeMessageInfo WelcomeMessageManager::parse_welcome_message(
    Td *td, telegram_api::object_ptr<telegram_api::ephemeralMessage> message, const char *source) {
  LOG(DEBUG) << "Receive from " << source << ' ' << to_string(message);
  CHECK(message != nullptr);

  WelcomeMessageInfo message_info;
  if (!message->welcome_template_) {
    LOG(ERROR) << "Receive non-welcome message from " << source;
    return message_info;
  }
  if (message->peer_id_ == nullptr) {
    LOG(ERROR) << "Receive welcome message without chat from " << source;
    return message_info;
  }
  auto dialog_id = DialogId(message->peer_id_);
  auto ephemeral_message_id = EphemeralMessageId(message->id_);
  if (!dialog_id.is_valid() || !ephemeral_message_id.is_valid()) {
    LOG(ERROR) << "Ignore " << ephemeral_message_id << " in " << dialog_id << " from " << source;
    return message_info;
  }
  message_info.dialog_id_ = dialog_id;
  message_info.message_ = make_unique<WelcomeMessage>();
  auto *m = message_info.message_.get();
  m->ephemeral_message_id_ = ephemeral_message_id;
  m->invert_media_ = message->invert_media_;
  m->content_ = get_message_content(
      td,
      get_message_text(td->user_manager_.get(), std::move(message->message_), std::move(message->entities_), true,
                       td->auth_manager_->is_bot(), 0, false, source),
      std::move(message->rich_message_), std::move(message->media_), dialog_id, 0, true, UserId(), nullptr,
      &m->disable_web_page_preview_, source);
  // m->reply_markup = std::move(message->reply_markup_);
  return message_info;
}

void WelcomeMessageManager::on_new_welcome_message(telegram_api::object_ptr<telegram_api::ephemeralMessage> &&message) {
  auto message_info = parse_welcome_message(td_, std::move(message), "on_new_welcome_message");
  auto dialog_id = message_info.dialog_id_;
  if (!dialog_id.is_valid() || can_access_welcome_messages(dialog_id).is_error() ||
      loaded_welcome_messages_.count(dialog_id) == 0) {
    return;
  }

  auto ephemeral_message_id = message_info.message_->ephemeral_message_id_;
  if (get_welcome_message(dialog_id, ephemeral_message_id) != nullptr) {
    return;
  }
  auto &messages = welcome_messages_[dialog_id].messages_;
  auto old_file_ids = get_dialog_welcome_message_file_ids(messages);
  register_welcome_message(dialog_id, message_info.message_.get(), "on_new_welcome_message");
  messages.push_back(std::move(message_info.message_));
  change_welcome_message_files(dialog_id, old_file_ids, get_dialog_welcome_message_file_ids(messages));
  send_update_chat_welcome_messages(dialog_id);
  td_->messages_manager_->on_update_dialog_has_welcome_messages(dialog_id, true);
  reload_welcome_messages(dialog_id, Promise<Unit>());
  save_welcome_messages(dialog_id);
}

void WelcomeMessageManager::on_edited_welcome_message(
    telegram_api::object_ptr<telegram_api::ephemeralMessage> &&message) {
  auto message_info = parse_welcome_message(td_, std::move(message), "on_edited_welcome_message");
  auto dialog_id = message_info.dialog_id_;
  if (!dialog_id.is_valid() || can_access_welcome_messages(dialog_id).is_error() ||
      loaded_welcome_messages_.count(dialog_id) == 0) {
    return;
  }

  auto ephemeral_message_id = message_info.message_->ephemeral_message_id_;
  auto *m = get_welcome_message_editable(dialog_id, ephemeral_message_id);
  if (m == nullptr) {
    return;
  }
  bool need_update = false;
  bool is_content_changed = false;
  update_welcome_message_content(m, message_info.message_.get(), dialog_id, is_content_changed, need_update);
  if (is_content_changed || need_update) {
    auto &messages = welcome_messages_[dialog_id].messages_;
    auto old_file_ids = get_dialog_welcome_message_file_ids(messages);
    unregister_welcome_message(dialog_id, m, "on_edited_welcome_message");
    m->content_ = std::move(message_info.message_->content_);
    register_welcome_message(dialog_id, m, "on_edited_welcome_message");
    change_welcome_message_files(dialog_id, old_file_ids, get_dialog_welcome_message_file_ids(messages));
    save_welcome_messages(dialog_id);
  }
  if (need_update) {
    send_update_chat_welcome_messages(dialog_id);
  }
  reload_welcome_messages(dialog_id, Promise<Unit>());
}

void WelcomeMessageManager::on_delete_welcome_messages(DialogId dialog_id,
                                                       vector<EphemeralMessageId> ephemeral_message_ids) {
  if (!dialog_id.is_valid() || can_access_welcome_messages(dialog_id).is_error() ||
      loaded_welcome_messages_.count(dialog_id) == 0) {
    return;
  }
  vector<EphemeralMessageId> message_ids;
  for (auto ephemeral_message_id : ephemeral_message_ids) {
    if (!ephemeral_message_id.is_valid()) {
      LOG(ERROR) << "Receive " << ephemeral_message_id;
      continue;
    }
    const auto *m = get_welcome_message(dialog_id, ephemeral_message_id);
    if (m == nullptr) {
      continue;
    }
    message_ids.push_back(ephemeral_message_id);
  }
  do_delete_welcome_messages(dialog_id, message_ids);
  reload_welcome_messages(dialog_id, Promise<Unit>());
}

void WelcomeMessageManager::do_delete_welcome_messages(DialogId dialog_id,
                                                       vector<EphemeralMessageId> ephemeral_message_ids) {
  if (ephemeral_message_ids.empty()) {
    return;
  }
  auto &messages = welcome_messages_[dialog_id].messages_;
  auto old_file_ids = get_dialog_welcome_message_file_ids(messages);
  td::remove_if(messages, [&](const auto &welcome_message) {
    if (td::contains(ephemeral_message_ids, welcome_message->ephemeral_message_id_)) {
      unregister_welcome_message(dialog_id, welcome_message.get(), "do_delete_welcome_messages");
      return true;
    }
    return false;
  });
  change_welcome_message_files(dialog_id, old_file_ids, get_dialog_welcome_message_file_ids(messages));
  if (messages.empty()) {
    td_->messages_manager_->on_update_dialog_has_welcome_messages(dialog_id, false);
    welcome_messages_.erase(dialog_id);
  }
  send_update_chat_welcome_messages(dialog_id);
  save_welcome_messages(dialog_id);
}

const WelcomeMessageManager::WelcomeMessages *WelcomeMessageManager::get_welcome_messages(DialogId dialog_id) const {
  auto it = welcome_messages_.find(dialog_id);
  if (it == welcome_messages_.end()) {
    return nullptr;
  }
  return &it->second;
}

const WelcomeMessageManager::WelcomeMessage *WelcomeMessageManager::get_welcome_message(
    DialogId dialog_id, EphemeralMessageId ephemeral_message_id) const {
  auto messages = get_welcome_messages(dialog_id);
  if (messages != nullptr) {
    for (auto &message : messages->messages_) {
      if (message->ephemeral_message_id_ == ephemeral_message_id) {
        return message.get();
      }
    }
  }
  return nullptr;
}

WelcomeMessageManager::WelcomeMessage *WelcomeMessageManager::get_welcome_message_editable(
    DialogId dialog_id, EphemeralMessageId ephemeral_message_id) {
  auto it = welcome_messages_.find(dialog_id);
  if (it != welcome_messages_.end()) {
    for (auto &message : it->second.messages_) {
      if (message->ephemeral_message_id_ == ephemeral_message_id) {
        return message.get();
      }
    }
  }
  return nullptr;
}

void WelcomeMessageManager::update_welcome_message_content(WelcomeMessage *old_message, WelcomeMessage *new_message,
                                                           DialogId dialog_id, bool &is_content_changed,
                                                           bool &need_update) {
  if (old_message->disable_web_page_preview_ != new_message->disable_web_page_preview_) {
    if (old_message->disable_web_page_preview_ && has_message_content_web_page(new_message->content_.get())) {
      need_update = true;
    } else if (new_message->disable_web_page_preview_ && has_message_content_web_page(old_message->content_.get())) {
      need_update = true;
    }
    old_message->disable_web_page_preview_ = new_message->disable_web_page_preview_;
  }
  if (old_message->invert_media_ != new_message->invert_media_) {
    old_message->invert_media_ = new_message->invert_media_;
    need_update = true;
  }
  merge_and_compare_message_contents(td_, old_message->content_.get(), new_message->content_.get(), false, dialog_id,
                                     false, vector<FileUploadId>(), MessageSelfDestructType(), 0.0, nullptr,
                                     is_content_changed, need_update);
}

void WelcomeMessageManager::load_welcome_messages(DialogId dialog_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, can_access_welcome_messages(dialog_id));
  if (loaded_welcome_messages_.count(dialog_id)) {
    promise.set_value(Unit());
    return reload_welcome_messages(dialog_id, Promise<Unit>());
  }
  if (G()->use_chat_info_database() && reload_welcome_messages_queries_.count(dialog_id) == 0) {
    auto &queries = load_welcome_messages_from_database_queries_[dialog_id];
    queries.push_back(std::move(promise));
    if (queries.size() == 1u) {
      LOG(INFO) << "Load welcome messages of " << dialog_id << " from database";
      G()->td_db()->get_sqlite_pmc()->get(get_welcome_messages_database_key(dialog_id),
                                          PromiseCreator::lambda([actor_id = actor_id(this), dialog_id](string value) {
                                            send_closure(actor_id,
                                                         &WelcomeMessageManager::on_load_welcome_messages_from_database,
                                                         dialog_id, std::move(value));
                                          }));
    }
    return;
  }
  reload_welcome_messages(dialog_id, std::move(promise));
}

void WelcomeMessageManager::on_load_welcome_messages_from_database(DialogId dialog_id, string value) {
  auto query_it = load_welcome_messages_from_database_queries_.find(dialog_id);
  CHECK(query_it != load_welcome_messages_from_database_queries_.end());
  auto promises = std::move(query_it->second);
  CHECK(!promises.empty());
  load_welcome_messages_from_database_queries_.erase(query_it);

  if (loaded_welcome_messages_.count(dialog_id) != 0) {
    // ignore database value
    return set_promises(promises);
  }
  WelcomeMessages messages;
  if (!value.empty() && log_event_parse(messages, value).is_ok()) {
    auto my_user_id = td_->user_manager_->get_my_id();
    auto is_bot = td_->auth_manager_->is_bot();
    Dependencies dependencies;
    for (auto &message : messages.messages_) {
      add_message_content_dependencies(dependencies, message->content_.get(), my_user_id, is_bot);
    }
    if (dependencies.resolve_force(td_, "on_load_welcome_messages_from_database")) {
      loaded_welcome_messages_.insert(dialog_id);
      welcome_messages_[dialog_id] = std::move(messages);
      send_update_chat_welcome_messages(dialog_id);
      return set_promises(promises);
    }
  }
  for (auto &promise : promises) {
    reload_welcome_messages(dialog_id, std::move(promise));
  }
}

void WelcomeMessageManager::reload_welcome_messages(DialogId dialog_id, Promise<Unit> &&promise) {
  CHECK(dialog_id.is_valid());
  TRY_STATUS_PROMISE(promise, can_access_welcome_messages(dialog_id));
  auto &queries = reload_welcome_messages_queries_[dialog_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1u) {
    auto query_promise = PromiseCreator::lambda(
        [actor_id = actor_id(this),
         dialog_id](Result<telegram_api::object_ptr<telegram_api::ephemeral_WelcomeMessages>> r_messages) {
          send_closure(actor_id, &WelcomeMessageManager::on_get_welcome_messages, dialog_id, std::move(r_messages));
        });
    td_->create_handler<GetWelcomeMessagesQuery>(std::move(query_promise))->send(dialog_id);
  }
}

void WelcomeMessageManager::on_get_welcome_messages(
    DialogId dialog_id, Result<telegram_api::object_ptr<telegram_api::ephemeral_WelcomeMessages>> r_messages) {
  G()->ignore_result_if_closing(r_messages);
  auto query_it = reload_welcome_messages_queries_.find(dialog_id);
  CHECK(query_it != reload_welcome_messages_queries_.end());
  auto promises = std::move(query_it->second);
  CHECK(!promises.empty());
  reload_welcome_messages_queries_.erase(query_it);

  if (r_messages.is_error()) {
    return fail_promises(promises, r_messages.move_as_error());
  }
  auto ephemeral_messages_ptr = r_messages.move_as_ok();
  if (ephemeral_messages_ptr->get_id() != telegram_api::ephemeral_welcomeMessages::ID) {
    LOG(ERROR) << "Receive " << to_string(ephemeral_messages_ptr);
    return fail_promises(promises, Status::Error(500, "Receive invalid response"));
  }
  auto ephemeral_messages =
      telegram_api::move_object_as<telegram_api::ephemeral_welcomeMessages>(ephemeral_messages_ptr);
  vector<unique_ptr<WelcomeMessage>> welcome_messages;
  bool need_update = false;
  bool is_content_changed = false;
  for (auto &message : ephemeral_messages->messages_) {
    auto message_info = parse_welcome_message(td_, std::move(message), "on_get_welcome_messages");
    if (dialog_id != message_info.dialog_id_) {
      LOG(ERROR) << "Receive welcome message in " << message_info.dialog_id_ << " instead of " << dialog_id;
      return fail_promises(promises, Status::Error(500, "Receive invalid response"));
    }
    auto *old_message = get_welcome_message_editable(dialog_id, message_info.message_->ephemeral_message_id_);
    if (old_message != nullptr) {
      update_welcome_message_content(old_message, message_info.message_.get(), dialog_id, is_content_changed,
                                     need_update);
    }
    welcome_messages.push_back(std::move(message_info.message_));
  }
  if (loaded_welcome_messages_.insert(dialog_id).second) {
    need_update = true;
  }

  if (welcome_messages.empty()) {
    if (delete_all_welcome_messages(dialog_id)) {
      need_update = true;
    }
  } else {
    auto &messages = welcome_messages_[dialog_id].messages_;
    if (messages.size() != welcome_messages.size()) {
      need_update = true;
    } else {
      for (size_t i = 0; i < messages.size(); i++) {
        if (messages[i]->ephemeral_message_id_ != welcome_messages[i]->ephemeral_message_id_) {
          need_update = true;
        }
      }
    }
    if (need_update || is_content_changed) {
      auto old_file_ids = get_dialog_welcome_message_file_ids(messages);
      for (auto &message : messages) {
        unregister_welcome_message(dialog_id, message.get(), "on_get_welcome_messages");
      }
      messages = std::move(welcome_messages);
      for (auto &message : messages) {
        register_welcome_message(dialog_id, message.get(), "on_get_welcome_messages");
      }
      change_welcome_message_files(dialog_id, old_file_ids, get_dialog_welcome_message_file_ids(messages));
    }
    td_->messages_manager_->on_update_dialog_has_welcome_messages(dialog_id, true);
  }
  if (need_update) {
    send_update_chat_welcome_messages(dialog_id);
  }
  if (is_content_changed || need_update) {
    save_welcome_messages(dialog_id);
  }
  set_promises(promises);
}

void WelcomeMessageManager::cancel_upload_welcome_message_content(MessageContentUploadId upload_id, Status status) {
  auto it = upload_welcome_message_queries_.find(upload_id);
  if (it == upload_welcome_message_queries_.end()) {
    return;
  }
  auto promise = std::move(it->second.promise_);
  upload_welcome_message_queries_.erase(upload_id);

  td_->message_query_manager_->cancel_upload_message_content(upload_id);
  if (status.is_error()) {
    promise.set_error(std::move(status));
  } else {
    promise.set_value(Unit());
  }
}

void WelcomeMessageManager::add_welcome_message(DialogId dialog_id, EphemeralMessageId ephemeral_message_id,
                                                td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                                                Promise<Unit> &&promise, bool is_recursive) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_STATUS_PROMISE(promise, can_access_welcome_messages(dialog_id));
  if (!is_recursive && loaded_welcome_messages_.count(dialog_id) == 0) {
    return reload_welcome_messages(
        dialog_id, PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, ephemeral_message_id,
                                           input_message_content = std::move(input_message_content),
                                           promise = std::move(promise)](Result<Unit> result) mutable {
          if (result.is_error()) {
            return promise.set_error(result.move_as_error());
          }
          send_closure(actor_id, &WelcomeMessageManager::add_welcome_message, dialog_id, ephemeral_message_id,
                       std::move(input_message_content), std::move(promise), true);
        }));
  }

  bool is_premium = td_->option_manager_->get_option_boolean("is_premium") || td_->auth_manager_->is_bot();
  TRY_RESULT_PROMISE(promise, content,
                     get_input_message_content(dialog_id, std::move(input_message_content), td_, is_premium));
  auto content_type = content.content->get_type();
  if (!is_allowed_ephemeral_message_content(content_type)) {
    return promise.set_error(400, "Unsupported welcome message content type");
  }
  if (!content.ttl.is_empty()) {
    return promise.set_error(400, "Can't enable self-destruction for media");
  }

  auto upload_id = td_->message_query_manager_->create_upload_message_content_query(
      dialog_id, content.content.get(), MessageSelfDestructType(), content.emoji, false, false,
      upload_welcome_message_content_callback_);
  auto &query = upload_welcome_message_queries_[upload_id];
  query.dialog_id_ = dialog_id;
  query.ephemeral_message_id_ = ephemeral_message_id;
  query.content_ = std::move(content.content);
  query.invert_media_ = content.invert_media;
  query.promise_ = std::move(promise);
  td_->message_query_manager_->start_upload_message_content(upload_id);
  td_->messages_manager_->on_update_dialog_has_welcome_messages(dialog_id, true);
}

void WelcomeMessageManager::edit_welcome_message(
    DialogId dialog_id, EphemeralMessageId ephemeral_message_id,
    td_api::object_ptr<td_api::InputMessageContent> &&input_message_content, Promise<Unit> &&promise) {
  if (!ephemeral_message_id.is_valid()) {
    return promise.set_error(400, "Invalid welcome message identifier specified");
  }
  add_welcome_message(dialog_id, ephemeral_message_id, std::move(input_message_content), std::move(promise));
}

void WelcomeMessageManager::delete_welcome_message(DialogId dialog_id, EphemeralMessageId ephemeral_message_id,
                                                   Promise<Unit> &&promise) {
  const auto *m = get_welcome_message(dialog_id, ephemeral_message_id);
  if (m == nullptr) {
    return promise.set_error(400, "Message not found");
  }
  do_delete_welcome_messages(dialog_id, {ephemeral_message_id});
  td_->create_handler<DeleteWelcomeMessageQuery>(std::move(promise))->send(dialog_id, ephemeral_message_id);
}

void WelcomeMessageManager::delete_all_welcome_messages(DialogId dialog_id, Promise<Unit> &&promise) {
  if (delete_all_welcome_messages(dialog_id)) {
    send_update_chat_welcome_messages(dialog_id);
    save_welcome_messages(dialog_id);
    td_->create_handler<DeleteAllWelcomeMessagesQuery>(std::move(promise))->send(dialog_id);
  }
}

void WelcomeMessageManager::drop_welcome_messages(DialogId dialog_id, bool is_empty) {
  if (delete_all_welcome_messages(dialog_id)) {
    send_update_chat_welcome_messages(dialog_id);
    save_welcome_messages(dialog_id);
  }
  if (!is_empty) {
    loaded_welcome_messages_.erase(dialog_id);
  }
}

bool WelcomeMessageManager::delete_all_welcome_messages(DialogId dialog_id) {
  td_->messages_manager_->on_update_dialog_has_welcome_messages(dialog_id, false);
  auto it = welcome_messages_.find(dialog_id);
  if (it != welcome_messages_.end()) {
    change_welcome_message_files(dialog_id, get_dialog_welcome_message_file_ids(it->second.messages_), {});
    for (auto &message : it->second.messages_) {
      unregister_welcome_message(dialog_id, message.get(), "delete_all_welcome_messages");
    }
    welcome_messages_.erase(it);
    return true;
  }
  return false;
}

vector<FileId> WelcomeMessageManager::get_dialog_welcome_message_file_ids(
    const vector<unique_ptr<WelcomeMessage>> &messages) const {
  vector<FileId> file_ids;
  for (auto &message : messages) {
    append(file_ids, get_message_content_file_ids(message->content_.get(), td_));
  }
  return file_ids;
}

td_api::object_ptr<td_api::welcomeMessage> WelcomeMessageManager::get_welcome_message_object(
    const WelcomeMessage *m) const {
  CHECK(m != nullptr);
  auto content = get_message_content_object(m->content_.get(), td_, DialogId(), MessageId(), DialogId(), false, false,
                                            false, DialogId(), 0, 0, false, true, -1, m->invert_media_,
                                            m->disable_web_page_preview_, "get_welcome_message_object");
  return td_api::make_object<td_api::welcomeMessage>(m->ephemeral_message_id_.get(), std::move(content));
}

vector<td_api::object_ptr<td_api::welcomeMessage>> WelcomeMessageManager::get_welcome_messages_object(
    const WelcomeMessages &messages) const {
  return transform(messages.messages_, [&](const unique_ptr<WelcomeMessage> &message) {
    return get_welcome_message_object(message.get());
  });
}

td_api::object_ptr<td_api::updateChatWelcomeMessages> WelcomeMessageManager::get_update_chat_welcome_messages_object(
    DialogId dialog_id, const WelcomeMessages &messages) const {
  return td_api::make_object<td_api::updateChatWelcomeMessages>(
      td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatWelcomeMessages"),
      get_welcome_messages_object(messages));
}

void WelcomeMessageManager::send_update_chat_welcome_messages(DialogId dialog_id) const {
  auto messages = get_welcome_messages(dialog_id);
  if (messages == nullptr) {
    send_closure(G()->td(), &Td::send_update, get_update_chat_welcome_messages_object(dialog_id, WelcomeMessages()));
  } else {
    send_closure(G()->td(), &Td::send_update, get_update_chat_welcome_messages_object(dialog_id, *messages));
  }
}

FileSourceId WelcomeMessageManager::get_welcome_messages_file_source_id(DialogId dialog_id) {
  if (td_->auth_manager_->is_bot()) {
    return FileSourceId();
  }
  switch (dialog_id.get_type()) {
    case DialogType::Chat:
    case DialogType::Channel:
      // ok
      break;
    default:
      return FileSourceId();
  }

  auto &file_source_id = dialog_to_file_source_id_[dialog_id];
  if (!file_source_id.is_valid()) {
    file_source_id = td_->file_reference_manager_->create_welcome_messages_file_source(dialog_id);
  }
  return file_source_id;
}

void WelcomeMessageManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!td_->auth_manager_->is_authorized() || td_->auth_manager_->is_bot()) {
    return;
  }

  for (auto &it : welcome_messages_) {
    updates.push_back(get_update_chat_welcome_messages_object(it.first, it.second));
  }
}

}  // namespace td
