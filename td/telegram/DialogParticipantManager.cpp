//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogParticipantManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"

#include "td/db/SqliteKeyValueAsync.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <limits>

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

class GetChannelAdministratorsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit GetChannelAdministratorsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, int64 hash) {
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(400, "Supergroup not found"));
    }

    hash = 0;  // to load even only ranks or creator changed

    channel_id_ = channel_id;
    send_query(G()->net_query_creator().create(telegram_api::channels_getParticipants(
        std::move(input_channel), telegram_api::make_object<telegram_api::channelParticipantsAdmins>(), 0,
        std::numeric_limits<int32>::max(), hash)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto participants_ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetChannelAdministratorsQuery: " << to_string(participants_ptr);
    switch (participants_ptr->get_id()) {
      case telegram_api::channels_channelParticipants::ID: {
        auto participants = telegram_api::move_object_as<telegram_api::channels_channelParticipants>(participants_ptr);
        td_->contacts_manager_->on_get_users(std::move(participants->users_), "GetChannelAdministratorsQuery");
        td_->contacts_manager_->on_get_chats(std::move(participants->chats_), "GetChannelAdministratorsQuery");

        auto channel_type = td_->contacts_manager_->get_channel_type(channel_id_);
        vector<DialogAdministrator> administrators;
        administrators.reserve(participants->participants_.size());
        for (auto &participant : participants->participants_) {
          DialogParticipant dialog_participant(std::move(participant), channel_type);
          if (!dialog_participant.is_valid() || !dialog_participant.status_.is_administrator() ||
              dialog_participant.dialog_id_.get_type() != DialogType::User) {
            LOG(ERROR) << "Receive " << dialog_participant << " as an administrator of " << channel_id_;
            continue;
          }
          administrators.emplace_back(dialog_participant.dialog_id_.get_user_id(),
                                      dialog_participant.status_.get_rank(), dialog_participant.status_.is_creator());
        }

        td_->contacts_manager_->on_update_channel_administrator_count(channel_id_,
                                                                      narrow_cast<int32>(administrators.size()));
        td_->dialog_participant_manager_->on_update_dialog_administrators(DialogId(channel_id_),
                                                                          std::move(administrators), true, false);

        break;
      }
      case telegram_api::channels_channelParticipantsNotModified::ID:
        break;
      default:
        UNREACHABLE();
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelAdministratorsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetChannelParticipantQuery final : public Td::ResultHandler {
  Promise<DialogParticipant> promise_;
  ChannelId channel_id_;
  DialogId participant_dialog_id_;

 public:
  explicit GetChannelParticipantQuery(Promise<DialogParticipant> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, DialogId participant_dialog_id, tl_object_ptr<telegram_api::InputPeer> &&input_peer) {
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    if (input_channel == nullptr) {
      return promise_.set_error(Status::Error(400, "Supergroup not found"));
    }

    CHECK(input_peer != nullptr);

    channel_id_ = channel_id;
    participant_dialog_id_ = participant_dialog_id;
    send_query(G()->net_query_creator().create(
        telegram_api::channels_getParticipant(std::move(input_channel), std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getParticipant>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto participant = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetChannelParticipantQuery: " << to_string(participant);

    td_->contacts_manager_->on_get_users(std::move(participant->users_), "GetChannelParticipantQuery");
    td_->contacts_manager_->on_get_chats(std::move(participant->chats_), "GetChannelParticipantQuery");
    DialogParticipant result(std::move(participant->participant_),
                             td_->contacts_manager_->get_channel_type(channel_id_));
    if (!result.is_valid()) {
      LOG(ERROR) << "Receive invalid " << result;
      return promise_.set_error(Status::Error(500, "Receive invalid chat member"));
    }
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    if (status.message() == "USER_NOT_PARTICIPANT") {
      promise_.set_value(DialogParticipant::left(participant_dialog_id_));
      return;
    }

    if (participant_dialog_id_.get_type() != DialogType::Channel) {
      td_->contacts_manager_->on_get_channel_error(channel_id_, status, "GetChannelParticipantQuery");
    }
    promise_.set_error(std::move(status));
  }
};

DialogParticipantManager::DialogParticipantManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  update_dialog_online_member_count_timeout_.set_callback(on_update_dialog_online_member_count_timeout_callback);
  update_dialog_online_member_count_timeout_.set_callback_data(static_cast<void *>(this));
}

DialogParticipantManager::~DialogParticipantManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), dialog_administrators_);
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

void DialogParticipantManager::speculative_update_dialog_administrators(DialogId dialog_id, UserId user_id,
                                                                        const DialogParticipantStatus &new_status,
                                                                        const DialogParticipantStatus &old_status) {
  if (new_status.is_administrator() == old_status.is_administrator() &&
      new_status.get_rank() == old_status.get_rank()) {
    return;
  }
  auto it = dialog_administrators_.find(dialog_id);
  if (it == dialog_administrators_.end()) {
    return;
  }
  auto administrators = it->second;
  if (new_status.is_administrator()) {
    bool is_found = false;
    for (auto &administrator : administrators) {
      if (administrator.get_user_id() == user_id) {
        is_found = true;
        if (administrator.get_rank() != new_status.get_rank() ||
            administrator.is_creator() != new_status.is_creator()) {
          administrator = DialogAdministrator(user_id, new_status.get_rank(), new_status.is_creator());
          on_update_dialog_administrators(dialog_id, std::move(administrators), true, false);
        }
        break;
      }
    }
    if (!is_found) {
      administrators.emplace_back(user_id, new_status.get_rank(), new_status.is_creator());
      on_update_dialog_administrators(dialog_id, std::move(administrators), true, false);
    }
  } else {
    size_t i = 0;
    while (i != administrators.size() && administrators[i].get_user_id() != user_id) {
      i++;
    }
    if (i != administrators.size()) {
      administrators.erase(administrators.begin() + i);
      on_update_dialog_administrators(dialog_id, std::move(administrators), true, false);
    }
  }
}

td_api::object_ptr<td_api::chatAdministrators> DialogParticipantManager::get_chat_administrators_object(
    const vector<DialogAdministrator> &dialog_administrators) {
  auto administrator_objects = transform(dialog_administrators, [this](const DialogAdministrator &administrator) {
    return administrator.get_chat_administrator_object(td_->contacts_manager_.get());
  });
  return td_api::make_object<td_api::chatAdministrators>(std::move(administrator_objects));
}

void DialogParticipantManager::get_dialog_administrators(
    DialogId dialog_id, Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "get_dialog_administrators")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
    case DialogType::SecretChat:
      return promise.set_value(td_api::make_object<td_api::chatAdministrators>());
    case DialogType::Chat:
    case DialogType::Channel:
      break;
    case DialogType::None:
    default:
      UNREACHABLE();
      return;
  }

  auto it = dialog_administrators_.find(dialog_id);
  if (it != dialog_administrators_.end()) {
    reload_dialog_administrators(dialog_id, it->second, Auto());  // update administrators cache
    return promise.set_value(get_chat_administrators_object(it->second));
  }

  if (G()->use_chat_info_database()) {
    LOG(INFO) << "Load administrators of " << dialog_id << " from database";
    G()->td_db()->get_sqlite_pmc()->get(
        get_dialog_administrators_database_key(dialog_id),
        PromiseCreator::lambda(
            [actor_id = actor_id(this), dialog_id, promise = std::move(promise)](string value) mutable {
              send_closure(actor_id, &DialogParticipantManager::on_load_dialog_administrators_from_database, dialog_id,
                           std::move(value), std::move(promise));
            }));
    return;
  }

  reload_dialog_administrators(dialog_id, {}, std::move(promise));
}

string DialogParticipantManager::get_dialog_administrators_database_key(DialogId dialog_id) {
  return PSTRING() << "adm" << (-dialog_id.get());
}

void DialogParticipantManager::on_load_dialog_administrators_from_database(
    DialogId dialog_id, string value, Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  if (value.empty()) {
    return reload_dialog_administrators(dialog_id, {}, std::move(promise));
  }

  vector<DialogAdministrator> administrators;
  if (log_event_parse(administrators, value).is_error()) {
    return reload_dialog_administrators(dialog_id, {}, std::move(promise));
  }

  LOG(INFO) << "Successfully loaded " << administrators.size() << " administrators in " << dialog_id
            << " from database";

  MultiPromiseActorSafe load_users_multipromise{"LoadUsersMultiPromiseActor"};
  load_users_multipromise.add_promise(
      PromiseCreator::lambda([actor_id = actor_id(this), dialog_id, administrators,
                              promise = std::move(promise)](Result<Unit> result) mutable {
        send_closure(actor_id, &DialogParticipantManager::on_load_administrator_users_finished, dialog_id,
                     std::move(administrators), std::move(result), std::move(promise));
      }));

  auto lock_promise = load_users_multipromise.get_promise();

  for (auto &administrator : administrators) {
    td_->contacts_manager_->get_user(administrator.get_user_id(), 3, load_users_multipromise.get_promise());
  }

  lock_promise.set_value(Unit());
}

void DialogParticipantManager::on_load_administrator_users_finished(
    DialogId dialog_id, vector<DialogAdministrator> administrators, Result<> result,
    Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  if (result.is_error()) {
    return reload_dialog_administrators(dialog_id, {}, std::move(promise));
  }

  auto it = dialog_administrators_.emplace(dialog_id, std::move(administrators)).first;
  reload_dialog_administrators(dialog_id, it->second, Auto());  // update administrators cache
  promise.set_value(get_chat_administrators_object(it->second));
}

void DialogParticipantManager::on_update_dialog_administrators(DialogId dialog_id,
                                                               vector<DialogAdministrator> &&administrators,
                                                               bool have_access, bool from_database) {
  LOG(INFO) << "Update administrators in " << dialog_id << " to " << administrators;
  if (have_access) {
    CHECK(dialog_id.is_valid());
    std::sort(administrators.begin(), administrators.end(),
              [](const DialogAdministrator &lhs, const DialogAdministrator &rhs) {
                return lhs.get_user_id().get() < rhs.get_user_id().get();
              });

    auto it = dialog_administrators_.find(dialog_id);
    if (it != dialog_administrators_.end()) {
      if (it->second == administrators) {
        return;
      }
      it->second = std::move(administrators);
    } else {
      it = dialog_administrators_.emplace(dialog_id, std::move(administrators)).first;
    }

    if (G()->use_chat_info_database() && !from_database) {
      LOG(INFO) << "Save administrators of " << dialog_id << " to database";
      G()->td_db()->get_sqlite_pmc()->set(get_dialog_administrators_database_key(dialog_id),
                                          log_event_store(it->second).as_slice().str(), Auto());
    }
  } else {
    dialog_administrators_.erase(dialog_id);
    if (G()->use_chat_info_database()) {
      G()->td_db()->get_sqlite_pmc()->erase(get_dialog_administrators_database_key(dialog_id), Auto());
    }
  }
}

void DialogParticipantManager::reload_dialog_administrators(
    DialogId dialog_id, const vector<DialogAdministrator> &dialog_administrators,
    Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise) {
  auto dialog_type = dialog_id.get_type();
  if (dialog_type == DialogType::Chat &&
      !td_->contacts_manager_->get_chat_permissions(dialog_id.get_chat_id()).is_member()) {
    return promise.set_value(td_api::make_object<td_api::chatAdministrators>());
  }
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (promise) {
          if (result.is_ok()) {
            send_closure(actor_id, &DialogParticipantManager::on_reload_dialog_administrators, dialog_id,
                         std::move(promise));
          } else {
            promise.set_error(result.move_as_error());
          }
        }
      });
  switch (dialog_type) {
    case DialogType::Chat:
      td_->contacts_manager_->load_chat_full(dialog_id.get_chat_id(), false, std::move(query_promise),
                                             "reload_dialog_administrators");
      break;
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      if (td_->contacts_manager_->is_broadcast_channel(channel_id) &&
          !td_->contacts_manager_->get_channel_status(channel_id).is_administrator()) {
        return query_promise.set_error(Status::Error(400, "Administrator list is inaccessible"));
      }
      auto hash = get_vector_hash(transform(dialog_administrators, [](const DialogAdministrator &administrator) {
        return static_cast<uint64>(administrator.get_user_id().get());
      }));
      td_->create_handler<GetChannelAdministratorsQuery>(std::move(query_promise))->send(channel_id, hash);
      break;
    }
    default:
      UNREACHABLE();
  }
}

