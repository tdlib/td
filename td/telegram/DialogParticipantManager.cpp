//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogParticipantManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

namespace td {

class GetOnlinesQuery final : public Td::ResultHandler {
  DialogId dialog_id_;

 public:
  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;
    CHECK(dialog_id.get_type() == DialogType::Channel);
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    send_query(G()->net_query_creator().create(telegram_api::messages_getOnlines(std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getOnlines>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    td_->dialog_participant_manager_->on_update_dialog_online_member_count(dialog_id_, result->onlines_, true);
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetOnlinesQuery");
    td_->dialog_participant_manager_->on_update_dialog_online_member_count(dialog_id_, 0, true);
  }
};

class GetChatJoinRequestsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::chatJoinRequests>> promise_;
  DialogId dialog_id_;
  bool is_full_list_ = false;

 public:
  explicit GetChatJoinRequestsQuery(Promise<td_api::object_ptr<td_api::chatJoinRequests>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const string &invite_link, const string &query, int32 offset_date,
            UserId offset_user_id, int32 limit) {
    dialog_id_ = dialog_id;
    is_full_list_ =
        invite_link.empty() && query.empty() && offset_date == 0 && !offset_user_id.is_valid() && limit >= 3;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    auto r_input_user = td_->contacts_manager_->get_input_user(offset_user_id);
    if (r_input_user.is_error()) {
      r_input_user = make_tl_object<telegram_api::inputUserEmpty>();
    }

    int32 flags = telegram_api::messages_getChatInviteImporters::REQUESTED_MASK;
    if (!invite_link.empty()) {
      flags |= telegram_api::messages_getChatInviteImporters::LINK_MASK;
    }
    if (!query.empty()) {
      flags |= telegram_api::messages_getChatInviteImporters::Q_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_getChatInviteImporters(flags, false /*ignored*/, std::move(input_peer), invite_link,
                                                      query, offset_date, r_input_user.move_as_ok(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getChatInviteImporters>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetChatJoinRequestsQuery: " << to_string(result);

    td_->contacts_manager_->on_get_users(std::move(result->users_), "GetChatJoinRequestsQuery");

    int32 total_count = result->count_;
    if (total_count < static_cast<int32>(result->importers_.size())) {
      LOG(ERROR) << "Receive wrong total count of join requests " << total_count << " in " << dialog_id_;
      total_count = static_cast<int32>(result->importers_.size());
    }
    vector<td_api::object_ptr<td_api::chatJoinRequest>> join_requests;
    vector<int64> recent_requesters;
    for (auto &request : result->importers_) {
      UserId user_id(request->user_id_);
      UserId approver_user_id(request->approved_by_);
      if (!user_id.is_valid() || approver_user_id.is_valid() || !request->requested_) {
        LOG(ERROR) << "Receive invalid join request: " << to_string(request);
        total_count--;
        continue;
      }
      if (recent_requesters.size() < 3) {
        recent_requesters.push_back(user_id.get());
      }
      join_requests.push_back(td_api::make_object<td_api::chatJoinRequest>(
          td_->contacts_manager_->get_user_id_object(user_id, "chatJoinRequest"), request->date_, request->about_));
    }
    if (is_full_list_) {
      td_->messages_manager_->on_update_dialog_pending_join_requests(dialog_id_, total_count,
                                                                     std::move(recent_requesters));
    }
    promise_.set_value(td_api::make_object<td_api::chatJoinRequests>(total_count, std::move(join_requests)));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetChatJoinRequestsQuery");
    promise_.set_error(std::move(status));
  }
};

class HideChatJoinRequestQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit HideChatJoinRequestQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, UserId user_id, bool approve) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    TRY_RESULT_PROMISE(promise_, input_user, td_->contacts_manager_->get_input_user(user_id));

    int32 flags = 0;
    if (approve) {
      flags |= telegram_api::messages_hideChatJoinRequest::APPROVED_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::messages_hideChatJoinRequest(
        flags, false /*ignored*/, std::move(input_peer), std::move(input_user))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_hideChatJoinRequest>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for HideChatJoinRequestQuery: " << to_string(result);
    td_->updates_manager_->on_get_updates(std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "HideChatJoinRequestQuery");
    promise_.set_error(std::move(status));
  }
};

class HideAllChatJoinRequestsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit HideAllChatJoinRequestsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const string &invite_link, bool approve) {
    dialog_id_ = dialog_id;
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Write);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (approve) {
      flags |= telegram_api::messages_hideAllChatJoinRequests::APPROVED_MASK;
    }
    if (!invite_link.empty()) {
      flags |= telegram_api::messages_hideAllChatJoinRequests::LINK_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::messages_hideAllChatJoinRequests(flags, false /*ignored*/, std::move(input_peer), invite_link)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_hideAllChatJoinRequests>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for HideAllChatJoinRequestsQuery: " << to_string(result);
    td_->updates_manager_->on_get_updates(std::move(result), std::move(promise_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "HideAllChatJoinRequestsQuery");
    promise_.set_error(std::move(status));
  }
};

DialogParticipantManager::DialogParticipantManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  update_dialog_online_member_count_timeout_.set_callback(on_update_dialog_online_member_count_timeout_callback);
  update_dialog_online_member_count_timeout_.set_callback_data(static_cast<void *>(this));
}

void DialogParticipantManager::tear_down() {
  parent_.reset();
}

void DialogParticipantManager::on_update_dialog_online_member_count_timeout_callback(
    void *dialog_participant_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto dialog_participant_manager = static_cast<DialogParticipantManager *>(dialog_participant_manager_ptr);
  send_closure_later(dialog_participant_manager->actor_id(dialog_participant_manager),
                     &DialogParticipantManager::on_update_dialog_online_member_count_timeout, DialogId(dialog_id_int));
}

void DialogParticipantManager::on_update_dialog_online_member_count_timeout(DialogId dialog_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Expired timeout for number of online members in " << dialog_id;
  bool is_open = td_->messages_manager_->is_dialog_opened(dialog_id);
  if (!is_open) {
    send_update_chat_online_member_count(dialog_id, 0);
    return;
  }

  if (dialog_id.get_type() == DialogType::Channel && !td_->dialog_manager_->is_broadcast_channel(dialog_id)) {
    auto participant_count = td_->contacts_manager_->get_channel_participant_count(dialog_id.get_channel_id());
    auto has_hidden_participants = td_->contacts_manager_->get_channel_effective_has_hidden_participants(
        dialog_id.get_channel_id(), "on_update_dialog_online_member_count_timeout");
    if (participant_count == 0 || participant_count >= 195 || has_hidden_participants) {
      td_->create_handler<GetOnlinesQuery>()->send(dialog_id);
    } else {
      td_->contacts_manager_->get_channel_participants(dialog_id.get_channel_id(),
                                                       td_api::make_object<td_api::supergroupMembersFilterRecent>(),
                                                       string(), 0, 200, 200, Auto());
    }
    return;
  }
  if (dialog_id.get_type() == DialogType::Chat) {
    // we need actual online status state, so we need to reget chat participants
    td_->contacts_manager_->repair_chat_participants(dialog_id.get_chat_id());
    return;
  }
}

void DialogParticipantManager::on_update_dialog_online_member_count(DialogId dialog_id, int32 online_member_count,
                                                                    bool is_from_server) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (!dialog_id.is_valid()) {
    LOG(ERROR) << "Receive number of online members in invalid " << dialog_id;
    return;
  }

