//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageQueryManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/Global.h"
#include "td/telegram/HashtagHints.h"
#include "td/telegram/MessageSearchOffset.h"
#include "td/telegram/MessagesInfo.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

namespace td {

class ReportMessageDeliveryQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(MessageFullId message_full_id, bool from_push) {
    dialog_id_ = message_full_id.get_dialog_id();
    auto input_peer = td_->dialog_manager_->get_input_peer(message_full_id.get_dialog_id(), AccessRights::Read);
    if (input_peer == nullptr) {
      return;
    }
    int32 flags = 0;
    if (from_push) {
      flags |= telegram_api::messages_reportMessagesDelivery::PUSH_MASK;
    }
    auto message_id = message_full_id.get_message_id();
    CHECK(message_id.is_valid());
    CHECK(message_id.is_server());
    send_query(G()->net_query_creator().create(telegram_api::messages_reportMessagesDelivery(
        flags, false /*ignored*/, std::move(input_peer), {message_id.get_server_message_id().get()})));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_reportMessagesDelivery>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    // ok
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "ReportMessageDeliveryQuery");
  }
};

class SearchMessagesGlobalQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundMessages>> promise_;
  string query_;
  int32 offset_date_;
  DialogId offset_dialog_id_;
  MessageId offset_message_id_;
  int32 limit_;
  MessageSearchFilter filter_;
  int32 min_date_;
  int32 max_date_;

 public:
  explicit SearchMessagesGlobalQuery(Promise<td_api::object_ptr<td_api::foundMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(FolderId folder_id, bool ignore_folder_id, const string &query, int32 offset_date,
            DialogId offset_dialog_id, MessageId offset_message_id, int32 limit, MessageSearchFilter filter,
            const td_api::object_ptr<td_api::SearchMessagesChatTypeFilter> &dialog_type_filter, int32 min_date,
            int32 max_date) {
    query_ = query;
    offset_date_ = offset_date;
    offset_dialog_id_ = offset_dialog_id;
    offset_message_id_ = offset_message_id;
    limit_ = limit;
    filter_ = filter;
    min_date_ = min_date;
    max_date_ = max_date;

    auto input_peer = DialogManager::get_input_peer_force(offset_dialog_id);
    CHECK(input_peer != nullptr);

    int32 flags = 0;
    if (!ignore_folder_id) {
      flags |= telegram_api::messages_searchGlobal::FOLDER_ID_MASK;
    }
    if (dialog_type_filter != nullptr) {
      switch (dialog_type_filter->get_id()) {
        case td_api::searchMessagesChatTypeFilterPrivate::ID:
          flags |= telegram_api::messages_searchGlobal::USERS_ONLY_MASK;
          break;
        case td_api::searchMessagesChatTypeFilterGroup::ID:
          flags |= telegram_api::messages_searchGlobal::GROUPS_ONLY_MASK;
          break;
        case td_api::searchMessagesChatTypeFilterChannel::ID:
          flags |= telegram_api::messages_searchGlobal::BROADCASTS_ONLY_MASK;
          break;
        default:
          UNREACHABLE();
      }
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_searchGlobal(
        flags, false /*ignored*/, false /*ignored*/, false /*ignored*/, folder_id.get(), query,
        get_input_messages_filter(filter), min_date_, max_date_, offset_date_, std::move(input_peer),
        offset_message_id.get_server_message_id().get(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_searchGlobal>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, DialogId(), result_ptr.move_as_ok(), "SearchMessagesGlobalQuery");
    td_->messages_manager_->get_channel_differences_if_needed(
        std::move(info),
        PromiseCreator::lambda([actor_id = td_->message_query_manager_actor_.get(), query = std::move(query_),
                                offset_date = offset_date_, offset_dialog_id = offset_dialog_id_,
                                offset_message_id = offset_message_id_, limit = limit_, filter = std::move(filter_),
                                min_date = min_date_, max_date = max_date_,
                                promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &MessageQueryManager::on_get_messages_search_result, query, offset_date,
                         offset_dialog_id, offset_message_id, limit, filter, min_date, max_date, info.total_count,
                         std::move(info.messages), info.next_rate, std::move(promise));
          }
        }),
        "SearchMessagesGlobalQuery");
  }

  void on_error(Status status) final {
    if (status.message() == "SEARCH_QUERY_EMPTY") {
      return promise_.set_value(td_->messages_manager_->get_found_messages_object({}, "SearchMessagesGlobalQuery"));
    }
    promise_.set_error(std::move(status));
  }
};

class SearchSentMediaQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundMessages>> promise_;

 public:
  explicit SearchSentMediaQuery(Promise<td_api::object_ptr<td_api::foundMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &query, int32 limit) {
    send_query(G()->net_query_creator().create(telegram_api::messages_searchSentMedia(
        query, telegram_api::make_object<telegram_api::inputMessagesFilterDocument>(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_searchSentMedia>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, DialogId(), result_ptr.move_as_ok(), "SearchSentMediaQuery");
    td_->messages_manager_->get_channel_differences_if_needed(
        std::move(info),
        PromiseCreator::lambda([actor_id = td_->message_query_manager_actor_.get(),
                                promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &MessageQueryManager::on_get_outgoing_document_messages, std::move(info.messages),
                         std::move(promise));
          }
        }),
        "SearchSentMediaQuery");
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SearchPostsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::foundMessages>> promise_;
  string hashtag_;
  MessageSearchOffset offset_;
  int32 limit_;

 public:
  explicit SearchPostsQuery(Promise<td_api::object_ptr<td_api::foundMessages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const string &hashtag, MessageSearchOffset offset, int32 limit) {
    hashtag_ = hashtag;
    offset_ = offset;
    limit_ = limit;

    auto input_peer = DialogManager::get_input_peer_force(offset.dialog_id_);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(telegram_api::channels_searchPosts(
        hashtag, offset.date_, std::move(input_peer), offset.message_id_.get_server_message_id().get(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_searchPosts>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, DialogId(), result_ptr.move_as_ok(), "SearchPostsQuery");
    td_->messages_manager_->get_channel_differences_if_needed(
        std::move(info),
        PromiseCreator::lambda([actor_id = td_->message_query_manager_actor_.get(), hashtag = std::move(hashtag_),
                                offset = offset_, limit = limit_,
                                promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &MessageQueryManager::on_get_hashtag_search_result, hashtag, offset, limit,
                         info.total_count, std::move(info.messages), info.next_rate, std::move(promise));
          }
        }),
        "SearchPostsQuery");
  }

  void on_error(Status status) final {
    if (status.message() == "SEARCH_QUERY_EMPTY") {
      return promise_.set_value(td_->messages_manager_->get_found_messages_object({}, "SearchPostsQuery"));
    }
    promise_.set_error(std::move(status));
  }
};

class GetRecentLocationsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::messages>> promise_;
  DialogId dialog_id_;
  int32 limit_;

 public:
  explicit GetRecentLocationsQuery(Promise<td_api::object_ptr<td_api::messages>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, int32 limit) {
    dialog_id_ = dialog_id;
    limit_ = limit;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(
        G()->net_query_creator().create(telegram_api::messages_getRecentLocations(std::move(input_peer), limit, 0)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getRecentLocations>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto info = get_messages_info(td_, dialog_id_, result_ptr.move_as_ok(), "GetRecentLocationsQuery");
    td_->messages_manager_->get_channel_difference_if_needed(
        dialog_id_, std::move(info),
        PromiseCreator::lambda([actor_id = td_->message_query_manager_actor_.get(), dialog_id = dialog_id_,
                                limit = limit_, promise = std::move(promise_)](Result<MessagesInfo> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            auto info = result.move_as_ok();
            send_closure(actor_id, &MessageQueryManager::on_get_recent_locations, dialog_id, limit, info.total_count,
                         std::move(info.messages), std::move(promise));
          }
        }),
        "GetRecentLocationsQuery");
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetRecentLocationsQuery");
    promise_.set_error(std::move(status));
  }
};

MessageQueryManager::MessageQueryManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void MessageQueryManager::tear_down() {
  parent_.reset();
}

void MessageQueryManager::report_message_delivery(MessageFullId message_full_id, int32 until_date, bool from_push) {
  if (G()->unix_time() > until_date) {
    return;
  }
  td_->create_handler<ReportMessageDeliveryQuery>()->send(message_full_id, from_push);
}

void MessageQueryManager::search_messages(DialogListId dialog_list_id, bool ignore_folder_id, const string &query,
                                          const string &offset_str, int32 limit, MessageSearchFilter filter,
                                          td_api::object_ptr<td_api::SearchMessagesChatTypeFilter> &&dialog_type_filter,
                                          int32 min_date, int32 max_date,
                                          Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  if (!dialog_list_id.is_folder()) {
    return promise.set_error(Status::Error(400, "Wrong chat list specified"));
  }
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }
  if (limit > MAX_SEARCH_MESSAGES) {
    limit = MAX_SEARCH_MESSAGES;
  }

  TRY_RESULT_PROMISE(promise, offset, MessageSearchOffset::from_string(offset_str));

  CHECK(filter != MessageSearchFilter::Call && filter != MessageSearchFilter::MissedCall);
  if (filter == MessageSearchFilter::Mention || filter == MessageSearchFilter::UnreadMention ||
      filter == MessageSearchFilter::UnreadReaction || filter == MessageSearchFilter::FailedToSend ||
      filter == MessageSearchFilter::Pinned) {
    return promise.set_error(Status::Error(400, "The filter is not supported"));
  }

  if (query.empty() && filter == MessageSearchFilter::Empty) {
    return promise.set_value(td_->messages_manager_->get_found_messages_object({}, "search_messages"));
  }

  td_->create_handler<SearchMessagesGlobalQuery>(std::move(promise))
      ->send(dialog_list_id.get_folder_id(), ignore_folder_id, query, offset.date_, offset.dialog_id_,
             offset.message_id_, limit, filter, dialog_type_filter, min_date, max_date);
}