void DialogParticipantManager::on_reload_dialog_administrators(
    DialogId dialog_id, Promise<td_api::object_ptr<td_api::chatAdministrators>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto it = dialog_administrators_.find(dialog_id);
  if (it != dialog_administrators_.end()) {
    return promise.set_value(get_chat_administrators_object(it->second));
  }

  LOG(ERROR) << "Failed to load administrators in " << dialog_id;
  promise.set_error(Status::Error(500, "Failed to find chat administrators"));
}

void DialogParticipantManager::send_update_chat_member(DialogId dialog_id, UserId agent_user_id, int32 date,
                                                       const DialogInviteLink &invite_link,
                                                       bool via_dialog_filter_invite_link,
                                                       const DialogParticipant &old_dialog_participant,
                                                       const DialogParticipant &new_dialog_participant) {
  CHECK(td_->auth_manager_->is_bot());
  td_->dialog_manager_->force_create_dialog(dialog_id, "send_update_chat_member", true);
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateChatMember>(
                   td_->dialog_manager_->get_chat_id_object(dialog_id, "updateChatMember"),
                   td_->contacts_manager_->get_user_id_object(agent_user_id, "updateChatMember"), date,
                   invite_link.get_chat_invite_link_object(td_->contacts_manager_.get()), via_dialog_filter_invite_link,
                   td_->contacts_manager_->get_chat_member_object(old_dialog_participant, "updateChatMember old"),
                   td_->contacts_manager_->get_chat_member_object(new_dialog_participant, "updateChatMember new")));
}

