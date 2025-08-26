//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/SavedMessagesManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AffectedHistory.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogFilterManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DraftMessage.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageQueryManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"

#include "td/actor/SleepActor.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"

#include <algorithm>

namespace td {

class GetPinnedSavedDialogsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  uint32 generation_;
  int32 limit_;

 public:
  explicit GetPinnedSavedDialogsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(uint32 generation, int32 limit) {
    generation_ = generation;
    limit_ = limit;
    send_query(G()->net_query_creator().create(telegram_api::messages_getPinnedSavedDialogs()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getPinnedSavedDialogs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetPinnedSavedDialogsQuery: " << to_string(result);
    td_->saved_messages_manager_->on_get_saved_messages_topics(DialogId(), generation_, SavedMessagesTopicId(), true,
                                                               limit_, std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedDialogsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;
  uint32 generation_;
  int32 limit_;

 public:
  explicit GetSavedDialogsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, uint32 generation, int32 offset_date, MessageId offset_message_id,
            DialogId offset_dialog_id, int32 limit) {
    dialog_id_ = dialog_id;
    generation_ = generation;
    limit_ = limit;
    auto offset_input_peer = DialogManager::get_input_peer_force(offset_dialog_id);
    CHECK(offset_input_peer != nullptr);

    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> parent_input_peer;
    if (dialog_id != DialogId()) {
      parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
      CHECK(parent_input_peer != nullptr);
      flags |= telegram_api::messages_getSavedDialogs::PARENT_PEER_MASK;
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_getSavedDialogs(
        flags, true, std::move(parent_input_peer), offset_date, offset_message_id.get_server_message_id().get(),
        std::move(offset_input_peer), limit, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedDialogs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedDialogsQuery: " << to_string(result);
    td_->saved_messages_manager_->on_get_saved_messages_topics(dialog_id_, generation_, SavedMessagesTopicId(), false,
                                                               limit_, std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedDialogsByIdQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;
  uint32 generation_;
  SavedMessagesTopicId saved_messages_topic_id_;

 public:
  explicit GetSavedDialogsByIdQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, uint32 generation, SavedMessagesTopicId saved_messages_topic_id) {
    dialog_id_ = dialog_id;
    generation_ = generation;
    saved_messages_topic_id_ = saved_messages_topic_id;

    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);
    vector<telegram_api::object_ptr<telegram_api::InputPeer>> saved_input_peers;
    saved_input_peers.push_back(std::move(saved_input_peer));

    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> parent_input_peer;
    if (dialog_id.get_type() == DialogType::Channel) {
      flags |= telegram_api::messages_getSavedDialogsByID::PARENT_PEER_MASK;
      parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
      if (parent_input_peer == nullptr) {
        return on_error(Status::Error(400, "Can't access the chat"));
      }
    }

    send_query(G()->net_query_creator().create(
        telegram_api::messages_getSavedDialogsByID(flags, std::move(parent_input_peer), std::move(saved_input_peers)),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedDialogsByID>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetSavedDialogsByIdQuery: " << to_string(result);
    td_->saved_messages_manager_->on_get_saved_messages_topics(dialog_id_, generation_, saved_messages_topic_id_, false,
                                                               -1, std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetSavedHistoryQuery final : public Td::ResultHandler {
  Promise<MessagesInfo> promise_;
  DialogId dialog_id_;

 public:
  explicit GetSavedHistoryQuery(Promise<MessagesInfo> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id, int32 offset,
            int32 limit) {
    dialog_id_ = dialog_id;
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> parent_input_peer;
    if (dialog_id.get_type() == DialogType::Channel) {
      flags |= telegram_api::messages_getSavedHistory::PARENT_PEER_MASK;
      parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
      if (parent_input_peer == nullptr) {
        return on_error(Status::Error(400, "Can't access the chat"));
      }
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_getSavedHistory(
        flags, std::move(parent_input_peer), std::move(saved_input_peer), from_message_id.get_server_message_id().get(),
        0, offset, limit, 0, 0, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, dialog_id_, result_ptr.move_as_ok(), "GetSavedHistoryQuery");
    LOG_IF(ERROR, info.is_channel_messages != (dialog_id_.get_type() == DialogType::Channel))
        << "Receive channel messages in GetSavedHistoryQuery";
    td_->messages_manager_->get_channel_difference_if_needed(dialog_id_, std::move(info), std::move(promise_),
                                                             "GetSavedHistoryQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetSavedHistoryQuery");
    promise_.set_error(std::move(status));
  }
};

class GetSavedMessageByDateQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::message>> promise_;
  DialogId dialog_id_;
  int32 date_ = 0;

 public:
  explicit GetSavedMessageByDateQuery(Promise<td_api::object_ptr<td_api::message>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, int32 date) {
    dialog_id_ = dialog_id;
    date_ = date;
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> parent_input_peer;
    if (dialog_id.get_type() == DialogType::Channel) {
      flags |= telegram_api::messages_getSavedHistory::PARENT_PEER_MASK;
      parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
      if (parent_input_peer == nullptr) {
        return on_error(Status::Error(400, "Can't access the chat"));
      }
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_getSavedHistory(
        flags, std::move(parent_input_peer), std::move(saved_input_peer), 0, date, -3, 5, 0, 0, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, dialog_id_, result_ptr.move_as_ok(), "GetSavedMessageByDateQuery");
    // TODO td_->messages_manager_->get_channel_difference_if_needed(dialog_id_, std::move(info), std::move(promise_), "GetSavedMessageByDateQuery");
    for (auto &message : info.messages) {
      auto message_date = MessagesManager::get_message_date(message);
      if (message_date != 0 && message_date <= date_) {
        auto message_full_id = td_->messages_manager_->on_get_message(dialog_id_, std::move(message), false, false,
                                                                      false, "GetSavedMessageByDateQuery");
        if (message_full_id != MessageFullId()) {
          // TODO check message topic_id
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

  void send(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) {
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> parent_input_peer;
    if (dialog_id.get_type() == DialogType::Channel) {
      flags |= telegram_api::messages_deleteSavedHistory::PARENT_PEER_MASK;
      parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
      if (parent_input_peer == nullptr) {
        return on_error(Status::Error(400, "Can't access the chat"));
      }
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_deleteSavedHistory(
        flags, std::move(parent_input_peer), std::move(saved_input_peer), std::numeric_limits<int32>::max(), 0, 0)));
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

  void send(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, int32 min_date, int32 max_date) {
    auto saved_input_peer = saved_messages_topic_id.get_input_peer(td_);
    CHECK(saved_input_peer != nullptr);

    int32 flags = telegram_api::messages_deleteSavedHistory::MIN_DATE_MASK |
                  telegram_api::messages_deleteSavedHistory::MAX_DATE_MASK;
    telegram_api::object_ptr<telegram_api::InputPeer> parent_input_peer;
    if (dialog_id.get_type() == DialogType::Channel) {
      flags |= telegram_api::messages_deleteSavedHistory::PARENT_PEER_MASK;
      parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
      if (parent_input_peer == nullptr) {
        return on_error(Status::Error(400, "Can't access the chat"));
      }
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_deleteSavedHistory(
        flags, std::move(parent_input_peer), std::move(saved_input_peer), 0, min_date, max_date)));
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

    send_query(G()->net_query_creator().create(
        telegram_api::messages_toggleSavedDialogPin(0, is_pinned, std::move(saved_input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_toggleSavedDialogPin>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->saved_messages_manager_->reload_pinned_saved_messages_topics();
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
    send_query(
        G()->net_query_creator().create(telegram_api::messages_reorderPinnedSavedDialogs(0, true, std::move(order))));
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
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->saved_messages_manager_->reload_pinned_saved_messages_topics();
    promise_.set_error(std::move(status));
  }
};

class ReadSavedHistoryQuery final : public Td::ResultHandler {
 public:
  void send(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, MessageId max_message_id) {
    auto parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    auto input_peer = saved_messages_topic_id.get_input_peer(td_);
    if (parent_input_peer == nullptr || input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_readSavedHistory(std::move(parent_input_peer), std::move(input_peer),
                                                max_message_id.get_server_message_id().get()),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_readSavedHistory>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }
  }

  void on_error(Status status) final {
    // two dialogs are involved
    // td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ReadSavedHistoryQuery");
  }
};

class GetMonoforumPaidMessageRevenueQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::starCount>> promise_;

 public:
  explicit GetMonoforumPaidMessageRevenueQuery(Promise<td_api::object_ptr<td_api::starCount>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, telegram_api::object_ptr<telegram_api::InputUser> &&input_user) {
    int32 flags = telegram_api::account_getPaidMessagesRevenue::PARENT_PEER_MASK;
    auto parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    CHECK(parent_input_peer != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::account_getPaidMessagesRevenue(flags, std::move(parent_input_peer), std::move(input_user))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_getPaidMessagesRevenue>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(DEBUG) << "Receive result for GetMonoforumPaidMessageRevenueQuery: " << to_string(ptr);
    promise_.set_value(td_api::make_object<td_api::starCount>(StarManager::get_star_count(ptr->stars_amount_)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class AddMonoforumNoPaidMessageExceptionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit AddMonoforumNoPaidMessageExceptionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, telegram_api::object_ptr<telegram_api::InputUser> &&input_user, bool require_payment,
            bool refund_charged) {
    int32 flags = telegram_api::account_toggleNoPaidMessagesException::PARENT_PEER_MASK;
    auto parent_input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    CHECK(parent_input_peer != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::account_toggleNoPaidMessagesException(
        flags, refund_charged, require_payment, std::move(parent_input_peer), std::move(input_user))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::account_toggleNoPaidMessagesException>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetMessageAuthorQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::user>> promise_;
  ChannelId channel_id_;

 public:
  explicit GetMessageAuthorQuery(Promise<td_api::object_ptr<td_api::user>> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, MessageId message_id) {
    channel_id_ = channel_id;
    auto input_channel = td_->chat_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::channels_getMessageAuthor(std::move(input_channel), message_id.get_server_message_id().get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getMessageAuthor>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetMessageAuthorQuery: " << to_string(ptr);
    auto user_id = UserManager::get_user_id(ptr);
    td_->user_manager_->on_get_user(std::move(ptr), "GetMessageAuthorQuery");
    promise_.set_value(td_->user_manager_->get_user_object(user_id));
  }

  void on_error(Status status) final {
    td_->chat_manager_->on_get_channel_error(channel_id_, status, "GetMessageAuthorQuery");
    promise_.set_error(std::move(status));
  }
};

SavedMessagesManager::SavedMessagesManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void SavedMessagesManager::tear_down() {
  parent_.reset();
}

bool SavedMessagesManager::have_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) const {
  return get_topic(dialog_id, saved_messages_topic_id) != nullptr;
}

SavedMessagesTopicId SavedMessagesManager::get_topic_id(DialogId dialog_id, int64 topic_id) const {
  if (topic_id == 0) {
    return SavedMessagesTopicId();
  }
  auto saved_messages_topic_id = SavedMessagesTopicId(DialogId(topic_id));
  if (td_->auth_manager_->is_bot() && saved_messages_topic_id.is_valid_in(td_, dialog_id).is_ok()) {
    return saved_messages_topic_id;
  }
  if (dialog_id == DialogId() &&
      saved_messages_topic_id.is_valid_in(td_, td_->dialog_manager_->get_my_dialog_id()).is_ok()) {
    return saved_messages_topic_id;
  }
  if (get_topic(dialog_id, saved_messages_topic_id) == nullptr) {
    return SavedMessagesTopicId(DialogId(std::numeric_limits<int64>::max()));  // an invalid topic identifier
  }
  return saved_messages_topic_id;
}

vector<SavedMessagesTopicId> SavedMessagesManager::get_topic_ids(DialogId dialog_id,
                                                                 const vector<int64> &topic_ids) const {
  return transform(topic_ids, [this, dialog_id](int64 topic_id) { return get_topic_id(dialog_id, topic_id); });
}

int64 SavedMessagesManager::get_saved_messages_topic_id_object(DialogId dialog_id,
                                                               SavedMessagesTopicId saved_messages_topic_id) {
  if (saved_messages_topic_id == SavedMessagesTopicId()) {
    return 0;
  }
  auto *topic_list = add_topic_list(dialog_id);
  if (topic_list == nullptr) {
    if (td_->auth_manager_->is_bot()) {
      return saved_messages_topic_id.get_unique_id();
    }
    return 0;
  }

  add_topic(topic_list, saved_messages_topic_id, false);

  return saved_messages_topic_id.get_unique_id();
}

bool SavedMessagesManager::is_last_topic_message(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                 MessageId message_id) const {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return false;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  return topic != nullptr && topic->last_message_id_ == message_id;
}

SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::get_topic(
    DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) {
  return const_cast<SavedMessagesTopic *>(
      const_cast<const SavedMessagesManager *>(this)->get_topic(dialog_id, saved_messages_topic_id));
}

const SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::get_topic(
    DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) const {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return nullptr;
  }
  return get_topic(topic_list, saved_messages_topic_id);
}

SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::get_topic(
    TopicList *topic_list, SavedMessagesTopicId saved_messages_topic_id) {
  return const_cast<SavedMessagesTopic *>(
      get_topic(const_cast<const TopicList *>(topic_list), saved_messages_topic_id));
}

const SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::get_topic(
    const TopicList *topic_list, SavedMessagesTopicId saved_messages_topic_id) {
  CHECK(topic_list != nullptr);
  auto it = topic_list->topics_.find(saved_messages_topic_id);
  if (it == topic_list->topics_.end()) {
    return nullptr;
  }
  return it->second.get();
}

SavedMessagesManager::SavedMessagesTopic *SavedMessagesManager::add_topic(TopicList *topic_list,
                                                                          SavedMessagesTopicId saved_messages_topic_id,
                                                                          bool from_server) {
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(topic_list != nullptr);
  CHECK(saved_messages_topic_id.is_valid());
  auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
  bool is_saved_messages = topic_list->dialog_id_ == DialogId();
  auto &result = topic_list->topics_[saved_messages_topic_id];
  if (result == nullptr) {
    result = make_unique<SavedMessagesTopic>();
    if (!is_saved_messages) {
      result->dialog_id_ = topic_list->dialog_id_;
    }
    result->saved_messages_topic_id_ = saved_messages_topic_id;
    if (is_saved_messages && saved_messages_topic_id == SavedMessagesTopicId(my_dialog_id)) {
      auto draft_message_object = td_->messages_manager_->get_my_dialog_draft_message_object();
      if (draft_message_object != nullptr) {
        result->draft_message_date_ = draft_message_object->date_;
      }
    }
    send_update_saved_messages_topic(topic_list, result.get(), "add_topic");
  }
  if (from_server) {
    result->is_received_from_server_ = true;
  } else if (!result->is_received_from_server_ && !is_saved_messages) {
    reload_monoforum_topic(result->dialog_id_, saved_messages_topic_id, Auto());
  }
  return result.get();
}

void SavedMessagesManager::do_set_topic_last_message_id(SavedMessagesTopic *topic, MessageId last_message_id,
                                                        int32 last_message_date) {
  if (td_->auth_manager_->is_bot() || topic->last_message_id_ == last_message_id) {
    return;
  }

  CHECK(last_message_id == MessageId() || last_message_id.is_valid());
  LOG(INFO) << "Set last message in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_ << " to "
            << last_message_id;
  topic->last_message_id_ = last_message_id;
  topic->last_message_date_ = last_message_date;
  topic->is_changed_ = true;

  if (last_message_id == MessageId() && last_message_date != 0) {
    do_get_topic_history(get_topic_list(topic->dialog_id_), nullptr /*force request*/,
                         topic->dialog_id_ == DialogId() ? td_->dialog_manager_->get_my_dialog_id() : topic->dialog_id_,
                         topic->saved_messages_topic_id_, MessageId::max(), 0, 1, 2, Auto());
  }
}

void SavedMessagesManager::do_set_topic_read_inbox_max_message_id(SavedMessagesTopic *topic,
                                                                  MessageId read_inbox_max_message_id,
                                                                  int32 unread_count, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (unread_count < 0) {
    LOG(ERROR) << "Receive " << unread_count << " unread messages in " << topic->saved_messages_topic_id_ << " of "
               << topic->dialog_id_ << " from " << source;
    unread_count = 0;
  }
  if (!read_inbox_max_message_id.is_valid() && read_inbox_max_message_id != MessageId()) {
    LOG(ERROR) << "Receive " << read_inbox_max_message_id << " last read message in " << topic->saved_messages_topic_id_
               << " of " << topic->dialog_id_ << " from " << source;
    read_inbox_max_message_id = MessageId();
  }
  if (topic->last_message_id_.is_valid() && read_inbox_max_message_id >= topic->last_message_id_) {
    unread_count = 0;
  }
  if (topic->read_inbox_max_message_id_ == read_inbox_max_message_id && topic->unread_count_ == unread_count) {
    return;
  }
  if (read_inbox_max_message_id < topic->read_inbox_max_message_id_) {
    return;
  }

  LOG(INFO) << "Set read inbox max message in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_
            << " to " << read_inbox_max_message_id << " with " << unread_count << " unread messages from " << source;
  topic->read_inbox_max_message_id_ = read_inbox_max_message_id;
  topic->unread_count_ = unread_count;
  topic->is_changed_ = true;
}

void SavedMessagesManager::do_set_topic_read_outbox_max_message_id(SavedMessagesTopic *topic,
                                                                   MessageId read_outbox_max_message_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!read_outbox_max_message_id.is_valid() && read_outbox_max_message_id != MessageId()) {
    LOG(ERROR) << "Receive " << read_outbox_max_message_id << " last read message in "
               << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_;
    read_outbox_max_message_id = MessageId();
  }
  if (read_outbox_max_message_id <= topic->read_outbox_max_message_id_) {
    return;
  }

  LOG(INFO) << "Set read outbox max message in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_
            << " to " << read_outbox_max_message_id;
  topic->read_outbox_max_message_id_ = read_outbox_max_message_id;
  topic->is_changed_ = true;
}

void SavedMessagesManager::do_set_topic_is_marked_as_unread(SavedMessagesTopic *topic, bool is_marked_as_unread) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (topic->is_marked_as_unread_ == is_marked_as_unread) {
    return;
  }

  LOG(INFO) << "Set is_marked_as_unread in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_ << " to "
            << is_marked_as_unread;
  topic->is_marked_as_unread_ = is_marked_as_unread;
  topic->is_changed_ = true;
}

void SavedMessagesManager::do_set_topic_nopaid_messages_exception(SavedMessagesTopic *topic,
                                                                  bool nopaid_messages_exception) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (topic->nopaid_messages_exception_ == nopaid_messages_exception) {
    return;
  }

  LOG(INFO) << "Set can_send_unpaid_messages in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_
            << " to " << nopaid_messages_exception;
  topic->nopaid_messages_exception_ = nopaid_messages_exception;
  topic->is_changed_ = true;
}

void SavedMessagesManager::do_set_topic_unread_reaction_count(SavedMessagesTopic *topic, int32 unread_reaction_count) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (unread_reaction_count < 0) {
    // can happen after local read of all reactions in the topic or chat
    LOG(INFO) << "Receive " << unread_reaction_count << " unread reactions in " << topic->saved_messages_topic_id_
              << " of " << topic->dialog_id_;
    unread_reaction_count = 0;
  }
  if (topic->unread_reaction_count_ == unread_reaction_count) {
    return;
  }

  LOG(INFO) << "Set unread reaction count in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_
            << " to " << unread_reaction_count;
  topic->unread_reaction_count_ = unread_reaction_count;
  topic->is_changed_ = true;
}

void SavedMessagesManager::do_set_topic_draft_message(SavedMessagesTopic *topic,
                                                      unique_ptr<DraftMessage> &&draft_message, bool from_update) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!need_update_draft_message(topic->draft_message_, draft_message, from_update)) {
    return;
  }

  topic->draft_message_ = std::move(draft_message);
  topic->is_changed_ = true;
}

void SavedMessagesManager::on_topic_message_added(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                  MessageId message_id, int32 message_date, const bool from_update,
                                                  const bool need_update, const bool is_new, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  CHECK(message_id.is_valid());

  LOG(INFO) << "Add " << message_id << " to " << saved_messages_topic_id << " of " << dialog_id
            << " with from_update = " << from_update << ", need_update = " << need_update << " and is_new = " << is_new;
  auto *topic_list = add_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = add_topic(topic_list, saved_messages_topic_id, false);

  topic->ordered_messages_.insert(message_id, from_update, topic->last_message_id_, source);

  if (message_id > topic->last_message_id_) {
    if (from_update && is_new) {
      CHECK(topic->ordered_messages_.get_last_message_id() == message_id);
      do_set_topic_last_message_id(topic, message_id, message_date);
    } else {
      do_set_topic_last_message_id(topic, MessageId(), message_date);
    }
  }
  if (topic->dialog_id_.is_valid() && need_update && message_id > topic->read_inbox_max_message_id_ &&
      td_->messages_manager_->get_is_counted_as_unread(dialog_id, MessageType::Server)(message_id)) {
    // must be called after update of last_message_id
    do_set_topic_read_inbox_max_message_id(topic, topic->read_inbox_max_message_id_, topic->unread_count_ + 1,
                                           "on_topic_message_added");
  }

  if (message_id.is_server()) {
    if (from_update && topic->is_server_message_count_inited_) {
      topic->server_message_count_++;
      on_topic_message_count_changed(topic, "on_topic_message_added");
    }
  } else {
    topic->local_message_count_++;
    on_topic_message_count_changed(topic, "on_topic_message_added");
  }

  on_topic_changed(topic_list, topic, "on_topic_message_added");
}

void SavedMessagesManager::on_topic_message_updated(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                    MessageId message_id) {
  if (td_->auth_manager_->is_bot() || message_id.is_scheduled()) {
    return;
  }
  CHECK(message_id.is_valid());

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr || topic->last_message_id_ != message_id) {
    return;
  }

  send_update_saved_messages_topic(topic_list, topic, "on_topic_message_updated");
}

void SavedMessagesManager::on_topic_message_deleted(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                    MessageId message_id, bool only_from_memory, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  CHECK(message_id.is_valid());

  LOG(INFO) << "Delete " << message_id << " from " << saved_messages_topic_id << " of " << dialog_id << " from "
            << source;
  auto *topic_list = get_topic_list(dialog_id);
  CHECK(topic_list != nullptr);
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  CHECK(topic != nullptr);

  if (message_id == topic->last_message_id_) {
    CHECK(!only_from_memory);

    MessageId new_last_message_id;
    int32 new_last_message_date;
    auto it = topic->ordered_messages_.get_const_iterator(message_id);
    CHECK(*it != nullptr);
    CHECK((*it)->get_message_id() == message_id);
    --it;
    if (*it != nullptr) {
      new_last_message_id = (*it)->get_message_id();
      new_last_message_date = td_->messages_manager_->get_get_message_date(dialog_id)(new_last_message_id);
    } else {
      new_last_message_date = topic->last_message_date_;
    }
    do_set_topic_last_message_id(topic, new_last_message_id, new_last_message_date);
  }
  topic->ordered_messages_.erase(message_id, only_from_memory, source);
  if (topic->last_message_id_ != MessageId()) {
    CHECK(topic->ordered_messages_.get_last_message_id() == topic->last_message_id_);
  }
  if (!only_from_memory) {
    if (message_id.is_server()) {
      if (topic->is_server_message_count_inited_) {
        if (topic->server_message_count_ > 0) {
          topic->server_message_count_--;
          on_topic_message_count_changed(topic, "on_topic_message_deleted");
        } else {
          LOG(ERROR) << "Server message count become negative in " << saved_messages_topic_id << " of " << dialog_id
                     << " after deletion of " << message_id << " from " << source;
        }
      }
    } else {
      CHECK(topic->local_message_count_ > 0);
      topic->local_message_count_--;
      on_topic_message_count_changed(topic, "on_topic_message_deleted");
    }

    if (message_id > topic->read_inbox_max_message_id_ && topic->read_inbox_max_message_id_.is_valid() &&
        td_->messages_manager_->get_is_counted_as_unread(dialog_id, MessageType::Server)(message_id)) {
      do_set_topic_read_inbox_max_message_id(topic, topic->read_inbox_max_message_id_, topic->unread_count_ - 1,
                                             "on_topic_message_deleted");
    }
  }

  on_topic_changed(topic_list, topic, "on_topic_message_deleted");
}

void SavedMessagesManager::on_all_dialog_messages_deleted(DialogId dialog_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }

  fail_promises(topic_list->load_pinned_queries_, Status::Error(400, "Topic list was cleared"));
  fail_promises(topic_list->load_queries_, Status::Error(400, "Topic list was cleared"));
  for (auto &it : topic_list->get_topic_queries_) {
    fail_promises(it.second, Status::Error(400, "Topic list was cleared"));
  }
  for (auto &it : topic_list->topics_) {
    auto *topic = it.second.get();
    do_set_topic_last_message_id(topic, MessageId(), 0);
    do_set_topic_read_inbox_max_message_id(topic, topic->read_inbox_max_message_id_, 0,
                                           "on_all_dialog_messages_deleted");
    do_set_topic_is_marked_as_unread(topic, false);
    do_set_topic_unread_reaction_count(topic, 0);
    // do_set_topic_reply_markup(topic, MessageId());
    do_set_topic_draft_message(topic, nullptr, false);
    topic->pinned_order_ = 0;
    on_topic_changed(topic_list, topic, "on_all_dialog_messages_deleted");
  }

  if (topic_list->dialog_id_ == DialogId()) {
    topic_list->sent_total_count_ = 0;
    send_closure(G()->td(), &Td::send_update, get_update_saved_messages_topic_count_object());

    Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), topic_list->ordered_topics_,
                                                topic_list->topics_);
    topic_list_ = TopicList();
    topic_list_.generation_ = ++current_topic_list_generation_;
  } else {
    auto it = monoforum_topic_lists_.find(dialog_id);
    CHECK(it != monoforum_topic_lists_.end());
    Scheduler::instance()->destroy_on_scheduler_unique_ptr(G()->get_gc_scheduler_id(), it->second);
    monoforum_topic_lists_.erase(it);
  }
}

void SavedMessagesManager::on_topic_draft_message_updated(DialogId dialog_id,
                                                          SavedMessagesTopicId saved_messages_topic_id,
                                                          int32 draft_message_date) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }

  LOG(INFO) << "Set draft message date in " << topic->saved_messages_topic_id_ << " to " << draft_message_date;
  topic->draft_message_date_ = draft_message_date;
  topic->is_changed_ = true;

  on_topic_changed(topic_list, topic, "on_topic_draft_message_updated");
}

void SavedMessagesManager::clear_monoforum_topic_draft_by_sent_message(DialogId dialog_id,
                                                                       SavedMessagesTopicId saved_messages_topic_id,
                                                                       bool message_clear_draft,
                                                                       MessageContentType message_content_type) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }

  LOG(INFO) << "Clear draft in " << saved_messages_topic_id << " of " << dialog_id << " by sent message";
  if (!message_clear_draft) {
    const auto *draft_message = topic->draft_message_.get();
    if (draft_message == nullptr || !draft_message->need_clear_local(message_content_type)) {
      return;
    }
  }
  do_set_topic_draft_message(topic, nullptr, false);
  on_topic_changed(topic_list, topic, "clear_monoforum_topic_draft_by_sent_message");
}

void SavedMessagesManager::repair_topic_unread_count(const SavedMessagesTopic *topic) {
  if (td_->auth_manager_->is_bot() ||
      !td_->dialog_manager_->have_input_peer(topic->dialog_id_, false, AccessRights::Read)) {
    return;
  }
  // if (pending_read_history_timeout_.has_timeout(dialog_id.get())) {
  //   return;  // postpone until read history request is sent
  // }

  LOG(INFO) << "Repair unread count in " << topic->saved_messages_topic_id_ << " of " << topic->dialog_id_;
  create_actor<SleepActor>("RepairTopicUnreadCountSleepActor", 0.05,
                           PromiseCreator::lambda([actor_id = actor_id(this), dialog_id = topic->dialog_id_,
                                                   saved_messages_topic_id = topic->saved_messages_topic_id_](Unit) {
                             send_closure(actor_id, &SavedMessagesManager::reload_monoforum_topic, dialog_id,
                                          saved_messages_topic_id, Auto());
                           }))
      .release();
}

void SavedMessagesManager::read_topic_messages(SavedMessagesTopic *topic, MessageId read_inbox_max_message_id,
                                               int32 hint_unread_count) {
  auto dialog_id = topic->dialog_id_;
  CHECK(dialog_id != DialogId());
  read_inbox_max_message_id =
      max(read_inbox_max_message_id, td_->messages_manager_->get_dialog_last_read_inbox_message_id(dialog_id));
  auto unread_count = topic->ordered_messages_.calc_new_unread_count(
      read_inbox_max_message_id, topic->read_inbox_max_message_id_, topic->unread_count_, topic->last_message_id_,
      td_->messages_manager_->get_is_counted_as_unread(dialog_id, MessageType::Server), hint_unread_count);
  if (unread_count < 0) {
    unread_count = topic->unread_count_;
    if (td_->dialog_manager_->have_input_peer(dialog_id, false, AccessRights::Read)) {
      topic->need_repair_unread_count_ = true;
      repair_topic_unread_count(topic);
    }
  }
  do_set_topic_read_inbox_max_message_id(topic, read_inbox_max_message_id, unread_count, "read_topic_messages");

  // on_topic_changed must be called by the caller
}

void SavedMessagesManager::read_monoforum_topic_messages(DialogId dialog_id,
                                                         SavedMessagesTopicId saved_messages_topic_id,
                                                         MessageId read_inbox_max_message_id) {
  CHECK(!td_->auth_manager_->is_bot());
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  if (topic->dialog_id_ != dialog_id) {
    return;
  }

  read_topic_messages(topic, read_inbox_max_message_id, -1);

  if (topic->is_changed_) {
    td_->create_handler<ReadSavedHistoryQuery>()->send(dialog_id, saved_messages_topic_id,
                                                       read_inbox_max_message_id.get_prev_server_message_id());
  }

  do_set_topic_is_marked_as_unread(topic, false);

  on_topic_changed(topic_list, topic, "read_monoforum_topic_messages");
}

void SavedMessagesManager::on_update_read_monoforum_inbox(DialogId dialog_id,
                                                          SavedMessagesTopicId saved_messages_topic_id,
                                                          MessageId read_inbox_max_message_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  if (topic->dialog_id_ != dialog_id) {
    LOG(ERROR) << "Can't update read inbox in a topic of " << dialog_id;
    return;
  }

  read_topic_messages(topic, read_inbox_max_message_id, -1);

  on_topic_changed(topic_list, topic, "on_update_read_monoforum_inbox");
}

void SavedMessagesManager::on_update_read_all_monoforum_inbox(DialogId dialog_id, MessageId read_inbox_max_message_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  if (topic_list->dialog_id_ != dialog_id) {
    LOG(ERROR) << "Can't update read inbox in " << dialog_id;
    return;
  }

  for (auto &it : topic_list->topics_) {
    auto *topic = it.second.get();
    if (topic->read_inbox_max_message_id_ < read_inbox_max_message_id &&
        (!topic->last_message_id_.is_valid() || topic->read_inbox_max_message_id_ < topic->last_message_id_)) {
      read_topic_messages(topic, read_inbox_max_message_id, -1);
      on_topic_changed(topic_list, topic, "on_update_read_all_monoforum_inbox");
    }
  }
}

void SavedMessagesManager::on_update_read_monoforum_outbox(DialogId dialog_id,
                                                           SavedMessagesTopicId saved_messages_topic_id,
                                                           MessageId read_outbox_max_message_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  if (topic->dialog_id_ != dialog_id) {
    LOG(ERROR) << "Can't update read outbox in a topic of " << dialog_id;
    return;
  }

  do_set_topic_read_outbox_max_message_id(topic, read_outbox_max_message_id);

  on_topic_changed(topic_list, topic, "on_update_read_monoforum_outbox");
}

void SavedMessagesManager::on_update_monoforum_nopaid_messages_exception(DialogId dialog_id,
                                                                         SavedMessagesTopicId saved_messages_topic_id,
                                                                         bool nopaid_messages_exception) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  if (topic->dialog_id_ != dialog_id) {
    LOG(ERROR) << "Can't update can_send_unpaid_messages in a topic of " << dialog_id;
    return;
  }

  do_set_topic_nopaid_messages_exception(topic, nopaid_messages_exception);

  on_topic_changed(topic_list, topic, "on_update_monoforum_nopaid_messages_exception");
}

void SavedMessagesManager::on_update_topic_draft_message(
    DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
    telegram_api::object_ptr<telegram_api::DraftMessage> &&draft_message, int32 try_count) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  if (topic->dialog_id_ != dialog_id) {
    LOG(ERROR) << "Can't mark topic as unread in a topic of " << dialog_id;
    return;
  }

  auto input_dialog_ids = get_draft_message_reply_input_dialog_ids(draft_message);
  if (try_count < static_cast<int32>(input_dialog_ids.size())) {
    for (const auto &input_dialog_id : input_dialog_ids) {
      auto reply_in_dialog_id = input_dialog_id.get_dialog_id();
      if (reply_in_dialog_id.is_valid() &&
          !td_->dialog_manager_->have_dialog_force(reply_in_dialog_id, "on_update_topic_draft_message")) {
        td_->dialog_filter_manager_->load_input_dialog(
            input_dialog_id, [actor_id = actor_id(this), dialog_id, saved_messages_topic_id,
                              draft_message = std::move(draft_message), try_count](Unit) mutable {
              send_closure(actor_id, &SavedMessagesManager::on_update_topic_draft_message, dialog_id,
                           saved_messages_topic_id, std::move(draft_message), try_count + 1);
            });
        return;
      }
    }
  }

  do_set_topic_draft_message(topic, get_draft_message(td_, std::move(draft_message)), true);

  on_topic_changed(topic_list, topic, "on_update_topic_draft_message");
}

void SavedMessagesManager::on_update_topic_is_marked_as_unread(DialogId dialog_id,
                                                               SavedMessagesTopicId saved_messages_topic_id,
                                                               bool is_marked_as_unread) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  if (topic->dialog_id_ != dialog_id) {
    LOG(ERROR) << "Can't mark topic as unread in a topic of " << dialog_id;
    return;
  }

  do_set_topic_is_marked_as_unread(topic, is_marked_as_unread);

  on_topic_changed(topic_list, topic, "on_update_topic_is_marked_as_unread");
}

void SavedMessagesManager::on_topic_reaction_count_changed(DialogId dialog_id,
                                                           SavedMessagesTopicId saved_messages_topic_id, int32 count,
                                                           bool is_relative) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return;
  }
  CHECK(topic->dialog_id_ == dialog_id);

  LOG(INFO) << "Change " << (is_relative ? "by" : "to") << ' ' << count << " number of reactions in "
            << saved_messages_topic_id << " of " << dialog_id;

  do_set_topic_unread_reaction_count(topic, is_relative ? topic->unread_reaction_count_ + count : count);
  on_topic_changed(topic_list, topic, "on_topic_reaction_count_changed");
}

int64 SavedMessagesManager::get_topic_order(int32 message_date, MessageId message_id) {
  return (static_cast<int64>(message_date) << 31) +
         message_id.get_prev_server_message_id().get_server_message_id().get();
}

int64 SavedMessagesManager::get_topic_public_order(const TopicList *topic_list, const SavedMessagesTopic *topic) {
  CHECK(topic_list != nullptr);
  if (TopicDate(topic->private_order_, topic->saved_messages_topic_id_) <= topic_list->last_topic_date_) {
    return topic->private_order_;
  }
  return 0;
}

void SavedMessagesManager::on_topic_changed(TopicList *topic_list, SavedMessagesTopic *topic, const char *source) {
  CHECK(topic_list != nullptr);
  CHECK(topic != nullptr);
  if (!topic->is_changed_) {
    return;
  }
  topic->is_changed_ = false;

  int64 new_private_order;
  if (td_->auth_manager_->is_bot()) {
    new_private_order = 0;
  } else {
    if (topic->pinned_order_ != 0) {
      new_private_order = topic->pinned_order_;
    } else if (topic->last_message_date_ != 0 || topic->last_message_id_ != MessageId()) {
      new_private_order = get_topic_order(topic->last_message_date_, topic->last_message_id_);
    } else {
      new_private_order = 0;
    }
    auto draft_message_date = topic->draft_message_date_ != 0
                                  ? topic->draft_message_date_
                                  : (topic->draft_message_ != nullptr ? topic->draft_message_->get_date() : 0);
    if (draft_message_date != 0) {
      int64 draft_order = get_topic_order(draft_message_date, MessageId());
      if (new_private_order < draft_order) {
        new_private_order = draft_order;
      }
    }
    if (topic->private_order_ != new_private_order) {
      if (topic->private_order_ != 0) {
        bool is_deleted =
            topic_list->ordered_topics_.erase({topic->private_order_, topic->saved_messages_topic_id_}) > 0;
        CHECK(is_deleted);
        if (topic_list->server_total_count_ > 0) {
          topic_list->server_total_count_--;
        }
      }
      topic->private_order_ = new_private_order;
      if (topic->private_order_ != 0) {
        bool is_inserted =
            topic_list->ordered_topics_.insert({topic->private_order_, topic->saved_messages_topic_id_}).second;
        CHECK(is_inserted);
        if (topic_list->server_total_count_ >= 0) {
          topic_list->server_total_count_++;
        }
      }
    }
    LOG(INFO) << "Update order of " << topic->saved_messages_topic_id_ << " to " << topic->private_order_ << " from "
              << source;
  }

  send_update_saved_messages_topic(topic_list, topic, source);

  update_saved_messages_topic_sent_total_count(topic_list, source);
}

void SavedMessagesManager::on_topic_message_count_changed(const SavedMessagesTopic *topic, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  LOG(INFO) << "Schedule update of number of messages in " << topic->saved_messages_topic_id_ << " of "
            << topic->dialog_id_ << " from " << source;
  send_closure_later(actor_id(this), &SavedMessagesManager::update_topic_message_count, topic->dialog_id_,
                     topic->saved_messages_topic_id_);
}

void SavedMessagesManager::update_topic_message_count(DialogId dialog_id,
                                                      SavedMessagesTopicId saved_messages_topic_id) {
  CHECK(!td_->auth_manager_->is_bot());
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return;
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr || !topic->is_server_message_count_inited_) {
    return;
  }
  auto new_message_count = topic->local_message_count_ + topic->server_message_count_;
  if (new_message_count == topic->sent_message_count_) {
    return;
  }
  CHECK(new_message_count >= 0);
  topic->sent_message_count_ = new_message_count;
  send_closure(G()->td(), &Td::send_update, get_update_topic_message_count_object(topic));
}

Status SavedMessagesManager::check_monoforum_dialog_id(DialogId dialog_id) const {
  TRY_STATUS(
      td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "get_monoforum_topic_list"));
  if (!td_->dialog_manager_->is_admined_monoforum_channel(dialog_id)) {
    return Status::Error(400, "Chat is not a channel direct messages chat");
  }
  return Status::OK();
}