void MessageQueryManager::on_get_messages_search_result(
    const string &query, int32 offset_date, DialogId offset_dialog_id, MessageId offset_message_id, int32 limit,
    MessageSearchFilter filter, int32 min_date, int32 max_date, int32 total_count,
    vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, int32 next_rate,
    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Receive " << messages.size() << " found messages";

  MessagesManager::FoundMessages found_messages;
  auto &result = found_messages.message_full_ids;
  MessageSearchOffset next_offset;
  for (auto &message : messages) {
    next_offset.update_from_message(message);

    bool is_channel_message = DialogId::get_message_dialog_id(message).get_type() == DialogType::Channel;
    auto new_message_full_id =
        td_->messages_manager_->on_get_message(std::move(message), false, is_channel_message, false, "search messages");
    if (new_message_full_id != MessageFullId()) {
      result.push_back(new_message_full_id);
    } else {
      total_count--;
    }
  }
  if (total_count < static_cast<int32>(result.size())) {
    LOG(ERROR) << "Receive " << result.size() << " valid messages out of " << total_count << " in " << messages.size()
               << " messages";
    total_count = static_cast<int32>(result.size());
  }
  found_messages.total_count = total_count;
  if (!result.empty()) {
    if (next_rate > 0) {
      next_offset.date_ = next_rate;
    }
    found_messages.next_offset = next_offset.to_string();
  }
  promise.set_value(td_->messages_manager_->get_found_messages_object(found_messages, "on_get_messages_search_result"));
}

void MessageQueryManager::search_outgoing_document_messages(
    const string &query, int32 limit, Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }
  if (limit > MAX_SEARCH_MESSAGES) {
    limit = MAX_SEARCH_MESSAGES;
  }

  td_->create_handler<SearchSentMediaQuery>(std::move(promise))->send(query, limit);
}

void MessageQueryManager::on_get_outgoing_document_messages(
    vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  MessagesManager::FoundMessages found_messages;
  for (auto &message : messages) {
    auto dialog_id = DialogId::get_message_dialog_id(message);
    auto message_full_id =
        td_->messages_manager_->on_get_message(std::move(message), false, dialog_id.get_type() == DialogType::Channel,
                                               false, "on_get_outgoing_document_messages");
    if (message_full_id != MessageFullId()) {
      CHECK(dialog_id == message_full_id.get_dialog_id());
      found_messages.message_full_ids.push_back(message_full_id);
    }
  }
  auto result = td_->messages_manager_->get_found_messages_object(found_messages, "on_get_outgoing_document_messages");
  td::remove_if(result->messages_,
                [](const auto &message) { return message->content_->get_id() != td_api::messageDocument::ID; });
  result->total_count_ = narrow_cast<int32>(result->messages_.size());
  promise.set_value(std::move(result));
}