  if (td_->dialog_manager_->is_broadcast_channel(dialog_id)) {
    LOG_IF(ERROR, online_member_count != 0)
        << "Receive " << online_member_count << " as a number of online members in a channel " << dialog_id;
    return;
  }

  if (online_member_count < 0) {
    LOG(ERROR) << "Receive " << online_member_count << " as a number of online members in a " << dialog_id;
    return;
  }

  set_dialog_online_member_count(dialog_id, online_member_count, is_from_server,
                                 "on_update_channel_online_member_count");
}

void DialogParticipantManager::on_dialog_opened(DialogId dialog_id) {
  auto online_count_it = dialog_online_member_counts_.find(dialog_id);
  if (online_count_it == dialog_online_member_counts_.end()) {
    return;
  }
  auto &info = online_count_it->second;
  CHECK(!info.is_update_sent);
  if (Time::now() - info.update_time < ONLINE_MEMBER_COUNT_CACHE_EXPIRE_TIME) {
    info.is_update_sent = true;
    send_update_chat_online_member_count(dialog_id, info.online_member_count);
  }
}

void DialogParticipantManager::on_dialog_closed(DialogId dialog_id) {
  auto online_count_it = dialog_online_member_counts_.find(dialog_id);
  if (online_count_it != dialog_online_member_counts_.end()) {
    auto &info = online_count_it->second;
    info.is_update_sent = false;
  }
  update_dialog_online_member_count_timeout_.set_timeout_in(dialog_id.get(), ONLINE_MEMBER_COUNT_CACHE_EXPIRE_TIME);
}