void DialogParticipantManager::on_update_bot_stopped(UserId user_id, int32 date, bool is_stopped, bool force) {
  CHECK(td_->auth_manager_->is_bot());
  if (date <= 0 || !td_->contacts_manager_->have_user_force(user_id, "on_update_bot_stopped")) {
    LOG(ERROR) << "Receive invalid updateBotStopped by " << user_id << " at " << date;
    return;
  }
  auto my_user_id = td_->contacts_manager_->get_my_id();
  if (!td_->contacts_manager_->have_user_force(my_user_id, "on_update_bot_stopped 2")) {
    if (!force) {
      td_->contacts_manager_->get_me(
          PromiseCreator::lambda([actor_id = actor_id(this), user_id, date, is_stopped](Unit) {
            send_closure(actor_id, &DialogParticipantManager::on_update_bot_stopped, user_id, date, is_stopped, true);
          }));
      return;
    }
    LOG(ERROR) << "Have no self-user to process updateBotStopped";
  }

  DialogParticipant old_dialog_participant(DialogId(my_user_id), user_id, date, DialogParticipantStatus::Banned(0));
  DialogParticipant new_dialog_participant(DialogId(my_user_id), user_id, date, DialogParticipantStatus::Member());
  if (is_stopped) {
    std::swap(old_dialog_participant.status_, new_dialog_participant.status_);
  }

  send_update_chat_member(DialogId(user_id), user_id, date, DialogInviteLink(), false, old_dialog_participant,
                          new_dialog_participant);
}