Result<SavedMessagesManager::TopicList *> SavedMessagesManager::get_monoforum_topic_list(DialogId dialog_id) {
  TRY_STATUS(check_monoforum_dialog_id(dialog_id));
  return add_topic_list(dialog_id);
}

SavedMessagesManager::TopicList *SavedMessagesManager::get_topic_list(DialogId dialog_id) {
  return const_cast<TopicList *>(const_cast<const SavedMessagesManager *>(this)->get_topic_list(dialog_id));
}

const SavedMessagesManager::TopicList *SavedMessagesManager::get_topic_list(DialogId dialog_id) const {
  if (dialog_id == DialogId() || dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    return &topic_list_;
  }
  if (!td_->dialog_manager_->is_admined_monoforum_channel(dialog_id)) {
    return nullptr;
  }
  auto it = monoforum_topic_lists_.find(dialog_id);
  if (it == monoforum_topic_lists_.end()) {
    return nullptr;
  }
  return it->second.get();
}

SavedMessagesManager::TopicList *SavedMessagesManager::add_topic_list(DialogId dialog_id) {
  if (td_->auth_manager_->is_bot()) {
    return nullptr;
  }
  if (dialog_id == DialogId() || dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    return &topic_list_;
  }
  if (check_monoforum_dialog_id(dialog_id).is_error()) {
    return nullptr;
  }
  CHECK(dialog_id.is_valid());
  auto &topic_list = monoforum_topic_lists_[dialog_id];
  if (topic_list == nullptr) {
    topic_list = make_unique<TopicList>();
    topic_list->dialog_id_ = dialog_id;
    topic_list->generation_ = ++current_topic_list_generation_;
    topic_list->are_pinned_saved_messages_topics_inited_ = true;
  }
  return topic_list.get();
}