void MessageQueryManager::search_hashtag_posts(string hashtag, string offset_str, int32 limit,
                                               Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }
  if (limit > MAX_SEARCH_MESSAGES) {
    limit = MAX_SEARCH_MESSAGES;
  }

  TRY_RESULT_PROMISE(promise, offset, MessageSearchOffset::from_string(offset_str));

  bool is_cashtag = false;
  if (hashtag[0] == '#' || hashtag[0] == '$') {
    is_cashtag = (hashtag[0] == '$');
    hashtag = hashtag.substr(1);
  }
  if (hashtag.empty()) {
    return promise.set_value(td_->messages_manager_->get_found_messages_object({}, "search_hashtag_posts"));
  }
  send_closure(is_cashtag ? td_->cashtag_search_hints_ : td_->hashtag_search_hints_, &HashtagHints::hashtag_used,
               hashtag);

  td_->create_handler<SearchPostsQuery>(std::move(promise))
      ->send(PSTRING() << (is_cashtag ? '$' : '#') << hashtag, offset, limit);
}

void MessageQueryManager::on_get_hashtag_search_result(
    const string &hashtag, const MessageSearchOffset &old_offset, int32 limit, int32 total_count,
    vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, int32 next_rate,
    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  MessagesManager::FoundMessages found_messages;
  auto &result = found_messages.message_full_ids;
  MessageSearchOffset next_offset;
  for (auto &message : messages) {
    next_offset.update_from_message(message);

    auto new_message_full_id =
        td_->messages_manager_->on_get_message(std::move(message), false, true, false, "search hashtag");
    if (new_message_full_id != MessageFullId()) {
      result.push_back(new_message_full_id);
    } else {
      total_count--;
    }
  }
  if (total_count < static_cast<int32>(result.size())) {
    LOG(ERROR) << "Receive " << result.size() << " valid messages out of " << total_count << " in " << messages.size()
               << " messages";
    total_count = static_cast<int32>(result.size());
  }
  found_messages.total_count = total_count;
  if (!result.empty()) {
    if (next_rate > 0) {
      next_offset.date_ = next_rate;
    }
    found_messages.next_offset = next_offset.to_string();
  }
  promise.set_value(td_->messages_manager_->get_found_messages_object(found_messages, "on_get_hashtag_search_result"));
}

void MessageQueryManager::search_dialog_recent_location_messages(
    DialogId dialog_id, int32 limit, Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  LOG(INFO) << "Search recent location messages in " << dialog_id << " with limit " << limit;

  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }
  if (limit > MAX_SEARCH_MESSAGES) {
    limit = MAX_SEARCH_MESSAGES;
  }

  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, true, AccessRights::Read,
                                                                        "search_dialog_recent_location_messages"));

  switch (dialog_id.get_type()) {
    case DialogType::User:
    case DialogType::Chat:
    case DialogType::Channel:
      return td_->create_handler<GetRecentLocationsQuery>(std::move(promise))->send(dialog_id, limit);
    case DialogType::SecretChat:
      return promise.set_value(td_->messages_manager_->get_messages_object(0, dialog_id, {}, false,
                                                                           "search_dialog_recent_location_messages"));
    default:
      UNREACHABLE();
      promise.set_error(Status::Error(500, "Message search is not supported"));
  }
}

void MessageQueryManager::on_get_recent_locations(DialogId dialog_id, int32 limit, int32 total_count,
                                                  vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
                                                  Promise<td_api::object_ptr<td_api::messages>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Receive " << messages.size() << " recent locations in " << dialog_id;
  vector<MessageId> message_ids;
  for (auto &message : messages) {
    auto new_message_full_id = td_->messages_manager_->on_get_message(
        std::move(message), false, dialog_id.get_type() == DialogType::Channel, false, "get recent locations");
    if (new_message_full_id != MessageFullId()) {
      if (new_message_full_id.get_dialog_id() != dialog_id) {
        LOG(ERROR) << "Receive " << new_message_full_id << " instead of a message in " << dialog_id;
        total_count--;
        continue;
      }

      message_ids.push_back(new_message_full_id.get_message_id());
    } else {
      total_count--;
    }
  }
  if (total_count < static_cast<int32>(message_ids.size())) {
    LOG(ERROR) << "Receive " << message_ids.size() << " valid messages out of " << total_count << " in "
               << messages.size() << " messages";
    total_count = static_cast<int32>(message_ids.size());
  }
  auto result =
      td_->messages_manager_->get_messages_object(total_count, dialog_id, message_ids, true, "on_get_recent_locations");
  td::remove_if(result->messages_, [&](const auto &message) {
    if (message->content_->get_id() != td_api::messageLocation::ID ||
        static_cast<const td_api::messageLocation *>(message->content_.get())->live_period_ <= 0) {
      result->total_count_--;
      return true;
    }
    return false;
  });

  promise.set_value(std::move(result));
}

}  // namespace td