void DialogParticipantManager::on_update_chat_participant(
    ChatId chat_id, UserId user_id, int32 date, DialogInviteLink invite_link,
    telegram_api::object_ptr<telegram_api::ChatParticipant> old_participant,
    telegram_api::object_ptr<telegram_api::ChatParticipant> new_participant) {
  CHECK(td_->auth_manager_->is_bot());
  if (!chat_id.is_valid() || !user_id.is_valid() || date <= 0 ||
      (old_participant == nullptr && new_participant == nullptr)) {
    LOG(ERROR) << "Receive invalid updateChatParticipant in " << chat_id << " by " << user_id << " at " << date << ": "
               << to_string(old_participant) << " -> " << to_string(new_participant);
    return;
  }

  if (!td_->contacts_manager_->have_chat(chat_id)) {
    LOG(ERROR) << "Receive updateChatParticipant in unknown " << chat_id;
    return;
  }
  auto chat_date = td_->contacts_manager_->get_chat_date(chat_id);
  auto chat_status = td_->contacts_manager_->get_chat_status(chat_id);
  auto is_creator = chat_status.is_creator();

  DialogParticipant old_dialog_participant;
  DialogParticipant new_dialog_participant;
  if (old_participant != nullptr) {
    old_dialog_participant = DialogParticipant(std::move(old_participant), chat_date, is_creator);
    if (new_participant == nullptr) {
      new_dialog_participant = DialogParticipant::left(old_dialog_participant.dialog_id_);
    } else {
      new_dialog_participant = DialogParticipant(std::move(new_participant), chat_date, is_creator);
    }
  } else {
    new_dialog_participant = DialogParticipant(std::move(new_participant), chat_date, is_creator);
    old_dialog_participant = DialogParticipant::left(new_dialog_participant.dialog_id_);
  }
  if (old_dialog_participant.dialog_id_ != new_dialog_participant.dialog_id_ || !old_dialog_participant.is_valid() ||
      !new_dialog_participant.is_valid()) {
    LOG(ERROR) << "Receive wrong updateChatParticipant: " << old_dialog_participant << " -> " << new_dialog_participant;
    return;
  }
  if (new_dialog_participant.dialog_id_ == DialogId(td_->contacts_manager_->get_my_id()) &&
      new_dialog_participant.status_ != chat_status && false) {
    LOG(ERROR) << "Have status " << chat_status << " after receiving updateChatParticipant in " << chat_id << " by "
               << user_id << " at " << date << " from " << old_dialog_participant << " to " << new_dialog_participant;
  }

  send_update_chat_member(DialogId(chat_id), user_id, date, invite_link, false, old_dialog_participant,
                          new_dialog_participant);
}