void SavedMessagesManager::load_monoforum_topics(DialogId dialog_id, int32 limit, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, topic_list, get_monoforum_topic_list(dialog_id));
  load_topics(topic_list, limit, std::move(promise));
}

void SavedMessagesManager::load_saved_messages_topics(int32 limit, Promise<Unit> &&promise) {
  load_topics(&topic_list_, limit, std::move(promise));
}

void SavedMessagesManager::load_topics(TopicList *topic_list, int32 limit, Promise<Unit> &&promise) {
  CHECK(topic_list != nullptr);
  if (limit < 0) {
    return promise.set_error(400, "Limit must be non-negative");
  }
  if (limit == 0) {
    return promise.set_value(Unit());
  }
  if (topic_list->last_topic_date_ == MAX_TOPIC_DATE) {
    return promise.set_error(404, "Not Found");
  }
  if (!topic_list->are_pinned_saved_messages_topics_inited_) {
    CHECK(topic_list == &topic_list_);
    return get_pinned_saved_dialogs(limit, std::move(promise));
  }
  get_saved_dialogs(topic_list, limit, std::move(promise));
}

void SavedMessagesManager::get_pinned_saved_dialogs(int32 limit, Promise<Unit> &&promise) {
  topic_list_.load_pinned_queries_.push_back(std::move(promise));
  if (topic_list_.load_pinned_queries_.size() == 1) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<Unit> &&result) {
      send_closure(actor_id, &SavedMessagesManager::on_get_pinned_saved_dialogs, std::move(result));
    });
    td_->create_handler<GetPinnedSavedDialogsQuery>(std::move(query_promise))->send(topic_list_.generation_, limit);
  }
}

