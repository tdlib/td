//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SavedMessagesManager.h"

#include "td/telegram/AffectedHistory.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"

namespace td {

class GetPinnedSavedDialogsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> promise_;

 public:
  explicit GetPinnedSavedDialogsQuery(Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::messages_getPinnedSavedDialogs()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getPinnedSavedDialogs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetPinnedSavedDialogsQuery: " << to_string(result);
    td_->saved_messages_manager_->on_get_saved_messages_topics(true, std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedDialogsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> promise_;

 public:
  explicit GetSavedDialogsQuery(Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int32 offset_date, MessageId offset_message_id, DialogId offset_dialog_id, int32 limit) {
    auto input_peer = DialogManager::get_input_peer_force(offset_dialog_id);
    CHECK(input_peer != nullptr);

    int32 flags = telegram_api::messages_getSavedDialogs::EXCLUDE_PINNED_MASK;
    send_query(G()->net_query_creator().create(telegram_api::messages_getSavedDialogs(
        flags, false /*ignored*/, offset_date, offset_message_id.get_server_message_id().get(), std::move(input_peer),
        limit, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedDialogs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedDialogsQuery: " << to_string(result);
    td_->saved_messages_manager_->on_get_saved_messages_topics(false, std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedHistoryQuery final : public Td::ResultHandler {
  Promise<MessagesInfo> promise_;

 public:
  explicit GetSavedHistoryQuery(Promise<MessagesInfo> &&promise) : promise_(std::move(promise)) {
  }

  void send(SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id, int32 offset, int32 limit) {
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::messages_getSavedHistory(
        std::move(saved_input_peer), from_message_id.get_server_message_id().get(), 0, offset, limit, 0, 0, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
    auto info = get_messages_info(td_, my_dialog_id, result_ptr.move_as_ok(), "GetSavedHistoryQuery");
    LOG_IF(ERROR, info.is_channel_messages) << "Receive channel messages in GetSavedHistoryQuery";
    promise_.set_value(std::move(info));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedMessageByDateQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::message>> promise_;
  int32 date_ = 0;

 public:
  explicit GetSavedMessageByDateQuery(Promise<td_api::object_ptr<td_api::message>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(SavedMessagesTopicId saved_messages_topic_id, int32 date) {
    date_ = date;
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getSavedHistory(std::move(saved_input_peer), 0, date, -3, 5, 0, 0, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
    auto info = get_messages_info(td_, my_dialog_id, result_ptr.move_as_ok(), "GetSavedMessageByDateQuery");
    LOG_IF(ERROR, info.is_channel_messages) << "Receive channel messages in GetSavedMessageByDateQuery";
    for (auto &message : info.messages) {
      auto message_date = MessagesManager::get_message_date(message);
      auto message_dialog_id = DialogId::get_message_dialog_id(message);
      if (message_dialog_id != my_dialog_id) {
        LOG(ERROR) << "Receive message in wrong " << message_dialog_id << " instead of " << my_dialog_id;
        continue;
      }
      if (message_date != 0 && message_date <= date_) {
        auto message_full_id = td_->messages_manager_->on_get_message(std::move(message), false, false, false,
                                                                      "GetSavedMessageByDateQuery");
        if (message_full_id != MessageFullId()) {
          return promise_.set_value(
              td_->messages_manager_->get_message_object(message_full_id, "GetSavedMessageByDateQuery"));
        }
      }
    }
    promise_.set_value(nullptr);
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteSavedHistoryQuery final : public Td::ResultHandler {
  Promise<AffectedHistory> promise_;

 public:
  explicit DeleteSavedHistoryQuery(Promise<AffectedHistory> &&promise) : promise_(std::move(promise)) {
  }

  void send(SavedMessagesTopicId saved_messages_topic_id) {
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags = 0;
    send_query(G()->net_query_creator().create(telegram_api::messages_deleteSavedHistory(
        flags, std::move(saved_input_peer), std::numeric_limits<int32>::max(), 0, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_deleteSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(AffectedHistory(result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteSavedMessagesByDateQuery final : public Td::ResultHandler {
  Promise<AffectedHistory> promise_;

 public:
  explicit DeleteSavedMessagesByDateQuery(Promise<AffectedHistory> &&promise) : promise_(std::move(promise)) {
  }

  void send(SavedMessagesTopicId saved_messages_topic_id, int32 min_date, int32 max_date) {
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags =
        telegram_api::messages_deleteHistory::MIN_DATE_MASK | telegram_api::messages_deleteHistory::MAX_DATE_MASK;

    send_query(G()->net_query_creator().create(
        telegram_api::messages_deleteSavedHistory(flags, std::move(saved_input_peer), 0, min_date, max_date)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_deleteSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(AffectedHistory(result_ptr.move_as_ok()));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ToggleSavedDialogPinQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleSavedDialogPinQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(SavedMessagesTopicId saved_messages_topic_id, bool is_pinned) {
    auto saved_input_peer = saved_messages_topic_id.get_input_dialog_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags = 0;
    if (is_pinned) {
      flags |= telegram_api::messages_toggleSavedDialogPin::PINNED_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_toggleSavedDialogPin(flags, false /*ignored*/, std::move(saved_input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_toggleSavedDialogPin>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->saved_messages_manager_->on_update_pinned_saved_messages_topics();
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ReorderPinnedSavedDialogsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ReorderPinnedSavedDialogsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(const vector<SavedMessagesTopicId> &saved_messages_topic_ids) {
    auto order = transform(saved_messages_topic_ids, [td = td_](SavedMessagesTopicId saved_messages_topic_id) {
      auto saved_input_peer = saved_messages_topic_id.get_input_dialog_peer(td);
      CHECK(saved_input_peer != nullptr);
      return saved_input_peer;
    });
    int32 flags = telegram_api::messages_reorderPinnedSavedDialogs::FORCE_MASK;
    send_query(G()->net_query_creator().create(
        telegram_api::messages_reorderPinnedSavedDialogs(flags, true /*ignored*/, std::move(order))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_reorderPinnedSavedDialogs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    bool result = result_ptr.move_as_ok();
    if (!result) {
      return on_error(Status::Error(400, "Result is false"));
    }
    td_->saved_messages_manager_->on_update_pinned_saved_messages_topics();
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

SavedMessagesManager::SavedMessagesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void SavedMessagesManager::tear_down() {
  parent_.reset();
}

SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::get_topic(
    SavedMessagesTopicId saved_messages_topic_id) {
  CHECK(saved_messages_topic_id.is_valid());
  auto it = saved_messages_topics_.find(saved_messages_topic_id);
  if (it == saved_messages_topics_.end()) {
    return nullptr;
  }
  return it->second.get();
}

SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::add_topic(
    SavedMessagesTopicId saved_messages_topic_id) {
  CHECK(saved_messages_topic_id.is_valid());
  auto &result = saved_messages_topics_[saved_messages_topic_id];
  if (result == nullptr) {
    result = make_unique<SavedMessagesTopic>();
    result->saved_messages_topic_id_ = saved_messages_topic_id;
  }
  return result.get();
}

void SavedMessagesManager::set_topic_last_message_id(SavedMessagesTopicId saved_messages_topic_id,
                                                     MessageId last_message_id, int32 last_message_date) {
  auto *topic = add_topic(saved_messages_topic_id);
  do_set_topic_last_message_id(topic, last_message_id, last_message_date);
  on_topic_changed(saved_messages_topic_id, topic);
}

void SavedMessagesManager::do_set_topic_last_message_id(SavedMessagesTopic *topic, MessageId last_message_id,
                                                        int32 last_message_date) {
  if (topic->last_message_id_ == last_message_id) {
    return;
  }

  CHECK(last_message_id == MessageId() || last_message_id.is_valid());
  LOG(INFO) << "Set last message in topic " << topic->saved_messages_topic_id_ << " to " << last_message_id;
  topic->last_message_id_ = last_message_id;
  topic->last_message_date_ = last_message_date;
  topic->is_changed_ = true;
}

void SavedMessagesManager::on_topic_message_deleted(SavedMessagesTopicId saved_messages_topic_id,
                                                    MessageId message_id) {
  auto *topic = get_topic(saved_messages_topic_id);
  if (topic == nullptr || topic->last_message_id_ != message_id) {
    return;
  }

  do_set_topic_last_message_id(topic, MessageId(), 0);

  on_topic_changed(saved_messages_topic_id, topic);

  get_saved_messages_topic_history(saved_messages_topic_id, MessageId(), 0, 1, Auto());
}

void SavedMessagesManager::on_topic_changed(SavedMessagesTopicId saved_messages_topic_id, SavedMessagesTopic *topic) {
  if (!topic->is_changed_) {
    return;
  }

  // TODO send updateSavedMessagesTopic
}

void SavedMessagesManager::get_pinned_saved_messages_topics(
    Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> &&promise) {
  td_->create_handler<GetPinnedSavedDialogsQuery>(std::move(promise))->send();
}

void SavedMessagesManager::get_saved_messages_topics(
    const string &offset, int32 limit, Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> &&promise) {
  int32 offset_date = std::numeric_limits<int32>::max();
  DialogId offset_dialog_id;
  MessageId offset_message_id;
  bool is_offset_valid = [&] {
    if (offset.empty()) {
      return true;
    }

    auto parts = full_split(offset, ',');
    if (parts.size() != 3) {
      return false;
    }
    auto r_offset_date = to_integer_safe<int32>(parts[0]);
    auto r_offset_dialog_id = to_integer_safe<int64>(parts[1]);
    auto r_offset_message_id = to_integer_safe<int32>(parts[2]);
    if (r_offset_date.is_error() || r_offset_date.ok() <= 0 || r_offset_message_id.is_error() ||
        r_offset_dialog_id.is_error()) {
      return false;
    }
    offset_date = r_offset_date.ok();
    offset_message_id = MessageId(ServerMessageId(r_offset_message_id.ok()));
    offset_dialog_id = DialogId(r_offset_dialog_id.ok());
    if (!offset_message_id.is_valid() || !offset_dialog_id.is_valid() ||
        DialogManager::get_input_peer_force(offset_dialog_id)->get_id() == telegram_api::inputPeerEmpty::ID) {
      return false;
    }
    return true;
  }();
  if (!is_offset_valid) {
    return promise.set_error(Status::Error(400, "Invalid offset specified"));
  }

  if (limit < 0) {
    return promise.set_error(Status::Error(400, "Limit must be non-negative"));
  }

  td_->create_handler<GetSavedDialogsQuery>(std::move(promise))
      ->send(offset_date, offset_message_id, offset_dialog_id, limit);
}

void SavedMessagesManager::on_get_saved_messages_topics(
    bool is_pinned, telegram_api::object_ptr<telegram_api::messages_SavedDialogs> &&saved_dialogs_ptr,
    Promise<td_api::object_ptr<td_api::foundSavedMessagesTopics>> &&promise) {
  CHECK(saved_dialogs_ptr != nullptr);
  int32 total_count = -1;
  vector<telegram_api::object_ptr<telegram_api::savedDialog>> dialogs;
  vector<telegram_api::object_ptr<telegram_api::Message>> messages;
  vector<telegram_api::object_ptr<telegram_api::Chat>> chats;
  vector<telegram_api::object_ptr<telegram_api::User>> users;
  bool is_last = false;
  switch (saved_dialogs_ptr->get_id()) {
    case telegram_api::messages_savedDialogsNotModified::ID:
      LOG(ERROR) << "Receive messages.savedDialogsNotModified";
      return promise.set_error(Status::Error(500, "Receive messages.savedDialogsNotModified"));
    case telegram_api::messages_savedDialogs::ID: {
      auto saved_dialogs = telegram_api::move_object_as<telegram_api::messages_savedDialogs>(saved_dialogs_ptr);
      total_count = static_cast<int32>(saved_dialogs->dialogs_.size());
      dialogs = std::move(saved_dialogs->dialogs_);
      messages = std::move(saved_dialogs->messages_);
      chats = std::move(saved_dialogs->chats_);
      users = std::move(saved_dialogs->users_);
      is_last = true;
      break;
    }
    case telegram_api::messages_savedDialogsSlice::ID: {
      auto saved_dialogs = telegram_api::move_object_as<telegram_api::messages_savedDialogsSlice>(saved_dialogs_ptr);
      total_count = saved_dialogs->count_;
      if (total_count < static_cast<int32>(saved_dialogs->dialogs_.size())) {
        LOG(ERROR) << "Receive total_count = " << total_count << ", but " << saved_dialogs->dialogs_.size()
                   << " Saved Messages topics";
        total_count = static_cast<int32>(saved_dialogs->dialogs_.size());
      }
      dialogs = std::move(saved_dialogs->dialogs_);
      messages = std::move(saved_dialogs->messages_);
      chats = std::move(saved_dialogs->chats_);
      users = std::move(saved_dialogs->users_);
      break;
    }
    default:
      UNREACHABLE();
  }
  td_->contacts_manager_->on_get_users(std::move(users), "on_get_saved_messages_topics");
  td_->contacts_manager_->on_get_chats(std::move(chats), "on_get_saved_messages_topics");

  FlatHashMap<MessageId, telegram_api::object_ptr<telegram_api::Message>, MessageIdHash> message_id_to_message;
  for (auto &message : messages) {
    auto message_id = MessageId::get_message_id(message, false);
    if (!message_id.is_valid()) {
      continue;
    }
    message_id_to_message[message_id] = std::move(message);
  }

  int32 last_message_date = 0;
  MessageId last_message_id;
  DialogId last_dialog_id;
  vector<SavedMessagesTopicId> added_saved_messages_topic_ids;
  vector<td_api::object_ptr<td_api::foundSavedMessagesTopic>> found_saved_messages_topics;
  for (auto &dialog : dialogs) {
    auto peer_dialog_id = DialogId(dialog->peer_);
    if (!peer_dialog_id.is_valid()) {
      LOG(ERROR) << "Receive " << peer_dialog_id << " in result of getSavedMessagesTopics";
      total_count--;
      continue;
    }
    SavedMessagesTopicId saved_messages_topic_id(peer_dialog_id);
    if (td::contains(added_saved_messages_topic_ids, saved_messages_topic_id)) {
      LOG(ERROR) << "Receive " << saved_messages_topic_id
                 << " twice in result of getSavedMessagesTopics with total_count = " << total_count;
      total_count--;
      continue;
    }
    added_saved_messages_topic_ids.push_back(saved_messages_topic_id);

    MessageId last_topic_message_id(ServerMessageId(dialog->top_message_));
    if (!last_topic_message_id.is_valid()) {
      // skip topics without messages
      LOG(ERROR) << "Receive " << saved_messages_topic_id << " without last message";
      total_count--;
      continue;
    }

    auto it = message_id_to_message.find(last_topic_message_id);
    if (it == message_id_to_message.end()) {
      LOG(ERROR) << "Can't find last " << last_topic_message_id << " in " << saved_messages_topic_id;
      total_count--;
      continue;
    }
    auto message_date = MessagesManager::get_message_date(it->second);
    if (message_date > 0) {
      last_message_date = message_date;
      last_message_id = last_topic_message_id;
      last_dialog_id = peer_dialog_id;
    }
    auto full_message_id = td_->messages_manager_->on_get_message(std::move(it->second), false, false, false,
                                                                  "on_get_saved_messages_topics");
    message_id_to_message.erase(it);

    if (full_message_id.get_dialog_id() != td_->dialog_manager_->get_my_dialog_id()) {
      if (full_message_id.get_dialog_id() != DialogId()) {
        LOG(ERROR) << "Can't add last " << last_message_id << " to " << saved_messages_topic_id;
      }
      total_count--;
      continue;
    }
    CHECK(full_message_id.get_message_id() == last_topic_message_id);

    auto *topic = add_topic(saved_messages_topic_id);
    if (topic->last_message_id_ == MessageId()) {
      do_set_topic_last_message_id(topic, last_topic_message_id, message_date);
    }
    on_topic_changed(saved_messages_topic_id, topic);

    found_saved_messages_topics.push_back(td_api::make_object<td_api::foundSavedMessagesTopic>(
        saved_messages_topic_id.get_saved_messages_topic_object(td_),
        td_->messages_manager_->get_message_object(full_message_id, "on_get_saved_messages_topics")));
  }

  if (is_pinned) {
    are_pinned_saved_messages_topics_inited_ = true;
    set_pinned_saved_messages_topics(std::move(added_saved_messages_topic_ids));
  }

  string next_offset;
  if (last_message_date > 0 && !is_last) {
    next_offset = PSTRING() << last_message_date << ',' << last_dialog_id.get() << ','
                            << last_message_id.get_server_message_id().get();
  }
  promise.set_value(td_api::make_object<td_api::foundSavedMessagesTopics>(
      total_count, std::move(found_saved_messages_topics), next_offset));
}

int64 SavedMessagesManager::get_next_pinned_saved_messages_topic_order() {
  current_pinned_saved_messages_topic_order_++;
  LOG(INFO) << "Assign pinned_order = " << current_pinned_saved_messages_topic_order_;
  return current_pinned_saved_messages_topic_order_;
}

bool SavedMessagesManager::set_pinned_saved_messages_topics(vector<SavedMessagesTopicId> saved_messages_topic_ids) {
  if (pinned_saved_messages_topic_ids_ == saved_messages_topic_ids) {
    return false;
  }
  LOG(INFO) << "Update pinned Saved Messages topics from " << pinned_saved_messages_topic_ids_ << " to "
            << saved_messages_topic_ids;
  FlatHashSet<SavedMessagesTopicId, SavedMessagesTopicIdHash> old_pinned_saved_messages_topic_ids;
  for (auto pinned_saved_messages_topic_id : pinned_saved_messages_topic_ids_) {
    old_pinned_saved_messages_topic_ids.insert(pinned_saved_messages_topic_id);
  }

  auto pinned_saved_messages_topic_ids = pinned_saved_messages_topic_ids_;
  std::reverse(pinned_saved_messages_topic_ids.begin(), pinned_saved_messages_topic_ids.end());
  std::reverse(saved_messages_topic_ids.begin(), saved_messages_topic_ids.end());
  auto old_it = pinned_saved_messages_topic_ids.begin();
  for (auto saved_messages_topic_id : saved_messages_topic_ids) {
    old_pinned_saved_messages_topic_ids.erase(saved_messages_topic_id);
    while (old_it < pinned_saved_messages_topic_ids.end()) {
      if (*old_it == saved_messages_topic_id) {
        break;
      }
      ++old_it;
    }
    if (old_it < pinned_saved_messages_topic_ids.end()) {
      // leave saved_messages_topic where it is
      ++old_it;
      continue;
    }
    set_saved_messages_topic_is_pinned(saved_messages_topic_id, true);
  }
  for (auto saved_messages_topic_id : old_pinned_saved_messages_topic_ids) {
    set_saved_messages_topic_is_pinned(saved_messages_topic_id, false);
  }
  return true;
}

bool SavedMessagesManager::set_saved_messages_topic_is_pinned(SavedMessagesTopicId saved_messages_topic_id,
                                                              bool is_pinned) {
  return set_saved_messages_topic_is_pinned(get_topic(saved_messages_topic_id), is_pinned);
}

bool SavedMessagesManager::set_saved_messages_topic_is_pinned(SavedMessagesTopic *topic, bool is_pinned) {
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(topic != nullptr);
  if (!are_pinned_saved_messages_topics_inited_) {
    return false;
  }
  auto saved_messages_topic_id = topic->saved_messages_topic_id_;
  if (is_pinned) {
    if (!pinned_saved_messages_topic_ids_.empty() && pinned_saved_messages_topic_ids_[0] == saved_messages_topic_id) {
      return false;
    }
    topic->pinned_order_ = get_next_pinned_saved_messages_topic_order();
    add_to_top(pinned_saved_messages_topic_ids_, pinned_saved_messages_topic_ids_.size() + 1, saved_messages_topic_id);
  } else {
    if (topic->pinned_order_ == 0 || !td::remove(pinned_saved_messages_topic_ids_, saved_messages_topic_id)) {
      return false;
    }
    topic->pinned_order_ = 0;
  }

  LOG(INFO) << "Set " << saved_messages_topic_id << " pinned order to " << topic->pinned_order_;
  topic->is_changed_ = true;
  on_topic_changed(saved_messages_topic_id, topic);
  return true;
}

void SavedMessagesManager::get_saved_messages_topic_history(SavedMessagesTopicId saved_messages_topic_id,
                                                            MessageId from_message_id, int32 offset, int32 limit,
                                                            Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }
  if (limit > MAX_GET_HISTORY) {
    limit = MAX_GET_HISTORY;
  }
  if (offset > 0) {
    return promise.set_error(Status::Error(400, "Parameter offset must be non-positive"));
  }
  if (offset <= -MAX_GET_HISTORY) {
    return promise.set_error(Status::Error(400, "Parameter offset must be greater than -100"));
  }
  if (offset < -limit) {
    return promise.set_error(Status::Error(400, "Parameter offset must be greater than or equal to -limit"));
  }

  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_status(td_));

  if (from_message_id == MessageId() || from_message_id.get() > MessageId::max().get()) {
    from_message_id = MessageId::max();
    limit += offset;
    offset = 0;
  }
  if (!from_message_id.is_valid() || !from_message_id.is_server()) {
    return promise.set_error(Status::Error(400, "Invalid value of parameter from_message_id specified"));
  }

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), saved_messages_topic_id, from_message_id,
                                               promise = std::move(promise)](Result<MessagesInfo> &&r_info) mutable {
    send_closure(actor_id, &SavedMessagesManager::on_get_saved_messages_topic_history, saved_messages_topic_id,
                 from_message_id, std::move(r_info), std::move(promise));
  });
  td_->create_handler<GetSavedHistoryQuery>(std::move(query_promise))
      ->send(saved_messages_topic_id, from_message_id, offset, limit);
}

void SavedMessagesManager::on_get_saved_messages_topic_history(
    SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id, Result<MessagesInfo> &&r_info,
    Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  G()->ignore_result_if_closing(r_info);
  if (r_info.is_error()) {
    return promise.set_error(r_info.move_as_error());
  }
  auto info = r_info.move_as_ok();

  auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
  vector<td_api::object_ptr<td_api::message>> messages;
  MessageId last_message_id;
  int32 last_message_date = 0;
  for (auto &message : info.messages) {
    auto message_date = MessagesManager::get_message_date(message);
    auto full_message_id = td_->messages_manager_->on_get_message(std::move(message), false, false, false,
                                                                  "on_get_saved_messages_topic_history");
    auto dialog_id = full_message_id.get_dialog_id();
    if (dialog_id == DialogId()) {
      continue;
    }
    if (dialog_id != my_dialog_id) {
      LOG(ERROR) << "Receive " << full_message_id << " in history of " << saved_messages_topic_id;
      continue;
    }
    if (!last_message_id.is_valid()) {
      last_message_id = full_message_id.get_message_id();
      last_message_date = message_date;
    }
    messages.push_back(
        td_->messages_manager_->get_message_object(full_message_id, "on_get_saved_messages_topic_history"));
  }
  if (from_message_id == MessageId::max()) {
    auto *topic = add_topic(saved_messages_topic_id);
    if (info.messages.empty()) {
      do_set_topic_last_message_id(topic, MessageId(), 0);
    } else {
      if (last_message_id.is_valid() && topic->last_message_id_ == MessageId()) {
        do_set_topic_last_message_id(topic, last_message_id, last_message_date);
      }
    }
    on_topic_changed(saved_messages_topic_id, topic);
  }
  promise.set_value(td_api::make_object<td_api::messages>(info.total_count, std::move(messages)));
}

void SavedMessagesManager::delete_saved_messages_topic_history(SavedMessagesTopicId saved_messages_topic_id,
                                                               Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_status(td_));

  MessagesManager::AffectedHistoryQuery query = [td = td_, saved_messages_topic_id](
                                                    DialogId, Promise<AffectedHistory> &&query_promise) {
    td->create_handler<DeleteSavedHistoryQuery>(std::move(query_promise))->send(saved_messages_topic_id);
  };
  auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
  td_->messages_manager_->run_affected_history_query_until_complete(my_dialog_id, std::move(query), true,
                                                                    std::move(promise));
}

void SavedMessagesManager::get_saved_messages_topic_message_by_date(
    SavedMessagesTopicId saved_messages_topic_id, int32 date, Promise<td_api::object_ptr<td_api::message>> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_status(td_));

  if (date <= 0) {
    date = 1;
  }

  td_->create_handler<GetSavedMessageByDateQuery>(std::move(promise))->send(saved_messages_topic_id, date);
}

void SavedMessagesManager::delete_saved_messages_topic_messages_by_date(SavedMessagesTopicId saved_messages_topic_id,
                                                                        int32 min_date, int32 max_date,
                                                                        Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_status(td_));

  TRY_STATUS_PROMISE(promise, MessagesManager::fix_delete_message_min_max_dates(min_date, max_date));
  if (max_date == 0) {
    return promise.set_value(Unit());
  }

  MessagesManager::AffectedHistoryQuery query = [td = td_, saved_messages_topic_id, min_date, max_date](
                                                    DialogId, Promise<AffectedHistory> &&query_promise) {
    td->create_handler<DeleteSavedMessagesByDateQuery>(std::move(query_promise))
        ->send(saved_messages_topic_id, min_date, max_date);
  };
  auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
  td_->messages_manager_->run_affected_history_query_until_complete(my_dialog_id, std::move(query), true,
                                                                    std::move(promise));
}

int32 SavedMessagesManager::get_pinned_saved_messages_topic_limit() const {
  return clamp(narrow_cast<int32>(td_->option_manager_->get_option_integer("pinned_saved_messages_topic_count_max")), 0,
               1000);
}

void SavedMessagesManager::toggle_saved_messages_topic_is_pinned(SavedMessagesTopicId saved_messages_topic_id,
                                                                 bool is_pinned, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_status(td_));
  if (!are_pinned_saved_messages_topics_inited_) {
    return promise.set_error(Status::Error(400, "Pinned Saved Messages topics must be loaded first"));
  }
  if (get_topic(saved_messages_topic_id) == nullptr) {
    return promise.set_error(Status::Error(400, "Can't find Saved Messages topic"));
  }
  if (is_pinned && !td::contains(pinned_saved_messages_topic_ids_, saved_messages_topic_id) &&
      static_cast<size_t>(get_pinned_saved_messages_topic_limit()) <= pinned_saved_messages_topic_ids_.size()) {
    return promise.set_error(Status::Error(400, "The maximum number of pinned chats exceeded"));
  }
  if (!set_saved_messages_topic_is_pinned(saved_messages_topic_id, is_pinned)) {
    return promise.set_value(Unit());
  }
  td_->create_handler<ToggleSavedDialogPinQuery>(std::move(promise))->send(saved_messages_topic_id, is_pinned);
}

void SavedMessagesManager::set_pinned_saved_messages_topics(vector<SavedMessagesTopicId> saved_messages_topic_ids,
                                                            Promise<Unit> &&promise) {
  for (const auto &saved_messages_topic_id : saved_messages_topic_ids) {
    TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_status(td_));
    if (get_topic(saved_messages_topic_id) == nullptr) {
      return promise.set_error(Status::Error(400, "Can't find Saved Messages topic"));
    }
  }
  if (!are_pinned_saved_messages_topics_inited_) {
    return promise.set_error(Status::Error(400, "Pinned Saved Messages topics must be loaded first"));
  }
  if (static_cast<size_t>(get_pinned_saved_messages_topic_limit()) < saved_messages_topic_ids.size()) {
    return promise.set_error(Status::Error(400, "The maximum number of pinned chats exceeded"));
  }
  if (!set_pinned_saved_messages_topics(saved_messages_topic_ids)) {
    return promise.set_value(Unit());
  }
  td_->create_handler<ReorderPinnedSavedDialogsQuery>(std::move(promise))->send(std::move(saved_messages_topic_ids));
}

void SavedMessagesManager::on_update_pinned_saved_messages_topics() {
  if (td_->auth_manager_->is_bot()) {
    // just in case
    return;
  }

  send_closure(G()->td(), &Td::send_update, td_api::make_object<td_api::updatePinnedSavedMessagesTopics>());
}

}  // namespace td