void DialogParticipantManager::set_dialog_online_member_count(DialogId dialog_id, int32 online_member_count,
                                                              bool is_from_server, const char *source) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  if (online_member_count < 0) {
    LOG(ERROR) << "Receive online_member_count = " << online_member_count << " in " << dialog_id;
    online_member_count = 0;
  }

  switch (dialog_id.get_type()) {
    case DialogType::Chat: {
      auto participant_count = td_->contacts_manager_->get_chat_participant_count(dialog_id.get_chat_id());
      if (online_member_count > participant_count) {
        online_member_count = participant_count;
      }
      break;
    }
    case DialogType::Channel: {
      auto participant_count = td_->contacts_manager_->get_channel_participant_count(dialog_id.get_channel_id());
      if (participant_count != 0 && online_member_count > participant_count) {
        online_member_count = participant_count;
      }
      break;
    }
    default:
      break;
  }

  bool is_open = td_->messages_manager_->is_dialog_opened(dialog_id);
  auto &info = dialog_online_member_counts_[dialog_id];
  LOG(INFO) << "Change number of online members from " << info.online_member_count << " to " << online_member_count
            << " in " << dialog_id << " from " << source;
  bool need_update = is_open && (!info.is_update_sent || info.online_member_count != online_member_count);
  info.online_member_count = online_member_count;
  info.update_time = Time::now();

  if (need_update) {
    info.is_update_sent = true;
    send_update_chat_online_member_count(dialog_id, online_member_count);
  }
  if (is_open) {
    if (is_from_server) {
      update_dialog_online_member_count_timeout_.set_timeout_in(dialog_id.get(), ONLINE_MEMBER_COUNT_UPDATE_TIME);
    } else {
      update_dialog_online_member_count_timeout_.add_timeout_in(dialog_id.get(), ONLINE_MEMBER_COUNT_UPDATE_TIME);
    }
  }
}

void DialogParticipantManager::send_update_chat_online_member_count(DialogId dialog_id,
                                                                    int32 online_member_count) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  send_closure(
      G()->td(), &Td::send_update,
      td_api::make_object<td_api::updateChatOnlineMemberCount>(
          td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatOnlineMemberCount"), online_member_count));
}

void DialogParticipantManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  for (const auto &it : dialog_online_member_counts_) {
    auto dialog_id = it.first;
    if (it.second.is_update_sent && td_->messages_manager_->is_dialog_opened(dialog_id)) {
      updates.push_back(td_api::make_object<td_api::updateChatOnlineMemberCount>(
          td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatOnlineMemberCount"),
          it.second.online_member_count));
    }
  }
}

Status DialogParticipantManager::can_manage_dialog_join_requests(DialogId dialog_id) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "can_manage_dialog_join_requests")) {
    return Status::Error(400, "Chat not found");
  }

  switch (dialog_id.get_type()) {
    case DialogType::SecretChat:
    case DialogType::User:
      return Status::Error(400, "The chat can't have join requests");
    case DialogType::Chat: {
      auto chat_id = dialog_id.get_chat_id();
      if (!td_->contacts_manager_->get_chat_is_active(chat_id)) {
        return Status::Error(400, "Chat is deactivated");
      }
      if (!td_->contacts_manager_->get_chat_status(chat_id).can_manage_invite_links()) {
        return Status::Error(400, "Not enough rights to manage chat join requests");
      }
      break;
    }
    case DialogType::Channel:
      if (!td_->contacts_manager_->get_channel_status(dialog_id.get_channel_id()).can_manage_invite_links()) {
        return Status::Error(400, "Not enough rights to manage chat join requests");
      }
      break;
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  return Status::OK();
}

void DialogParticipantManager::get_dialog_join_requests(
    DialogId dialog_id, const string &invite_link, const string &query,
    td_api::object_ptr<td_api::chatJoinRequest> offset_request, int32 limit,
    Promise<td_api::object_ptr<td_api::chatJoinRequests>> &&promise) {
  TRY_STATUS_PROMISE(promise, can_manage_dialog_join_requests(dialog_id));

  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  UserId offset_user_id;
  int32 offset_date = 0;
  if (offset_request != nullptr) {
    offset_user_id = UserId(offset_request->user_id_);
    offset_date = offset_request->date_;
  }

  td_->create_handler<GetChatJoinRequestsQuery>(std::move(promise))
      ->send(dialog_id, invite_link, query, offset_date, offset_user_id, limit);
}

void DialogParticipantManager::process_dialog_join_request(DialogId dialog_id, UserId user_id, bool approve,
                                                           Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, can_manage_dialog_join_requests(dialog_id));
  td_->create_handler<HideChatJoinRequestQuery>(std::move(promise))->send(dialog_id, user_id, approve);
}

void DialogParticipantManager::process_dialog_join_requests(DialogId dialog_id, const string &invite_link, bool approve,
                                                            Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, can_manage_dialog_join_requests(dialog_id));
  td_->create_handler<HideAllChatJoinRequestsQuery>(std::move(promise))->send(dialog_id, invite_link, approve);
}

}  // namespace td