void SavedMessagesManager::on_get_pinned_saved_dialogs(Result<Unit> &&result) {
  G()->ignore_result_if_closing(result);
  if (result.is_error()) {
    fail_promises(topic_list_.load_pinned_queries_, result.move_as_error());
  } else {
    set_promises(topic_list_.load_pinned_queries_);
  }
}

void SavedMessagesManager::get_saved_dialogs(TopicList *topic_list, int32 limit, Promise<Unit> &&promise) {
  CHECK(topic_list != nullptr);
  topic_list->load_queries_.push_back(std::move(promise));
  if (topic_list->load_queries_.size() == 1) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), topic_list](Result<Unit> &&result) {
      send_closure(actor_id, &SavedMessagesManager::on_get_saved_dialogs, topic_list, std::move(result));
    });
    td_->create_handler<GetSavedDialogsQuery>(std::move(query_promise))
        ->send(topic_list->dialog_id_, topic_list->generation_, topic_list->offset_date_,
               topic_list->offset_message_id_, topic_list->offset_dialog_id_, limit);
  }
}

SavedMessagesManager::SavedMessagesTopicInfo SavedMessagesManager::get_saved_messages_topic_info(
    Td *td, telegram_api::object_ptr<telegram_api::SavedDialog> &&dialog_ptr, bool is_saved_messages) {
  SavedMessagesTopicInfo result;
  if (is_saved_messages) {
    if (dialog_ptr->get_id() != telegram_api::savedDialog::ID) {
      LOG(ERROR) << "Receive " << to_string(dialog_ptr);
      return result;
    }
    auto dialog = telegram_api::move_object_as<telegram_api::savedDialog>(dialog_ptr);
    result.peer_dialog_id_ = DialogId(dialog->peer_);
    result.last_topic_message_id_ = MessageId(ServerMessageId(dialog->top_message_));
    result.is_pinned_ = dialog->pinned_;
  } else {
    if (dialog_ptr->get_id() != telegram_api::monoForumDialog::ID) {
      LOG(ERROR) << "Receive " << to_string(dialog_ptr);
      return result;
    }
    auto dialog = telegram_api::move_object_as<telegram_api::monoForumDialog>(dialog_ptr);
    result.peer_dialog_id_ = DialogId(dialog->peer_);
    result.last_topic_message_id_ = MessageId(ServerMessageId(dialog->top_message_));
    result.read_inbox_max_message_id_ = MessageId(ServerMessageId(dialog->read_inbox_max_id_));
    result.read_outbox_max_message_id_ = MessageId(ServerMessageId(dialog->read_outbox_max_id_));
    result.unread_count_ = max(0, dialog->unread_count_);
    result.unread_reaction_count_ = dialog->unread_reactions_count_;
    result.is_marked_as_unread_ = dialog->unread_mark_;
    result.nopaid_messages_exception_ = dialog->nopaid_messages_exception_;
    result.draft_message_ = get_draft_message(td, std::move(dialog->draft_));
  }
  return result;
}