void DialogParticipantManager::on_update_channel_participant(
    ChannelId channel_id, UserId user_id, int32 date, DialogInviteLink invite_link, bool via_dialog_filter_invite_link,
    telegram_api::object_ptr<telegram_api::ChannelParticipant> old_participant,
    telegram_api::object_ptr<telegram_api::ChannelParticipant> new_participant) {
  CHECK(td_->auth_manager_->is_bot());
  if (!channel_id.is_valid() || !user_id.is_valid() || date <= 0 ||
      (old_participant == nullptr && new_participant == nullptr)) {
    LOG(ERROR) << "Receive invalid updateChannelParticipant in " << channel_id << " by " << user_id << " at " << date
               << ": " << to_string(old_participant) << " -> " << to_string(new_participant);
    return;
  }
  if (!td_->contacts_manager_->have_channel(channel_id)) {
    LOG(ERROR) << "Receive updateChannelParticipant in unknown " << channel_id;
    return;
  }

  DialogParticipant old_dialog_participant;
  DialogParticipant new_dialog_participant;
  auto channel_type = td_->contacts_manager_->get_channel_type(channel_id);
  if (old_participant != nullptr) {
    old_dialog_participant = DialogParticipant(std::move(old_participant), channel_type);
    if (new_participant == nullptr) {
      new_dialog_participant = DialogParticipant::left(old_dialog_participant.dialog_id_);
    } else {
      new_dialog_participant = DialogParticipant(std::move(new_participant), channel_type);
    }
  } else {
    new_dialog_participant = DialogParticipant(std::move(new_participant), channel_type);
    old_dialog_participant = DialogParticipant::left(new_dialog_participant.dialog_id_);
  }
  if (old_dialog_participant.dialog_id_ != new_dialog_participant.dialog_id_ || !old_dialog_participant.is_valid() ||
      !new_dialog_participant.is_valid()) {
    LOG(ERROR) << "Receive wrong updateChannelParticipant: " << old_dialog_participant << " -> "
               << new_dialog_participant;
    return;
  }
  if (new_dialog_participant.status_.is_administrator() && user_id == td_->contacts_manager_->get_my_id() &&
      !new_dialog_participant.status_.can_be_edited()) {
    LOG(ERROR) << "Fix wrong can_be_edited in " << new_dialog_participant << " from " << channel_id << " changed from "
               << old_dialog_participant;
    new_dialog_participant.status_.toggle_can_be_edited();
  }

  if (old_dialog_participant.dialog_id_ == td_->dialog_manager_->get_my_dialog_id() &&
      old_dialog_participant.status_.is_administrator() && !new_dialog_participant.status_.is_administrator()) {
    td_->contacts_manager_->drop_channel_participant_cache(channel_id);
  } else if (td_->contacts_manager_->have_channel_participant_cache(channel_id)) {
    td_->contacts_manager_->add_channel_participant_to_cache(channel_id, new_dialog_participant, true);
  }

  auto channel_status = td_->contacts_manager_->get_channel_status(channel_id);
  if (new_dialog_participant.dialog_id_ == td_->dialog_manager_->get_my_dialog_id() &&
      new_dialog_participant.status_ != channel_status && false) {
    LOG(ERROR) << "Have status " << channel_status << " after receiving updateChannelParticipant in " << channel_id
               << " by " << user_id << " at " << date << " from " << old_dialog_participant << " to "
               << new_dialog_participant;
  }

  send_update_chat_member(DialogId(channel_id), user_id, date, invite_link, via_dialog_filter_invite_link,
                          old_dialog_participant, new_dialog_participant);
}