void SavedMessagesManager::on_get_saved_dialogs(TopicList *topic_list, Result<Unit> &&result) {
  G()->ignore_result_if_closing(result);
  CHECK(topic_list != nullptr);
  if (result.is_error()) {
    fail_promises(topic_list->load_queries_, result.move_as_error());
  } else {
    set_promises(topic_list->load_queries_);
  }
}

void SavedMessagesManager::on_get_saved_messages_topics(
    DialogId dialog_id, uint32 generation, SavedMessagesTopicId expected_saved_messages_topic_id, bool is_pinned,
    int32 limit, telegram_api::object_ptr<telegram_api::messages_SavedDialogs> &&saved_dialogs_ptr,
    Promise<Unit> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Chat has no topics");
  }
  if (topic_list->generation_ != generation) {
    return promise.set_error(400, "Topic was deleted");
  }

  CHECK(saved_dialogs_ptr != nullptr);
  int32 total_count = -1;
  vector<telegram_api::object_ptr<telegram_api::SavedDialog>> dialogs;
  vector<telegram_api::object_ptr<telegram_api::Message>> messages;
  vector<telegram_api::object_ptr<telegram_api::Chat>> chats;
  vector<telegram_api::object_ptr<telegram_api::User>> users;
  bool is_last = false;
  switch (saved_dialogs_ptr->get_id()) {
    case telegram_api::messages_savedDialogsNotModified::ID:
      LOG(ERROR) << "Receive messages.savedDialogsNotModified";
      return promise.set_error(500, "Receive messages.savedDialogsNotModified");
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
      is_last = dialogs.empty();
      break;
    }
    default:
      UNREACHABLE();
  }
  td_->user_manager_->on_get_users(std::move(users), "on_get_saved_messages_topics");
  td_->chat_manager_->on_get_chats(std::move(chats), "on_get_saved_messages_topics");

  MessagesInfo messages_info;
  messages_info.messages = std::move(messages);
  td_->messages_manager_->get_channel_difference_if_needed(
      dialog_id, std::move(messages_info),
      PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, generation, expected_saved_messages_topic_id,
                              is_pinned, limit, total_count, dialogs = std::move(dialogs), is_last,
                              promise = std::move(promise)](Result<MessagesInfo> &&r_info) mutable {
        if (r_info.is_error()) {
          return promise.set_error(r_info.move_as_error());
        }
        auto info = r_info.move_as_ok();
        send_closure(actor_id, &SavedMessagesManager::process_saved_messages_topics, dialog_id, generation,
                     expected_saved_messages_topic_id, is_pinned, limit, total_count, std::move(dialogs),
                     std::move(info.messages), is_last, std::move(promise));
      }),
      "on_get_saved_messages_topics");
}

void SavedMessagesManager::process_saved_messages_topics(
    DialogId dialog_id, uint32 generation, SavedMessagesTopicId expected_saved_messages_topic_id, bool is_pinned,
    int32 limit, int32 total_count, vector<telegram_api::object_ptr<telegram_api::SavedDialog>> &&dialogs,
    vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, bool is_last, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Chat has no topics");
  }
  if (topic_list->generation_ != generation) {
    return promise.set_error(400, "Topic was deleted");
  }

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
  bool is_saved_messages = topic_list->dialog_id_ == DialogId();
  bool is_get_topic = expected_saved_messages_topic_id.is_valid();
  for (auto &dialog_ptr : dialogs) {
    auto topic_info = get_saved_messages_topic_info(td_, std::move(dialog_ptr), is_saved_messages);
    if (!topic_info.peer_dialog_id_.is_valid()) {
      LOG(ERROR) << "Receive " << topic_info.peer_dialog_id_ << " in result of getSavedMessagesTopics";
      total_count--;
      continue;
    }
    SavedMessagesTopicId saved_messages_topic_id(topic_info.peer_dialog_id_);
    if (is_get_topic && expected_saved_messages_topic_id != saved_messages_topic_id) {
      LOG(ERROR) << "Receive " << saved_messages_topic_id << " instead of " << expected_saved_messages_topic_id;
      total_count--;
      continue;
    }
    if (td::contains(added_saved_messages_topic_ids, saved_messages_topic_id)) {
      LOG(ERROR) << "Receive " << saved_messages_topic_id
                 << " twice in result of getSavedMessagesTopics with total_count = " << total_count;
      total_count--;
      continue;
    }
    added_saved_messages_topic_ids.push_back(saved_messages_topic_id);

    auto last_topic_message_id = topic_info.last_topic_message_id_;
    auto message_date = 0;
    if (last_topic_message_id.is_valid()) {
      auto it = message_id_to_message.find(last_topic_message_id);
      if (it == message_id_to_message.end()) {
        LOG(ERROR) << "Can't find last " << last_topic_message_id << " in " << saved_messages_topic_id;
        total_count--;
        continue;
      }
      message_date = MessagesManager::get_message_date(it->second);
      if (!is_pinned && message_date > 0) {
        if (last_message_date != 0 && (last_message_date < message_date || last_message_id < last_topic_message_id)) {
          LOG(ERROR) << "Receive " << last_topic_message_id << " at " << message_date << " after " << last_message_id
                     << " at " << last_message_date;
        }
        last_message_date = message_date;
        last_message_id = last_topic_message_id;
        last_dialog_id = topic_info.peer_dialog_id_;
      }
      auto message_full_id = td_->messages_manager_->on_get_message(
          is_saved_messages ? td_->dialog_manager_->get_my_dialog_id() : dialog_id, std::move(it->second), false, false,
          false, "on_get_saved_messages_topics");
      message_id_to_message.erase(it);

      auto message_id = message_full_id.get_message_id();
      if (message_id == MessageId()) {
        LOG(ERROR) << "Can't add last " << last_topic_message_id << " to " << saved_messages_topic_id;
        total_count--;
        continue;
      }
      CHECK(message_id == last_topic_message_id);
    } else if (!is_get_topic) {
      // skip topics without messages
      LOG(ERROR) << "Receive " << saved_messages_topic_id << " without last message";
      total_count--;
      continue;
    }

    auto *topic = add_topic(topic_list, saved_messages_topic_id, true);
    if (last_topic_message_id.is_valid() && !topic->ordered_messages_.has_message(last_topic_message_id)) {
      LOG(ERROR) << "Receive " << last_topic_message_id << " in " << dialog_id << ", which isn't from "
                 << saved_messages_topic_id;
      total_count--;
      continue;
    }
    if (!td_->auth_manager_->is_bot()) {
      if (topic->last_message_id_ == MessageId() && last_topic_message_id.is_valid() &&
          topic->ordered_messages_.get_last_message_id() == last_topic_message_id) {
        do_set_topic_last_message_id(topic, last_topic_message_id, message_date);
      }
      if (topic->read_inbox_max_message_id_ == MessageId() || topic->need_repair_unread_count_) {
        auto read_inbox_max_message_id = topic_info.read_inbox_max_message_id_;
        if (topic->read_inbox_max_message_id_.is_valid() && !topic->read_inbox_max_message_id_.is_server() &&
            read_inbox_max_message_id == topic->read_inbox_max_message_id_.get_prev_server_message_id()) {
          read_inbox_max_message_id = topic->read_inbox_max_message_id_;
        }
        if (topic->need_repair_unread_count_ &&
            (topic->read_inbox_max_message_id_ <= read_inbox_max_message_id ||
             !td_->dialog_manager_->have_input_peer(dialog_id, false, AccessRights::Read))) {
          LOG(INFO) << "Repaired server unread count in " << dialog_id << " from " << topic->read_inbox_max_message_id_
                    << '/' << topic->unread_count_ << " to " << read_inbox_max_message_id << '/'
                    << topic_info.unread_count_;
          topic->need_repair_unread_count_ = false;
        }
        if (topic->need_repair_unread_count_) {
          LOG(INFO) << "Failed to repair server unread count in " << saved_messages_topic_id << " of " << dialog_id
                    << ", because locally read messages up to " << topic->read_inbox_max_message_id_
                    << ", but server-side only up to " << read_inbox_max_message_id;
          topic->need_repair_unread_count_ = false;
        }
        do_set_topic_read_inbox_max_message_id(topic, topic_info.read_inbox_max_message_id_, topic_info.unread_count_,
                                               "on_get_saved_messages_topics");
      }
      do_set_topic_read_outbox_max_message_id(topic, topic_info.read_outbox_max_message_id_);
      do_set_topic_unread_reaction_count(topic, topic_info.unread_reaction_count_);
      do_set_topic_is_marked_as_unread(topic, topic_info.is_marked_as_unread_);
      do_set_topic_nopaid_messages_exception(topic, topic_info.nopaid_messages_exception_);
      do_set_topic_draft_message(topic, std::move(topic_info.draft_message_), true);
    }
    on_topic_changed(topic_list, topic, "on_get_saved_messages_topics");
  }

  if (is_get_topic) {
    // nothing to do
  } else if (!is_pinned) {
    topic_list->server_total_count_ = total_count;

    topic_list->offset_date_ = last_message_date;
    topic_list->offset_dialog_id_ = last_dialog_id;
    topic_list->offset_message_id_ = last_message_id;
  } else if (topic_list->server_total_count_ <= total_count) {
    topic_list->server_total_count_ = total_count + 1;
  }
  update_saved_messages_topic_sent_total_count(topic_list, "on_get_saved_messages_topics");

  if (is_get_topic) {
    if (added_saved_messages_topic_ids.size() != 1u) {
      return promise.set_error(404, "Not Found");
    }
  } else if (is_pinned) {
    if (!topic_list->are_pinned_saved_messages_topics_inited_ && total_count < limit) {
      get_saved_dialogs(topic_list, limit - total_count, std::move(promise));
      promise = Promise<Unit>();
    }
    topic_list->are_pinned_saved_messages_topics_inited_ = true;
    set_pinned_saved_messages_topics(std::move(added_saved_messages_topic_ids));
    set_last_topic_date(topic_list, {MIN_PINNED_TOPIC_ORDER - 1, SavedMessagesTopicId()});
  } else if (is_last) {
    set_last_topic_date(topic_list, MAX_TOPIC_DATE);

    if (dialogs.empty()) {
      return promise.set_error(404, "Not Found");
    }
  } else if (last_message_date > 0) {
    set_last_topic_date(topic_list,
                        {get_topic_order(last_message_date, last_message_id), SavedMessagesTopicId(last_dialog_id)});
  } else {
    LOG(ERROR) << "Receive no suitable topics";
    set_last_topic_date(topic_list, MAX_TOPIC_DATE);
    return promise.set_error(404, "Not Found");
  }

  promise.set_value(Unit());
}

td_api::object_ptr<td_api::savedMessagesTopic> SavedMessagesManager::get_saved_messages_topic_object(
    const SavedMessagesTopic *topic) const {
  CHECK(topic != nullptr);
  CHECK(topic->dialog_id_ == DialogId());
  td_api::object_ptr<td_api::message> last_message_object;
  if (topic->last_message_id_ != MessageId()) {
    last_message_object = td_->messages_manager_->get_message_object(
        {td_->dialog_manager_->get_my_dialog_id(), topic->last_message_id_}, "get_saved_messages_topic_object");
  }
  td_api::object_ptr<td_api::draftMessage> draft_message_object;
  if (topic->draft_message_date_ != 0) {
    draft_message_object = td_->messages_manager_->get_my_dialog_draft_message_object();
  }
  return td_api::make_object<td_api::savedMessagesTopic>(
      topic->saved_messages_topic_id_.get_unique_id(),
      topic->saved_messages_topic_id_.get_saved_messages_topic_type_object(td_), topic->pinned_order_ != 0,
      get_topic_public_order(&topic_list_, topic), std::move(last_message_object), std::move(draft_message_object));
}

td_api::object_ptr<td_api::updateSavedMessagesTopic> SavedMessagesManager::get_update_saved_messages_topic_object(
    const SavedMessagesTopic *topic) const {
  return td_api::make_object<td_api::updateSavedMessagesTopic>(get_saved_messages_topic_object(topic));
}

td_api::object_ptr<td_api::directMessagesChatTopic> SavedMessagesManager::get_direct_messages_chat_topic_object(
    const TopicList *topic_list, const SavedMessagesTopic *topic) const {
  CHECK(topic_list != nullptr);
  CHECK(topic != nullptr);
  CHECK(topic->dialog_id_ != DialogId());
  td_api::object_ptr<td_api::message> last_message_object;
  if (topic->last_message_id_ != MessageId()) {
    last_message_object = td_->messages_manager_->get_message_object({topic->dialog_id_, topic->last_message_id_},
                                                                     "get_direct_messages_chat_topic_object");
  }
  return td_api::make_object<td_api::directMessagesChatTopic>(
      td_->dialog_manager_->get_chat_id_object(topic->dialog_id_, "directMessagesChatTopic"),
      topic->saved_messages_topic_id_.get_unique_id(),
      topic->saved_messages_topic_id_.get_monoforum_message_sender_object(td_),
      get_topic_public_order(topic_list, topic), topic->nopaid_messages_exception_, topic->is_marked_as_unread_,
      topic->unread_count_, topic->read_inbox_max_message_id_.get(), topic->read_outbox_max_message_id_.get(),
      topic->unread_reaction_count_, std::move(last_message_object),
      get_draft_message_object(td_, topic->draft_message_));
}

td_api::object_ptr<td_api::updateDirectMessagesChatTopic>
SavedMessagesManager::get_update_direct_messages_chat_topic_object(const TopicList *topic_list,
                                                                   const SavedMessagesTopic *topic) const {
  return td_api::make_object<td_api::updateDirectMessagesChatTopic>(
      get_direct_messages_chat_topic_object(topic_list, topic));
}

void SavedMessagesManager::send_update_saved_messages_topic(const TopicList *topic_list,
                                                            const SavedMessagesTopic *topic, const char *source) const {
  CHECK(topic_list != nullptr);
  CHECK(topic != nullptr);
  LOG(INFO) << "Send update about " << topic->saved_messages_topic_id_ << " in " << topic->dialog_id_ << " with order "
            << get_topic_public_order(topic_list, topic) << " and last " << topic->last_message_id_ << " sent at "
            << topic->last_message_date_ << " with draft at " << topic->draft_message_date_ << " from " << source;
  if (topic->dialog_id_ == DialogId()) {
    send_closure(G()->td(), &Td::send_update, get_update_saved_messages_topic_object(topic));
  } else {
    send_closure(G()->td(), &Td::send_update, get_update_direct_messages_chat_topic_object(topic_list, topic));
  }
}

int64 SavedMessagesManager::get_next_pinned_saved_messages_topic_order() {
  current_pinned_saved_messages_topic_order_++;
  LOG(INFO) << "Assign pinned_order = " << current_pinned_saved_messages_topic_order_;
  return current_pinned_saved_messages_topic_order_;
}

td_api::object_ptr<td_api::updateSavedMessagesTopicCount>
SavedMessagesManager::get_update_saved_messages_topic_count_object() const {
  CHECK(topic_list_.sent_total_count_ != -1);
  return td_api::make_object<td_api::updateSavedMessagesTopicCount>(topic_list_.sent_total_count_);
}

void SavedMessagesManager::update_saved_messages_topic_sent_total_count(TopicList *topic_list, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  CHECK(topic_list != nullptr);
  if (topic_list->dialog_id_ != DialogId()) {
    return;
  }
  if (topic_list->server_total_count_ == -1) {
    return;
  }
  LOG(INFO) << "Update Saved Messages topic sent total count from " << source;
  auto new_total_count = static_cast<int32>(topic_list->ordered_topics_.size());
  if (topic_list->last_topic_date_ != MAX_TOPIC_DATE) {
    new_total_count = max(new_total_count, topic_list->server_total_count_);
  } else if (topic_list->server_total_count_ != new_total_count) {
    topic_list->server_total_count_ = new_total_count;
  }
  if (topic_list->sent_total_count_ != new_total_count) {
    topic_list->sent_total_count_ = new_total_count;
    send_closure(G()->td(), &Td::send_update, get_update_saved_messages_topic_count_object());
  }
}

td_api::object_ptr<td_api::updateTopicMessageCount> SavedMessagesManager::get_update_topic_message_count_object(
    const SavedMessagesTopic *topic) const {
  CHECK(topic != nullptr);
  auto dialog_id = topic->dialog_id_ == DialogId() ? td_->dialog_manager_->get_my_dialog_id() : topic->dialog_id_;
  auto message_topic = topic->dialog_id_ == DialogId()
                           ? MessageTopic::saved_messages(dialog_id, topic->saved_messages_topic_id_)
                           : MessageTopic::monoforum(dialog_id, topic->saved_messages_topic_id_);
  return td_api::make_object<td_api::updateTopicMessageCount>(
      td_->dialog_manager_->get_chat_id_object(dialog_id, "updateTopicMessageCount"),
      message_topic.get_message_topic_object(td_), topic->sent_message_count_);
}

bool SavedMessagesManager::set_pinned_saved_messages_topics(vector<SavedMessagesTopicId> saved_messages_topic_ids) {
  auto *topic_list = &topic_list_;
  if (topic_list->pinned_saved_messages_topic_ids_ == saved_messages_topic_ids) {
    return false;
  }
  LOG(INFO) << "Update pinned Saved Messages topics from " << topic_list->pinned_saved_messages_topic_ids_ << " to "
            << saved_messages_topic_ids;
  FlatHashSet<SavedMessagesTopicId, SavedMessagesTopicIdHash> old_pinned_saved_messages_topic_ids;
  for (auto pinned_saved_messages_topic_id : topic_list->pinned_saved_messages_topic_ids_) {
    CHECK(pinned_saved_messages_topic_id.is_valid());
    old_pinned_saved_messages_topic_ids.insert(pinned_saved_messages_topic_id);
  }

  auto pinned_saved_messages_topic_ids = topic_list->pinned_saved_messages_topic_ids_;
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
    set_saved_messages_topic_is_pinned(saved_messages_topic_id, true, "set_pinned_saved_messages_topics 1");
  }
  for (auto saved_messages_topic_id : old_pinned_saved_messages_topic_ids) {
    set_saved_messages_topic_is_pinned(saved_messages_topic_id, false, "set_pinned_saved_messages_topics 2");
  }
  return true;
}

bool SavedMessagesManager::set_saved_messages_topic_is_pinned(SavedMessagesTopicId saved_messages_topic_id,
                                                              bool is_pinned, const char *source) {
  return set_saved_messages_topic_is_pinned(get_topic(DialogId(), saved_messages_topic_id), is_pinned, source);
}

bool SavedMessagesManager::set_saved_messages_topic_is_pinned(SavedMessagesTopic *topic, bool is_pinned,
                                                              const char *source) {
  CHECK(!td_->auth_manager_->is_bot());
  LOG_CHECK(topic != nullptr) << source;
  CHECK(topic->dialog_id_ == DialogId());
  auto *topic_list = &topic_list_;
  if (!topic_list->are_pinned_saved_messages_topics_inited_) {
    return false;
  }
  auto saved_messages_topic_id = topic->saved_messages_topic_id_;
  if (is_pinned) {
    if (!topic_list->pinned_saved_messages_topic_ids_.empty() &&
        topic_list->pinned_saved_messages_topic_ids_[0] == saved_messages_topic_id) {
      return false;
    }
    topic->pinned_order_ = get_next_pinned_saved_messages_topic_order();
    add_to_top(topic_list->pinned_saved_messages_topic_ids_, topic_list->pinned_saved_messages_topic_ids_.size() + 1,
               saved_messages_topic_id);
  } else {
    if (topic->pinned_order_ == 0 ||
        !td::remove(topic_list->pinned_saved_messages_topic_ids_, saved_messages_topic_id)) {
      return false;
    }
    topic->pinned_order_ = 0;
  }

  LOG(INFO) << "Set " << saved_messages_topic_id << " pinned order to " << topic->pinned_order_ << " from " << source;
  topic->is_changed_ = true;
  on_topic_changed(&topic_list_, topic, source);
  return true;
}

void SavedMessagesManager::set_last_topic_date(TopicList *topic_list, TopicDate topic_date) {
  CHECK(topic_list != nullptr);
  if (topic_date <= topic_list->last_topic_date_) {
    return;
  }
  auto min_topic_date = topic_list->last_topic_date_;
  topic_list->last_topic_date_ = topic_date;
  for (auto it = topic_list->ordered_topics_.upper_bound(min_topic_date);
       it != topic_list->ordered_topics_.end() && *it <= topic_date; ++it) {
    auto topic = get_topic(topic_list, it->get_topic_id());
    CHECK(topic != nullptr);
    send_update_saved_messages_topic(topic_list, topic, "set_last_topic_date");
  }
}

void SavedMessagesManager::get_monoforum_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                               Promise<td_api::object_ptr<td_api::directMessagesChatTopic>> &&promise) {
  TRY_RESULT_PROMISE(promise, topic_list, get_monoforum_topic_list(dialog_id));
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));

  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic != nullptr && topic->is_received_from_server_) {
    if (!promise) {
      return promise.set_value(nullptr);
    }
    return promise.set_value(get_direct_messages_chat_topic_object(topic_list, topic));
  }

  reload_monoforum_topic(dialog_id, saved_messages_topic_id, std::move(promise));
}

void SavedMessagesManager::reload_monoforum_topic(
    DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
    Promise<td_api::object_ptr<td_api::directMessagesChatTopic>> &&promise) {
  CHECK(dialog_id != DialogId());
  auto topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Topic list not found");
  }
  if (saved_messages_topic_id.is_valid_in(td_, dialog_id).is_error()) {
    LOG(ERROR) << "Can't load " << saved_messages_topic_id << " of " << dialog_id << ": "
               << saved_messages_topic_id.is_valid_in(td_, dialog_id);
    return promise.set_error(500, "Can't load topic info");
  }
  auto &queries = topic_list->get_topic_queries_[saved_messages_topic_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1u) {
    auto query_promise =
        PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, generation = topic_list->generation_,
                                saved_messages_topic_id](Result<Unit> &&result) mutable {
          send_closure(actor_id, &SavedMessagesManager::on_get_monoforum_topic, dialog_id, generation,
                       saved_messages_topic_id, std::move(result));
        });
    td_->create_handler<GetSavedDialogsByIdQuery>(std::move(query_promise))
        ->send(dialog_id, topic_list->generation_, saved_messages_topic_id);
  }
}