void DialogParticipantManager::on_update_chat_invite_requester(DialogId dialog_id, UserId user_id, string about,
                                                               int32 date, DialogInviteLink invite_link) {
  CHECK(td_->auth_manager_->is_bot());
  if (date <= 0 || !td_->contacts_manager_->have_user_force(user_id, "on_update_chat_invite_requester") ||
      !td_->dialog_manager_->have_dialog_info_force(dialog_id, "on_update_chat_invite_requester")) {
    LOG(ERROR) << "Receive invalid updateBotChatInviteRequester by " << user_id << " in " << dialog_id << " at "
               << date;
    return;
  }
  DialogId user_dialog_id(user_id);
  td_->dialog_manager_->force_create_dialog(dialog_id, "on_update_chat_invite_requester", true);
  td_->dialog_manager_->force_create_dialog(user_dialog_id, "on_update_chat_invite_requester");

  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateNewChatJoinRequest>(
                   td_->dialog_manager_->get_chat_id_object(dialog_id, "updateNewChatJoinRequest"),
                   td_api::make_object<td_api::chatJoinRequest>(
                       td_->contacts_manager_->get_user_id_object(user_id, "updateNewChatJoinRequest"), date, about),
                   td_->dialog_manager_->get_chat_id_object(user_dialog_id, "updateNewChatJoinRequest 2"),
                   invite_link.get_chat_invite_link_object(td_->contacts_manager_.get())));
}

void DialogParticipantManager::get_dialog_participant(DialogId dialog_id, DialogId participant_dialog_id,
                                                      Promise<td_api::object_ptr<td_api::chatMember>> &&promise) {
  auto new_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](Result<DialogParticipant> &&result) mutable {
        TRY_RESULT_PROMISE(promise, dialog_participant, std::move(result));
        send_closure(actor_id, &DialogParticipantManager::finish_get_dialog_participant, std::move(dialog_participant),
                     std::move(promise));
      });
  do_get_dialog_participant(dialog_id, participant_dialog_id, std::move(new_promise));
}

void DialogParticipantManager::finish_get_dialog_participant(
    DialogParticipant &&dialog_participant, Promise<td_api::object_ptr<td_api::chatMember>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto participant_dialog_id = dialog_participant.dialog_id_;
  bool is_user = participant_dialog_id.get_type() == DialogType::User;
  if ((is_user && !td_->contacts_manager_->have_user(participant_dialog_id.get_user_id())) ||
      (!is_user && !td_->messages_manager_->have_dialog(participant_dialog_id))) {
    return promise.set_error(Status::Error(400, "Member not found"));
  }

  promise.set_value(
      td_->contacts_manager_->get_chat_member_object(dialog_participant, "finish_get_dialog_participant"));
}