void SavedMessagesManager::on_get_monoforum_topic(DialogId dialog_id, uint32 generation,
                                                  SavedMessagesTopicId saved_messages_topic_id, Result<Unit> &&result) {
  G()->ignore_result_if_closing(result);

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr || topic_list->generation_ != generation) {
    return;
  }
  auto it = topic_list->get_topic_queries_.find(saved_messages_topic_id);
  CHECK(it != topic_list->get_topic_queries_.end());
  auto promises = std::move(it->second);
  topic_list->get_topic_queries_.erase(it);

  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (result.is_ok() && topic == nullptr) {
    result = Status::Error(500, "Topic not found");
  }
  if (result.is_error()) {
    return fail_promises(promises, result.move_as_error());
  }

  for (auto &promise : promises) {
    if (!promise) {
      return promise.set_value(nullptr);
    }
    promise.set_value(get_direct_messages_chat_topic_object(topic_list, topic));
  }
}

void SavedMessagesManager::get_monoforum_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                       MessageId from_message_id, int32 offset, int32 limit,
                                                       Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  TRY_STATUS_PROMISE(promise, get_monoforum_topic_list(dialog_id));
  get_topic_history(dialog_id, saved_messages_topic_id, from_message_id, offset, limit, 4, std::move(promise));
}

void SavedMessagesManager::get_saved_messages_topic_history(SavedMessagesTopicId saved_messages_topic_id,
                                                            MessageId from_message_id, int32 offset, int32 limit,
                                                            Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  get_topic_history(td_->dialog_manager_->get_my_dialog_id(), saved_messages_topic_id, from_message_id, offset, limit,
                    4, std::move(promise));
}

void SavedMessagesManager::get_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                             MessageId from_message_id, int32 offset, int32 limit, int32 left_tries,
                                             Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Chat has no topics");
  }

  if (limit <= 0) {
    return promise.set_error(400, "Parameter limit must be positive");
  }
  if (limit > MAX_GET_HISTORY) {
    limit = MAX_GET_HISTORY;
  }
  if (offset > 0) {
    return promise.set_error(400, "Parameter offset must be non-positive");
  }
  if (offset <= -MAX_GET_HISTORY) {
    return promise.set_error(400, "Parameter offset must be greater than -100");
  }
  if (offset < -limit) {
    return promise.set_error(400, "Parameter offset must be greater than or equal to -limit");
  }

  if (from_message_id == MessageId() || from_message_id.get() > MessageId::max().get()) {
    from_message_id = MessageId::max();
    limit += offset;
    offset = 0;
  }
  if (!from_message_id.is_valid()) {
    return promise.set_error(400, "Invalid value of parameter from_message_id specified");
  }

  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  do_get_topic_history(topic_list, topic, dialog_id, saved_messages_topic_id, from_message_id, offset, limit,
                       left_tries, std::move(promise));
}

void SavedMessagesManager::do_get_topic_history(const TopicList *topic_list, const SavedMessagesTopic *topic,
                                                DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                MessageId from_message_id, int32 offset, int32 limit, int32 left_tries,
                                                Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));

  CHECK(topic_list != nullptr);
  int32 total_count = -1;
  vector<MessageId> message_ids;
  auto initial_from_message_id = from_message_id;
  auto initial_offset = offset;
  auto initial_limit = limit;
  bool from_the_end = from_message_id == MessageId::max();
  if (topic != nullptr && topic->is_server_message_count_inited_) {
    total_count = topic->server_message_count_ + topic->local_message_count_;
    LOG(INFO) << "Have local last " << topic->last_message_id_ << " and " << total_count
              << " messages. Get history from " << from_message_id << " with offset " << offset << " and limit "
              << limit;
    message_ids = topic->ordered_messages_.get_history(topic->last_message_id_, from_message_id, offset, limit,
                                                       left_tries == 0 /* && !only_local*/);
  }
  if (!message_ids.empty() || limit <= 0 || left_tries == 0) {
    return promise.set_value(
        td_->messages_manager_->get_messages_object(total_count, dialog_id, message_ids, true, "do_get_topic_history"));
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, generation = topic_list->generation_, saved_messages_topic_id,
       from_message_id = initial_from_message_id, offset = initial_offset, limit = initial_limit, left_tries,
       promise = std::move(promise)](Result<MessagesInfo> &&r_info) mutable {
        send_closure(actor_id, &SavedMessagesManager::on_get_topic_history, dialog_id, generation,
                     saved_messages_topic_id, from_message_id, offset, limit, left_tries, std::move(r_info),
                     std::move(promise));
      });
  if (from_the_end) {
    // load only 10 messages when repairing the last message
    limit = !promise ? max(limit, 10) : MAX_GET_HISTORY;
    offset = 0;
  } else if (offset >= -1) {
    // get history before some server or local message
    limit = clamp(limit + offset + 1, MAX_GET_HISTORY / 2, MAX_GET_HISTORY);
    offset = -1;
  } else {
    // get history around some server or local message
    int32 messages_to_load = max(MAX_GET_HISTORY, limit);
    int32 max_add = max(messages_to_load - limit - 2, 0);
    offset -= max_add;
    limit = MAX_GET_HISTORY;
  }
  td_->create_handler<GetSavedHistoryQuery>(std::move(query_promise))
      ->send(dialog_id, saved_messages_topic_id, from_message_id.get_next_server_message_id(), offset, limit);
}

void SavedMessagesManager::on_get_topic_history(DialogId dialog_id, uint32 generation,
                                                SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id,
                                                int32 offset, int32 limit, int32 left_tries,
                                                Result<MessagesInfo> &&r_info,
                                                Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  G()->ignore_result_if_closing(r_info);

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Chat has no topics");
  }
  if (topic_list->generation_ != generation) {
    return promise.set_error(400, "Topic was deleted");
  }

  if (r_info.is_error()) {
    return promise.set_error(r_info.move_as_error());
  }
  auto info = r_info.move_as_ok();
  if (info.messages.empty() && get_topic(topic_list, saved_messages_topic_id) == nullptr) {
    return promise.set_value(
        td_->messages_manager_->get_messages_object(0, dialog_id, {}, true, "on_get_topic_history"));
  }
  if (!MessageId::is_message_id_order_descending(info.messages, "on_get_topic_history")) {
    return promise.set_error(500, "Receive invalid response");
  }

  auto *topic = add_topic(topic_list, saved_messages_topic_id, false);
  MessageId first_message_id;
  MessageId last_message_id;
  int32 last_message_date = 0;
  bool from_the_end = from_message_id == MessageId::max();
  bool have_next = false;
  for (auto &message : info.messages) {
    auto message_date = MessagesManager::get_message_date(message);
    auto message_full_id = td_->messages_manager_->on_get_message(dialog_id, std::move(message), false, false, false,
                                                                  "on_get_topic_history");
    auto message_id = message_full_id.get_message_id();
    if (message_id == MessageId()) {
      info.total_count--;
      continue;
    }
    if (!topic->ordered_messages_.has_message(message_id)) {
      LOG(ERROR) << "Receive " << message_id << " in " << dialog_id << ", which isn't from " << saved_messages_topic_id;
      info.total_count--;
      continue;
    }
    if (!have_next && from_the_end && message_id < topic->last_message_id_) {
      // last message in the dialog should be attached to the next message if there is some
      have_next = true;
    }
    if (have_next) {
      topic->ordered_messages_.attach_message_to_next(message_id, "on_get_topic_history");
    }
    if (!last_message_id.is_valid()) {
      last_message_id = message_id;
      last_message_date = message_date;
    }
    if (!have_next) {
      have_next = true;
    } else if (first_message_id.is_valid()) {
      topic->ordered_messages_.attach_message_to_previous(first_message_id, "on_get_topic_history");
    }
    first_message_id = message_id;
  }
  if (from_the_end && last_message_id.is_valid() && last_message_id > topic->last_message_id_ &&
      topic->ordered_messages_.get_last_message_id() == last_message_id) {
    do_set_topic_last_message_id(topic, last_message_id, last_message_date);
    on_topic_changed(topic_list, topic, "on_get_topic_history");
  }
  topic->server_message_count_ = info.total_count;
  topic->is_server_message_count_inited_ = true;
  update_topic_message_count(dialog_id, saved_messages_topic_id);

  do_get_topic_history(topic_list, topic, dialog_id, saved_messages_topic_id, from_message_id, offset, limit,
                       left_tries - 1, std::move(promise));
}

void SavedMessagesManager::delete_monoforum_topic_history(DialogId dialog_id,
                                                          SavedMessagesTopicId saved_messages_topic_id,
                                                          Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, get_monoforum_topic_list(dialog_id));
  delete_topic_history(dialog_id, saved_messages_topic_id, std::move(promise));
}

void SavedMessagesManager::delete_saved_messages_topic_history(SavedMessagesTopicId saved_messages_topic_id,
                                                               Promise<Unit> &&promise) {
  delete_topic_history(td_->dialog_manager_->get_my_dialog_id(), saved_messages_topic_id, std::move(promise));
}

void SavedMessagesManager::delete_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));

  MessageQueryManager::AffectedHistoryQuery query = [td = td_, saved_messages_topic_id](
                                                        DialogId dialog_id, Promise<AffectedHistory> &&query_promise) {
    td->create_handler<DeleteSavedHistoryQuery>(std::move(query_promise))->send(dialog_id, saved_messages_topic_id);
  };
  td_->message_query_manager_->run_affected_history_query_until_complete(dialog_id, std::move(query), true,
                                                                         std::move(promise));
}

void SavedMessagesManager::get_monoforum_topic_message_by_date(DialogId dialog_id,
                                                               SavedMessagesTopicId saved_messages_topic_id, int32 date,
                                                               Promise<td_api::object_ptr<td_api::message>> &&promise) {
  TRY_STATUS_PROMISE(promise, get_monoforum_topic_list(dialog_id));
  get_topic_message_by_date(dialog_id, saved_messages_topic_id, date, std::move(promise));
}

void SavedMessagesManager::get_saved_messages_topic_message_by_date(
    SavedMessagesTopicId saved_messages_topic_id, int32 date, Promise<td_api::object_ptr<td_api::message>> &&promise) {
  get_topic_message_by_date(td_->dialog_manager_->get_my_dialog_id(), saved_messages_topic_id, date,
                            std::move(promise));
}

void SavedMessagesManager::get_topic_message_by_date(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                     int32 date,
                                                     Promise<td_api::object_ptr<td_api::message>> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));

  if (date <= 0) {
    date = 1;
  }

  td_->create_handler<GetSavedMessageByDateQuery>(std::move(promise))->send(dialog_id, saved_messages_topic_id, date);
}

void SavedMessagesManager::delete_monoforum_topic_messages_by_date(DialogId dialog_id,
                                                                   SavedMessagesTopicId saved_messages_topic_id,
                                                                   int32 min_date, int32 max_date,
                                                                   Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, get_monoforum_topic_list(dialog_id));
  delete_topic_messages_by_date(dialog_id, saved_messages_topic_id, min_date, max_date, std::move(promise));
}

void SavedMessagesManager::delete_saved_messages_topic_messages_by_date(SavedMessagesTopicId saved_messages_topic_id,
                                                                        int32 min_date, int32 max_date,
                                                                        Promise<Unit> &&promise) {
  delete_topic_messages_by_date(td_->dialog_manager_->get_my_dialog_id(), saved_messages_topic_id, min_date, max_date,
                                std::move(promise));
}

void SavedMessagesManager::delete_topic_messages_by_date(DialogId dialog_id,
                                                         SavedMessagesTopicId saved_messages_topic_id, int32 min_date,
                                                         int32 max_date, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));

  TRY_STATUS_PROMISE(promise, MessagesManager::fix_delete_message_min_max_dates(min_date, max_date));
  if (max_date == 0) {
    return promise.set_value(Unit());
  }

  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list != nullptr) {
    auto *topic = get_topic(topic_list, saved_messages_topic_id);
    if (topic != nullptr) {
      auto message_ids = topic->ordered_messages_.find_messages_by_date(
          min_date, max_date, td_->messages_manager_->get_get_message_date(dialog_id));
      td_->messages_manager_->delete_dialog_messages(dialog_id, message_ids, false,
                                                     MessagesManager::DELETE_MESSAGE_USER_REQUEST_SOURCE);
    }
  }

  MessageQueryManager::AffectedHistoryQuery query = [td = td_, saved_messages_topic_id, min_date, max_date](
                                                        DialogId dialog_id, Promise<AffectedHistory> &&query_promise) {
    td->create_handler<DeleteSavedMessagesByDateQuery>(std::move(query_promise))
        ->send(dialog_id, saved_messages_topic_id, min_date, max_date);
  };
  td_->message_query_manager_->run_affected_history_query_until_complete(dialog_id, std::move(query), true,
                                                                         std::move(promise));
}

int32 SavedMessagesManager::get_pinned_saved_messages_topic_limit() const {
  return clamp(narrow_cast<int32>(td_->option_manager_->get_option_integer("pinned_saved_messages_topic_count_max")), 0,
               1000);
}

void SavedMessagesManager::toggle_saved_messages_topic_is_pinned(SavedMessagesTopicId saved_messages_topic_id,
                                                                 bool is_pinned, Promise<Unit> &&promise) {
  auto dialog_id = td_->dialog_manager_->get_my_dialog_id();
  TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));
  auto *topic_list = &topic_list_;
  if (!topic_list->are_pinned_saved_messages_topics_inited_) {
    return promise.set_error(400, "Pinned Saved Messages topics must be loaded first");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return promise.set_error(400, "Can't find Saved Messages topic");
  }
  if (is_pinned && !td::contains(topic_list->pinned_saved_messages_topic_ids_, saved_messages_topic_id) &&
      static_cast<size_t>(get_pinned_saved_messages_topic_limit()) <=
          topic_list->pinned_saved_messages_topic_ids_.size()) {
    return promise.set_error(400, "The maximum number of pinned chats exceeded");
  }
  if (!set_saved_messages_topic_is_pinned(topic, is_pinned, "toggle_saved_messages_topic_is_pinned")) {
    return promise.set_value(Unit());
  }
  td_->create_handler<ToggleSavedDialogPinQuery>(std::move(promise))->send(saved_messages_topic_id, is_pinned);
}

void SavedMessagesManager::set_pinned_saved_messages_topics(vector<SavedMessagesTopicId> saved_messages_topic_ids,
                                                            Promise<Unit> &&promise) {
  auto dialog_id = td_->dialog_manager_->get_my_dialog_id();
  auto *topic_list = &topic_list_;
  for (const auto &saved_messages_topic_id : saved_messages_topic_ids) {
    TRY_STATUS_PROMISE(promise, saved_messages_topic_id.is_valid_in(td_, dialog_id));
    if (get_topic(topic_list, saved_messages_topic_id) == nullptr) {
      return promise.set_error(400, "Can't find Saved Messages topic");
    }
  }
  if (!topic_list->are_pinned_saved_messages_topics_inited_) {
    return promise.set_error(400, "Pinned Saved Messages topics must be loaded first");
  }
  if (static_cast<size_t>(get_pinned_saved_messages_topic_limit()) < saved_messages_topic_ids.size()) {
    return promise.set_error(400, "The maximum number of pinned chats exceeded");
  }
  if (!set_pinned_saved_messages_topics(saved_messages_topic_ids)) {
    return promise.set_value(Unit());
  }
  td_->create_handler<ReorderPinnedSavedDialogsQuery>(std::move(promise))->send(std::move(saved_messages_topic_ids));
}

void SavedMessagesManager::reload_pinned_saved_messages_topics() {
  if (td_->auth_manager_->is_bot()) {
    // just in case
    return;
  }
  if (!topic_list_.are_pinned_saved_messages_topics_inited_) {
    return;
  }

  get_pinned_saved_dialogs(0, Auto());
}

void SavedMessagesManager::set_monoforum_topic_is_marked_as_unread(DialogId dialog_id,
                                                                   SavedMessagesTopicId saved_messages_topic_id,
                                                                   bool is_marked_as_unread, Promise<Unit> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  if (topic->dialog_id_ != dialog_id) {
    return promise.set_error(400, "Topic can't be marked as unread");
  }

  do_set_topic_is_marked_as_unread(topic, is_marked_as_unread);

  if (topic->is_changed_) {
    td_->dialog_manager_->toggle_dialog_is_marked_as_unread_on_server(dialog_id, saved_messages_topic_id,
                                                                      is_marked_as_unread, 0);
    on_topic_changed(topic_list, topic, "set_monoforum_topic_is_marked_as_unread");
  }
  promise.set_value(Unit());
}

Status SavedMessagesManager::set_monoforum_topic_draft_message(
    DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
    td_api::object_ptr<td_api::draftMessage> &&draft_message) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return Status::Error(400, "Topic not found");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return Status::Error(400, "Topic not found");
  }
  if (topic->dialog_id_ != dialog_id) {
    return Status::Error(400, "Topic draft can't be changed");
  }

  TRY_RESULT(new_draft_message, DraftMessage::get_draft_message(td_, dialog_id, MessageId(), std::move(draft_message)));

  do_set_topic_draft_message(topic, std::move(new_draft_message), false);

  if (topic->is_changed_) {
    if (!is_local_draft_message(topic->draft_message_)) {
      save_draft_message(td_, dialog_id, saved_messages_topic_id, topic->draft_message_, Auto());
    }
    on_topic_changed(topic_list, topic, "set_monoforum_topic_draft_message");
  }
  return Status::OK();
}

void SavedMessagesManager::unpin_all_monoforum_topic_messages(DialogId dialog_id,
                                                              SavedMessagesTopicId saved_messages_topic_id,
                                                              Promise<Unit> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  if (topic->dialog_id_ != dialog_id) {
    return promise.set_error(400, "Topic messages can't be unpinned");
  }

  td_->messages_manager_->unpin_all_local_dialog_messages(dialog_id, MessageId(), saved_messages_topic_id);

  td_->message_query_manager_->unpin_all_topic_messages_on_server(dialog_id, MessageId(), saved_messages_topic_id, 0,
                                                                  std::move(promise));
}

void SavedMessagesManager::read_all_monoforum_topic_reactions(DialogId dialog_id,
                                                              SavedMessagesTopicId saved_messages_topic_id,
                                                              Promise<Unit> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  if (topic->dialog_id_ != dialog_id) {
    return promise.set_error(400, "Topic messages can't have reactions");
  }

  td_->messages_manager_->read_all_local_dialog_reactions(dialog_id, MessageId(), saved_messages_topic_id);

  do_set_topic_unread_reaction_count(topic, 0);
  if (!topic->is_changed_) {
    return promise.set_value(Unit());
  }

  td_->message_query_manager_->read_all_topic_reactions_on_server(dialog_id, MessageId(), saved_messages_topic_id, 0,
                                                                  std::move(promise));

  on_topic_changed(topic_list, topic, "read_all_monoforum_topic_reactions");
}

void SavedMessagesManager::get_monoforum_topic_revenue(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                       Promise<td_api::object_ptr<td_api::starCount>> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  if (topic->dialog_id_ != dialog_id) {
    return promise.set_error(400, "Topic messages can't be paid");
  }
  TRY_RESULT_PROMISE(promise, input_user, saved_messages_topic_id.get_input_user(td_));
  td_->create_handler<GetMonoforumPaidMessageRevenueQuery>(std::move(promise))->send(dialog_id, std::move(input_user));
}

void SavedMessagesManager::toggle_monoforum_topic_nopaid_messages_exception(
    DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, bool nopaid_messages_exception,
    bool refund_payments, Promise<Unit> &&promise) {
  auto *topic_list = get_topic_list(dialog_id);
  if (topic_list == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  auto *topic = get_topic(topic_list, saved_messages_topic_id);
  if (topic == nullptr) {
    return promise.set_error(400, "Topic not found");
  }
  if (topic->dialog_id_ != dialog_id) {
    return promise.set_error(400, "Topic messages can't be paid");
  }
  TRY_RESULT_PROMISE(promise, input_user, saved_messages_topic_id.get_input_user(td_));

  do_set_topic_nopaid_messages_exception(topic, nopaid_messages_exception);
  if (!topic->is_changed_ && !refund_payments) {
    return promise.set_value(Unit());
  }
  on_topic_changed(topic_list, topic, "read_all_monoforum_topic_reactions");

  td_->create_handler<AddMonoforumNoPaidMessageExceptionQuery>(std::move(promise))
      ->send(dialog_id, std::move(input_user), !nopaid_messages_exception, refund_payments);
}

void SavedMessagesManager::get_monoforum_message_author(MessageFullId message_full_id,
                                                        Promise<td_api::object_ptr<td_api::user>> &&promise) {
  auto dialog_id = message_full_id.get_dialog_id();
  TRY_STATUS_PROMISE(promise, check_monoforum_dialog_id(dialog_id));
  if (!td_->messages_manager_->have_message_force(message_full_id, "get_monoforum_message_author")) {
    return promise.set_error(400, "Message not found");
  }
  auto message_id = message_full_id.get_message_id();
  if (!message_id.is_server()) {
    return promise.set_error(400, "Can't get message author");
  }

  td_->create_handler<GetMessageAuthorQuery>(std::move(promise))->send(dialog_id.get_channel_id(), message_id);
}

void SavedMessagesManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (topic_list_.sent_total_count_ != -1) {
    updates.push_back(get_update_saved_messages_topic_count_object());
  }

  for (const auto &it : topic_list_.topics_) {
    const auto *topic = it.second.get();
    updates.push_back(get_update_saved_messages_topic_object(topic));
  }

  for (const auto &dialog_it : monoforum_topic_lists_) {
    const auto *topic_list = dialog_it.second.get();
    for (const auto &it : topic_list->topics_) {
      const auto *topic = it.second.get();
      updates.push_back(get_update_direct_messages_chat_topic_object(topic_list, topic));
      if (topic->sent_message_count_ >= 0) {
        updates.push_back(get_update_topic_message_count_object(topic));
      }
    }
  }
}

const SavedMessagesManager::TopicDate SavedMessagesManager::MIN_TOPIC_DATE{std::numeric_limits<int64>::max(),
                                                                           SavedMessagesTopicId()};
const SavedMessagesManager::TopicDate SavedMessagesManager::MAX_TOPIC_DATE{0, SavedMessagesTopicId()};

}  // namespace td