void DialogParticipantManager::do_get_dialog_participant(DialogId dialog_id, DialogId participant_dialog_id,
                                                         Promise<DialogParticipant> &&promise) {
  LOG(INFO) << "Receive getChatMember request to get " << participant_dialog_id << " in " << dialog_id;
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "do_get_dialog_participant")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User: {
      auto my_user_id = td_->contacts_manager_->get_my_id();
      auto peer_user_id = dialog_id.get_user_id();
      if (participant_dialog_id == DialogId(my_user_id)) {
        return promise.set_value(DialogParticipant::private_member(my_user_id, peer_user_id));
      }
      if (participant_dialog_id == dialog_id) {
        return promise.set_value(DialogParticipant::private_member(peer_user_id, my_user_id));
      }

      return promise.set_error(Status::Error(400, "Member not found"));
    }
    case DialogType::Chat:
      if (participant_dialog_id.get_type() != DialogType::User) {
        return promise.set_value(DialogParticipant::left(participant_dialog_id));
      }
      return td_->contacts_manager_->get_chat_participant(dialog_id.get_chat_id(), participant_dialog_id.get_user_id(),
                                                          std::move(promise));
    case DialogType::Channel:
      return get_channel_participant(dialog_id.get_channel_id(), participant_dialog_id, std::move(promise));
    case DialogType::SecretChat: {
      auto my_user_id = td_->contacts_manager_->get_my_id();
      auto peer_user_id = td_->contacts_manager_->get_secret_chat_user_id(dialog_id.get_secret_chat_id());
      if (participant_dialog_id == DialogId(my_user_id)) {
        return promise.set_value(DialogParticipant::private_member(my_user_id, peer_user_id));
      }
      if (peer_user_id.is_valid() && participant_dialog_id == DialogId(peer_user_id)) {
        return promise.set_value(DialogParticipant::private_member(peer_user_id, my_user_id));
      }

      return promise.set_error(Status::Error(400, "Member not found"));
    }
    case DialogType::None:
    default:
      UNREACHABLE();
      return;
  }
}

void DialogParticipantManager::get_channel_participant(ChannelId channel_id, DialogId participant_dialog_id,
                                                       Promise<DialogParticipant> &&promise) {
  LOG(INFO) << "Trying to get " << participant_dialog_id << " as member of " << channel_id;

  auto input_peer = td_->dialog_manager_->get_input_peer(participant_dialog_id, AccessRights::Know);
  if (input_peer == nullptr) {
    return promise.set_error(Status::Error(400, "Member not found"));
  }

  if (td_->contacts_manager_->have_channel_participant_cache(channel_id)) {
    auto *participant = td_->contacts_manager_->get_channel_participant_from_cache(channel_id, participant_dialog_id);
    if (participant != nullptr) {
      return promise.set_value(DialogParticipant{*participant});
    }
  }

  auto on_result_promise = PromiseCreator::lambda([actor_id = actor_id(this), channel_id, promise = std::move(promise)](
                                                      Result<DialogParticipant> r_dialog_participant) mutable {
    TRY_RESULT_PROMISE(promise, dialog_participant, std::move(r_dialog_participant));
    send_closure(actor_id, &DialogParticipantManager::finish_get_channel_participant, channel_id,
                 std::move(dialog_participant), std::move(promise));
  });

  td_->create_handler<GetChannelParticipantQuery>(std::move(on_result_promise))
      ->send(channel_id, participant_dialog_id, std::move(input_peer));
}

void DialogParticipantManager::finish_get_channel_participant(ChannelId channel_id,
                                                              DialogParticipant &&dialog_participant,
                                                              Promise<DialogParticipant> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  CHECK(dialog_participant.is_valid());  // checked in GetChannelParticipantQuery

  LOG(INFO) << "Receive " << dialog_participant.dialog_id_ << " as a member of a channel " << channel_id;

  dialog_participant.status_.update_restrictions();
  if (td_->contacts_manager_->have_channel_participant_cache(channel_id)) {
    td_->contacts_manager_->add_channel_participant_to_cache(channel_id, dialog_participant, false);
  }
  promise.set_value(std::move(dialog_participant));
}

}  // namespace td
