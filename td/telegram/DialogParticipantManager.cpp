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
#include "td/telegram/ChannelType.h"
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
#include "td/actor/SleepActor.h"

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
#include <utility>

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
          if (!dialog_participant.is_valid() || !dialog_participant.status_.is_administrator_member() ||
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

class JoinChannelQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit JoinChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(
        G()->net_query_creator().create(telegram_api::channels_joinChannel(std::move(input_channel)), {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_joinChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for JoinChannelQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "JoinChannelQuery");
    promise_.set_error(std::move(status));
  }
};

class InviteToChannelQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  vector<UserId> user_ids_;

 public:
  explicit InviteToChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, vector<UserId> user_ids,
            vector<tl_object_ptr<telegram_api::InputUser>> &&input_users) {
    channel_id_ = channel_id;
    user_ids_ = std::move(user_ids);
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::channels_inviteToChannel(std::move(input_channel), std::move(input_users))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_inviteToChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for InviteToChannelQuery: " << to_string(ptr);
    td_->contacts_manager_->invalidate_channel_full(channel_id_, false, "InviteToChannelQuery");
    auto user_ids = td_->updates_manager_->extract_group_invite_privacy_forbidden_updates(ptr);
    auto promise = PromiseCreator::lambda([dialog_id = DialogId(channel_id_), user_ids = std::move(user_ids),
                                           promise = std::move(promise_)](Result<Unit> &&result) mutable {
      if (result.is_error()) {
        return promise.set_error(result.move_as_error());
      }
      promise.set_value(Unit());
      if (!user_ids.empty()) {
        send_closure(G()->dialog_participant_manager(),
                     &DialogParticipantManager::send_update_add_chat_members_privacy_forbidden, dialog_id,
                     std::move(user_ids), "InviteToChannelQuery");
      }
    });
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise));
  }

  void on_error(Status status) final {
    if (!td_->auth_manager_->is_bot() && status.message() == "USER_PRIVACY_RESTRICTED") {
      td_->dialog_participant_manager_->send_update_add_chat_members_privacy_forbidden(
          DialogId(channel_id_), std::move(user_ids_), "InviteToChannelQuery");
      return promise_.set_error(Status::Error(406, "USER_PRIVACY_RESTRICTED"));
    }
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "InviteToChannelQuery");
    td_->contacts_manager_->invalidate_channel_full(channel_id_, false, "InviteToChannelQuery");
    promise_.set_error(std::move(status));
  }
};

class EditChannelAdminQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  UserId user_id_;
  DialogParticipantStatus status_ = DialogParticipantStatus::Left();

 public:
  explicit EditChannelAdminQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
            const DialogParticipantStatus &status) {
    channel_id_ = channel_id;
    user_id_ = user_id;
    status_ = status;
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::channels_editAdmin(
        std::move(input_channel), std::move(input_user), status.get_chat_admin_rights(), status.get_rank())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_editAdmin>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditChannelAdminQuery: " << to_string(ptr);
    td_->contacts_manager_->invalidate_channel_full(channel_id_, false, "EditChannelAdminQuery");
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
    td_->dialog_participant_manager_->on_set_channel_participant_status(channel_id_, DialogId(user_id_), status_);
  }

  void on_error(Status status) final {
    if (!td_->auth_manager_->is_bot() && status.message() == "USER_PRIVACY_RESTRICTED") {
      td_->dialog_participant_manager_->send_update_add_chat_members_privacy_forbidden(
          DialogId(channel_id_), {user_id_}, "EditChannelAdminQuery");
      return promise_.set_error(Status::Error(406, "USER_PRIVACY_RESTRICTED"));
    }
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "EditChannelAdminQuery");
    td_->contacts_manager_->invalidate_channel_full(channel_id_, false, "EditChannelAdminQuery");
    promise_.set_error(std::move(status));
  }
};

class EditChannelBannedQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;
  DialogId participant_dialog_id_;
  DialogParticipantStatus status_ = DialogParticipantStatus::Left();

 public:
  explicit EditChannelBannedQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id, DialogId participant_dialog_id, tl_object_ptr<telegram_api::InputPeer> &&input_peer,
            const DialogParticipantStatus &status) {
    channel_id_ = channel_id;
    participant_dialog_id_ = participant_dialog_id;
    status_ = status;
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(G()->net_query_creator().create(telegram_api::channels_editBanned(
        std::move(input_channel), std::move(input_peer), status.get_chat_banned_rights())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_editBanned>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditChannelBannedQuery: " << to_string(ptr);
    td_->contacts_manager_->invalidate_channel_full(channel_id_, false, "EditChannelBannedQuery");
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
    td_->dialog_participant_manager_->on_set_channel_participant_status(channel_id_, participant_dialog_id_, status_);
  }

  void on_error(Status status) final {
    if (participant_dialog_id_.get_type() != DialogType::Channel) {
      td_->contacts_manager_->on_get_channel_error(channel_id_, status, "EditChannelBannedQuery");
    }
    td_->contacts_manager_->invalidate_channel_full(channel_id_, false, "EditChannelBannedQuery");
    promise_.set_error(std::move(status));
  }
};

class LeaveChannelQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  ChannelId channel_id_;

 public:
  explicit LeaveChannelQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;
    auto input_channel = td_->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);
    send_query(
        G()->net_query_creator().create(telegram_api::channels_leaveChannel(std::move(input_channel)), {{channel_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_leaveChannel>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for LeaveChannelQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "USER_NOT_PARTICIPANT") {
      return td_->contacts_manager_->reload_channel(channel_id_, std::move(promise_), "LeaveChannelQuery");
    }
    td_->contacts_manager_->on_get_channel_error(channel_id_, status, "LeaveChannelQuery");
    promise_.set_error(std::move(status));
  }
};

DialogParticipantManager::DialogParticipantManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  update_dialog_online_member_count_timeout_.set_callback(on_update_dialog_online_member_count_timeout_callback);
  update_dialog_online_member_count_timeout_.set_callback_data(static_cast<void *>(this));

  channel_participant_cache_timeout_.set_callback(on_channel_participant_cache_timeout_callback);
  channel_participant_cache_timeout_.set_callback_data(static_cast<void *>(this));
}

DialogParticipantManager::~DialogParticipantManager() {
  Scheduler::instance()->destroy_on_scheduler(G()->get_gc_scheduler_id(), dialog_administrators_,
                                              channel_participants_);
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
  if (new_status.is_administrator_member() == old_status.is_administrator_member() &&
      new_status.get_rank() == old_status.get_rank()) {
    return;
  }
  auto it = dialog_administrators_.find(dialog_id);
  if (it == dialog_administrators_.end()) {
    return;
  }
  auto administrators = it->second;
  if (new_status.is_administrator_member()) {
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
    drop_channel_participant_cache(channel_id);
  } else if (have_channel_participant_cache(channel_id)) {
    add_channel_participant_to_cache(channel_id, new_dialog_participant, true);
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

  if (have_channel_participant_cache(channel_id)) {
    auto *participant = get_channel_participant_from_cache(channel_id, participant_dialog_id);
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
  if (have_channel_participant_cache(channel_id)) {
    add_channel_participant_to_cache(channel_id, dialog_participant, false);
  }
  promise.set_value(std::move(dialog_participant));
}

void DialogParticipantManager::add_dialog_participant(DialogId dialog_id, UserId user_id, int32 forward_limit,
                                                      Promise<Unit> &&promise) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "add_dialog_participant")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return promise.set_error(Status::Error(400, "Can't add members to a private chat"));
    case DialogType::Chat:
      return td_->contacts_manager_->add_chat_participant(dialog_id.get_chat_id(), user_id, forward_limit,
                                                          std::move(promise));
    case DialogType::Channel:
      return add_channel_participant(dialog_id.get_channel_id(), user_id, DialogParticipantStatus::Left(),
                                     std::move(promise));
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Can't add members to a secret chat"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }
}

void DialogParticipantManager::add_dialog_participants(DialogId dialog_id, const vector<UserId> &user_ids,
                                                       Promise<Unit> &&promise) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "add_dialog_participants")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return promise.set_error(Status::Error(400, "Can't add members to a private chat"));
    case DialogType::Chat:
      if (user_ids.size() == 1) {
        return td_->contacts_manager_->add_chat_participant(dialog_id.get_chat_id(), user_ids[0], 0,
                                                            std::move(promise));
      }
      return promise.set_error(Status::Error(400, "Can't add many members at once to a basic group chat"));
    case DialogType::Channel:
      return add_channel_participants(dialog_id.get_channel_id(), user_ids, std::move(promise));
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Can't add members to a secret chat"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }
}

void DialogParticipantManager::set_dialog_participant_status(
    DialogId dialog_id, DialogId participant_dialog_id,
    td_api::object_ptr<td_api::ChatMemberStatus> &&chat_member_status, Promise<Unit> &&promise) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "set_dialog_participant_status")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return promise.set_error(Status::Error(400, "Chat member status can't be changed in private chats"));
    case DialogType::Chat: {
      auto status = get_dialog_participant_status(chat_member_status, ChannelType::Unknown);
      if (participant_dialog_id.get_type() != DialogType::User) {
        if (status == DialogParticipantStatus::Left()) {
          return promise.set_value(Unit());
        } else {
          return promise.set_error(Status::Error(400, "Chats can't be members of basic groups"));
        }
      }
      return td_->contacts_manager_->set_chat_participant_status(
          dialog_id.get_chat_id(), participant_dialog_id.get_user_id(), status, std::move(promise));
    }
    case DialogType::Channel:
      return set_channel_participant_status(dialog_id.get_channel_id(), participant_dialog_id,
                                            std::move(chat_member_status), std::move(promise));
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Chat member status can't be changed in secret chats"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }
}

void DialogParticipantManager::ban_dialog_participant(DialogId dialog_id, DialogId participant_dialog_id,
                                                      int32 banned_until_date, bool revoke_messages,
                                                      Promise<Unit> &&promise) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "ban_dialog_participant")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return promise.set_error(Status::Error(400, "Can't ban members in private chats"));
    case DialogType::Chat:
      if (participant_dialog_id.get_type() != DialogType::User) {
        return promise.set_error(Status::Error(400, "Can't ban chats in basic groups"));
      }
      return td_->contacts_manager_->delete_chat_participant(
          dialog_id.get_chat_id(), participant_dialog_id.get_user_id(), revoke_messages, std::move(promise));
    case DialogType::Channel:
      // must use td_api::chatMemberStatusBanned to properly fix banned_until_date
      return set_channel_participant_status(dialog_id.get_channel_id(), participant_dialog_id,
                                            td_api::make_object<td_api::chatMemberStatusBanned>(banned_until_date),
                                            std::move(promise));
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Can't ban members in secret chats"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }
}

void DialogParticipantManager::leave_dialog(DialogId dialog_id, Promise<Unit> &&promise) {
  if (!td_->dialog_manager_->have_dialog_force(dialog_id, "leave_dialog")) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }

  switch (dialog_id.get_type()) {
    case DialogType::User:
      return promise.set_error(Status::Error(400, "Can't leave private chats"));
    case DialogType::Chat:
      return td_->contacts_manager_->delete_chat_participant(
          dialog_id.get_chat_id(), td_->contacts_manager_->get_my_id(), false, std::move(promise));
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      auto old_status = td_->contacts_manager_->get_channel_status(channel_id);
      auto new_status = old_status;
      new_status.set_is_member(false);
      return restrict_channel_participant(channel_id, td_->dialog_manager_->get_my_dialog_id(), std::move(new_status),
                                          std::move(old_status), std::move(promise));
    }
    case DialogType::SecretChat:
      return promise.set_error(Status::Error(400, "Can't leave secret chats"));
    case DialogType::None:
    default:
      UNREACHABLE();
  }
}

void DialogParticipantManager::add_channel_participant(ChannelId channel_id, UserId user_id,
                                                       const DialogParticipantStatus &old_status,
                                                       Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Bots can't add new chat members"));
  }

  if (!td_->contacts_manager_->have_channel(channel_id)) {
    return promise.set_error(Status::Error(400, "Chat info not found"));
  }
  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(user_id));

  if (user_id == td_->contacts_manager_->get_my_id()) {
    // join the channel
    auto my_status = td_->contacts_manager_->get_channel_status(channel_id);
    if (my_status.is_banned()) {
      return promise.set_error(Status::Error(400, "Can't return to kicked from chat"));
    }
    if (my_status.is_member()) {
      return promise.set_value(Unit());
    }

    auto &queries = join_channel_queries_[channel_id];
    queries.push_back(std::move(promise));
    if (queries.size() == 1u) {
      if (!td_->contacts_manager_->get_channel_join_request(channel_id)) {
        auto new_status = my_status;
        new_status.set_is_member(true);
        speculative_add_channel_user(channel_id, user_id, new_status, my_status);
      }
      auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), channel_id](Result<Unit> result) {
        send_closure(actor_id, &DialogParticipantManager::on_join_channel, channel_id, std::move(result));
      });
      td_->create_handler<JoinChannelQuery>(std::move(query_promise))->send(channel_id);
    }
    return;
  }

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_invite_users()) {
    return promise.set_error(Status::Error(400, "Not enough rights to invite members to the supergroup chat"));
  }

  speculative_add_channel_user(channel_id, user_id, DialogParticipantStatus::Member(), old_status);
  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  input_users.push_back(std::move(input_user));
  td_->create_handler<InviteToChannelQuery>(std::move(promise))->send(channel_id, {user_id}, std::move(input_users));
}

void DialogParticipantManager::on_join_channel(ChannelId channel_id, Result<Unit> &&result) {
  G()->ignore_result_if_closing(result);

  auto it = join_channel_queries_.find(channel_id);
  CHECK(it != join_channel_queries_.end());
  auto promises = std::move(it->second);
  CHECK(!promises.empty());
  join_channel_queries_.erase(it);

  if (result.is_ok()) {
    set_promises(promises);
  } else {
    fail_promises(promises, result.move_as_error());
  }
}

void DialogParticipantManager::add_channel_participants(ChannelId channel_id, const vector<UserId> &user_ids,
                                                        Promise<Unit> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(Status::Error(400, "Bots can't add new chat members"));
  }

  if (!td_->contacts_manager_->have_channel(channel_id)) {
    return promise.set_error(Status::Error(400, "Chat info not found"));
  }

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_invite_users()) {
    return promise.set_error(Status::Error(400, "Not enough rights to invite members to the supergroup chat"));
  }

  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  for (auto user_id : user_ids) {
    TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(user_id));

    if (user_id == td_->contacts_manager_->get_my_id()) {
      // can't invite self
      continue;
    }
    input_users.push_back(std::move(input_user));

    speculative_add_channel_user(channel_id, user_id, DialogParticipantStatus::Member(),
                                 DialogParticipantStatus::Left());
  }

  if (input_users.empty()) {
    return promise.set_value(Unit());
  }

  td_->create_handler<InviteToChannelQuery>(std::move(promise))->send(channel_id, user_ids, std::move(input_users));
}

void DialogParticipantManager::set_channel_participant_status(
    ChannelId channel_id, DialogId participant_dialog_id,
    td_api::object_ptr<td_api::ChatMemberStatus> &&chat_member_status, Promise<Unit> &&promise) {
  if (!td_->contacts_manager_->have_channel(channel_id)) {
    return promise.set_error(Status::Error(400, "Chat info not found"));
  }
  auto new_status =
      get_dialog_participant_status(chat_member_status, td_->contacts_manager_->get_channel_type(channel_id));

  if (participant_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    // fast path is needed, because get_channel_status may return Creator, while GetChannelParticipantQuery returning Left
    return set_channel_participant_status_impl(channel_id, participant_dialog_id, std::move(new_status),
                                               td_->contacts_manager_->get_channel_status(channel_id),
                                               std::move(promise));
  }
  if (participant_dialog_id.get_type() != DialogType::User) {
    if (new_status.is_administrator() || new_status.is_member() || new_status.is_restricted()) {
      return promise.set_error(Status::Error(400, "Other chats can be only banned or unbanned"));
    }
    // always pretend that old_status is different
    return restrict_channel_participant(
        channel_id, participant_dialog_id, std::move(new_status),
        new_status.is_banned() ? DialogParticipantStatus::Left() : DialogParticipantStatus::Banned(0),
        std::move(promise));
  }

  auto on_result_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), channel_id, participant_dialog_id, new_status,
                              promise = std::move(promise)](Result<DialogParticipant> r_dialog_participant) mutable {
        // ResultHandlers are cleared before managers, so it is safe to capture this
        if (r_dialog_participant.is_error()) {
          return promise.set_error(r_dialog_participant.move_as_error());
        }

        send_closure(actor_id, &DialogParticipantManager::set_channel_participant_status_impl, channel_id,
                     participant_dialog_id, std::move(new_status), r_dialog_participant.ok().status_,
                     std::move(promise));
      });

  get_channel_participant(channel_id, participant_dialog_id, std::move(on_result_promise));
}

void DialogParticipantManager::set_channel_participant_status_impl(ChannelId channel_id, DialogId participant_dialog_id,
                                                                   DialogParticipantStatus new_status,
                                                                   DialogParticipantStatus old_status,
                                                                   Promise<Unit> &&promise) {
  if (old_status == new_status && !old_status.is_creator()) {
    return promise.set_value(Unit());
  }
  CHECK(participant_dialog_id.get_type() == DialogType::User);

  LOG(INFO) << "Change status of " << participant_dialog_id << " in " << channel_id << " from " << old_status << " to "
            << new_status;
  bool need_add = false;
  bool need_promote = false;
  bool need_restrict = false;
  if (new_status.is_creator() || old_status.is_creator()) {
    if (!old_status.is_creator()) {
      return promise.set_error(Status::Error(400, "Can't add another owner to the chat"));
    }
    if (!new_status.is_creator()) {
      return promise.set_error(Status::Error(400, "Can't remove chat owner"));
    }
    auto user_id = td_->contacts_manager_->get_my_id();
    if (participant_dialog_id != DialogId(user_id)) {
      return promise.set_error(Status::Error(400, "Not enough rights to edit chat owner rights"));
    }
    if (new_status.is_member() == old_status.is_member()) {
      // change rank and is_anonymous
      auto r_input_user = td_->contacts_manager_->get_input_user(user_id);
      CHECK(r_input_user.is_ok());
      td_->create_handler<EditChannelAdminQuery>(std::move(promise))
          ->send(channel_id, user_id, r_input_user.move_as_ok(), new_status);
      return;
    }
    if (new_status.is_member()) {
      // creator not member -> creator member
      need_add = true;
    } else {
      // creator member -> creator not member
      need_restrict = true;
    }
  } else if (new_status.is_administrator()) {
    need_promote = true;
  } else if (!new_status.is_member() || new_status.is_restricted()) {
    if (new_status.is_member() && !old_status.is_member()) {
      // TODO there is no way in server API to invite someone and change restrictions
      // we need to first add user and change restrictions again after that
      // but if restrictions aren't changed, then adding is enough
      auto copy_old_status = old_status;
      copy_old_status.set_is_member(true);
      if (copy_old_status == new_status) {
        need_add = true;
      } else {
        need_restrict = true;
      }
    } else {
      need_restrict = true;
    }
  } else {
    // regular member
    if (old_status.is_administrator()) {
      need_promote = true;
    } else if (old_status.is_restricted() || old_status.is_banned()) {
      need_restrict = true;
    } else {
      CHECK(!old_status.is_member());
      need_add = true;
    }
  }

  if (need_promote) {
    if (participant_dialog_id.get_type() != DialogType::User) {
      return promise.set_error(Status::Error(400, "Can't promote chats to chat administrators"));
    }
    return promote_channel_participant(channel_id, participant_dialog_id.get_user_id(), new_status, old_status,
                                       std::move(promise));
  } else if (need_restrict) {
    return restrict_channel_participant(channel_id, participant_dialog_id, std::move(new_status), std::move(old_status),
                                        std::move(promise));
  } else {
    CHECK(need_add);
    if (participant_dialog_id.get_type() != DialogType::User) {
      return promise.set_error(Status::Error(400, "Can't add chats as chat members"));
    }
    return add_channel_participant(channel_id, participant_dialog_id.get_user_id(), old_status, std::move(promise));
  }
}

void DialogParticipantManager::promote_channel_participant(ChannelId channel_id, UserId user_id,
                                                           const DialogParticipantStatus &new_status,
                                                           const DialogParticipantStatus &old_status,
                                                           Promise<Unit> &&promise) {
  LOG(INFO) << "Promote " << user_id << " in " << channel_id << " from " << old_status << " to " << new_status;
  if (user_id == td_->contacts_manager_->get_my_id()) {
    if (new_status.is_administrator()) {
      return promise.set_error(Status::Error(400, "Can't promote self"));
    }
    CHECK(new_status.is_member());
    // allow to demote self. TODO is it allowed server-side?
  } else {
    if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_promote_members()) {
      return promise.set_error(Status::Error(400, "Not enough rights"));
    }

    CHECK(!old_status.is_creator());
    CHECK(!new_status.is_creator());
  }

  TRY_RESULT_PROMISE(promise, input_user, td_->contacts_manager_->get_input_user(user_id));

  speculative_add_channel_user(channel_id, user_id, new_status, old_status);
  td_->create_handler<EditChannelAdminQuery>(std::move(promise))
      ->send(channel_id, user_id, std::move(input_user), new_status);
}

void DialogParticipantManager::restrict_channel_participant(ChannelId channel_id, DialogId participant_dialog_id,
                                                            DialogParticipantStatus &&new_status,
                                                            DialogParticipantStatus &&old_status,
                                                            Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  LOG(INFO) << "Restrict " << participant_dialog_id << " in " << channel_id << " from " << old_status << " to "
            << new_status;
  if (!td_->contacts_manager_->have_channel(channel_id)) {
    return promise.set_error(Status::Error(400, "Chat info not found"));
  }
  auto my_status = td_->contacts_manager_->get_channel_status(channel_id);
  if (!my_status.is_member() && !my_status.is_creator()) {
    if (participant_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
      if (new_status.is_member()) {
        return promise.set_error(Status::Error(400, "Can't unrestrict self"));
      }
      return promise.set_value(Unit());
    } else {
      return promise.set_error(Status::Error(400, "Not in the chat"));
    }
  }
  auto input_peer = td_->dialog_manager_->get_input_peer(participant_dialog_id, AccessRights::Know);
  if (input_peer == nullptr) {
    return promise.set_error(Status::Error(400, "Member not found"));
  }

  if (participant_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    if (new_status.is_restricted() || new_status.is_banned()) {
      return promise.set_error(Status::Error(400, "Can't restrict self"));
    }
    if (new_status.is_member()) {
      return promise.set_error(Status::Error(400, "Can't unrestrict self"));
    }

    // leave the channel
    speculative_add_channel_user(channel_id, participant_dialog_id.get_user_id(), new_status, my_status);
    td_->create_handler<LeaveChannelQuery>(std::move(promise))->send(channel_id);
    return;
  }

  switch (participant_dialog_id.get_type()) {
    case DialogType::User:
      // ok;
      break;
    case DialogType::Channel:
      if (new_status.is_administrator() || new_status.is_member() || new_status.is_restricted()) {
        return promise.set_error(Status::Error(400, "Other chats can be only banned or unbanned"));
      }
      break;
    default:
      return promise.set_error(Status::Error(400, "Can't restrict the chat"));
  }

  CHECK(!old_status.is_creator());
  CHECK(!new_status.is_creator());

  if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_restrict_members()) {
    return promise.set_error(Status::Error(400, "Not enough rights to restrict/unrestrict chat member"));
  }

  if (old_status.is_member() && !new_status.is_member() && !new_status.is_banned()) {
    // we can't make participant Left without kicking it first
    auto on_result_promise = PromiseCreator::lambda([actor_id = actor_id(this), channel_id, participant_dialog_id,
                                                     new_status = std::move(new_status),
                                                     promise = std::move(promise)](Result<> result) mutable {
      if (result.is_error()) {
        return promise.set_error(result.move_as_error());
      }

      create_actor<SleepActor>(
          "RestrictChannelParticipantSleepActor", 1.0,
          PromiseCreator::lambda([actor_id, channel_id, participant_dialog_id, new_status = std::move(new_status),
                                  promise = std::move(promise)](Result<> result) mutable {
            if (result.is_error()) {
              return promise.set_error(result.move_as_error());
            }

            send_closure(actor_id, &DialogParticipantManager::restrict_channel_participant, channel_id,
                         participant_dialog_id, std::move(new_status), DialogParticipantStatus::Banned(0),
                         std::move(promise));
          }))
          .release();
    });

    promise = std::move(on_result_promise);
    new_status = DialogParticipantStatus::Banned(G()->unix_time() + 60);
  }

  if (new_status.is_member() && !old_status.is_member()) {
    // there is no way in server API to invite someone and change restrictions
    // we need to first change restrictions and then try to add the user
    CHECK(participant_dialog_id.get_type() == DialogType::User);
    new_status.set_is_member(false);
    auto on_result_promise =
        PromiseCreator::lambda([actor_id = actor_id(this), channel_id, participant_dialog_id, old_status = new_status,
                                promise = std::move(promise)](Result<> result) mutable {
          if (result.is_error()) {
            return promise.set_error(result.move_as_error());
          }

          create_actor<SleepActor>(
              "AddChannelParticipantSleepActor", 1.0,
              PromiseCreator::lambda([actor_id, channel_id, participant_dialog_id, old_status = std::move(old_status),
                                      promise = std::move(promise)](Result<> result) mutable {
                if (result.is_error()) {
                  return promise.set_error(result.move_as_error());
                }

                send_closure(actor_id, &DialogParticipantManager::add_channel_participant, channel_id,
                             participant_dialog_id.get_user_id(), old_status, std::move(promise));
              }))
              .release();
        });

    promise = std::move(on_result_promise);
  }

  if (participant_dialog_id.get_type() == DialogType::User) {
    speculative_add_channel_user(channel_id, participant_dialog_id.get_user_id(), new_status, old_status);
  }
  td_->create_handler<EditChannelBannedQuery>(std::move(promise))
      ->send(channel_id, participant_dialog_id, std::move(input_peer), new_status);
}

void DialogParticipantManager::on_set_channel_participant_status(ChannelId channel_id, DialogId participant_dialog_id,
                                                                 DialogParticipantStatus status) {
  if (G()->close_flag() || participant_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    return;
  }

  status.update_restrictions();
  if (have_channel_participant_cache(channel_id)) {
    update_channel_participant_status_cache(channel_id, participant_dialog_id, std::move(status));
  }
}

void DialogParticipantManager::speculative_add_channel_user(ChannelId channel_id, UserId user_id,
                                                            const DialogParticipantStatus &new_status,
                                                            const DialogParticipantStatus &old_status) {
  speculative_update_dialog_administrators(DialogId(channel_id), user_id, new_status, old_status);

  td_->contacts_manager_->speculative_add_channel_user(channel_id, user_id, new_status, old_status);
}

void DialogParticipantManager::send_update_add_chat_members_privacy_forbidden(DialogId dialog_id,
                                                                              vector<UserId> user_ids,
                                                                              const char *source) {
  td_->dialog_manager_->force_create_dialog(dialog_id, source);
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateAddChatMembersPrivacyForbidden>(
                   td_->dialog_manager_->get_chat_id_object(dialog_id, "updateAddChatMembersPrivacyForbidden"),
                   td_->contacts_manager_->get_user_ids_object(user_ids, source)));
}

void DialogParticipantManager::on_channel_participant_cache_timeout_callback(void *dialog_participant_manager_ptr,
                                                                             int64 channel_id_long) {
  if (G()->close_flag()) {
    return;
  }

  auto dialog_participant_manager = static_cast<DialogParticipantManager *>(dialog_participant_manager_ptr);
  send_closure_later(dialog_participant_manager->actor_id(dialog_participant_manager),
                     &DialogParticipantManager::on_channel_participant_cache_timeout, ChannelId(channel_id_long));
}

void DialogParticipantManager::on_channel_participant_cache_timeout(ChannelId channel_id) {
  if (G()->close_flag()) {
    return;
  }

  auto channel_participants_it = channel_participants_.find(channel_id);
  if (channel_participants_it == channel_participants_.end()) {
    return;
  }

  auto &participants = channel_participants_it->second.participants_;
  auto min_access_date = G()->unix_time() - CHANNEL_PARTICIPANT_CACHE_TIME;
  table_remove_if(participants,
                  [min_access_date](const auto &it) { return it.second.last_access_date_ < min_access_date; });

  if (participants.empty()) {
    channel_participants_.erase(channel_participants_it);
  } else {
    channel_participant_cache_timeout_.set_timeout_in(channel_id.get(), CHANNEL_PARTICIPANT_CACHE_TIME);
  }
}

bool DialogParticipantManager::have_channel_participant_cache(ChannelId channel_id) const {
  if (!td_->auth_manager_->is_bot()) {
    return false;
  }
  return td_->contacts_manager_->get_channel_status(channel_id).is_administrator();
}

void DialogParticipantManager::add_channel_participant_to_cache(ChannelId channel_id,
                                                                const DialogParticipant &dialog_participant,
                                                                bool allow_replace) {
  CHECK(channel_id.is_valid());
  CHECK(dialog_participant.is_valid());
  auto &participants = channel_participants_[channel_id];
  if (participants.participants_.empty()) {
    channel_participant_cache_timeout_.set_timeout_in(channel_id.get(), CHANNEL_PARTICIPANT_CACHE_TIME);
  }
  auto &participant_info = participants.participants_[dialog_participant.dialog_id_];
  if (participant_info.last_access_date_ > 0 && !allow_replace) {
    return;
  }
  participant_info.participant_ = dialog_participant;
  participant_info.last_access_date_ = G()->unix_time();
}

void DialogParticipantManager::update_channel_participant_status_cache(
    ChannelId channel_id, DialogId participant_dialog_id, DialogParticipantStatus &&dialog_participant_status) {
  CHECK(channel_id.is_valid());
  CHECK(participant_dialog_id.is_valid());
  auto channel_participants_it = channel_participants_.find(channel_id);
  if (channel_participants_it == channel_participants_.end()) {
    return;
  }
  auto &participants = channel_participants_it->second;
  auto it = participants.participants_.find(participant_dialog_id);
  if (it == participants.participants_.end()) {
    return;
  }
  auto &participant_info = it->second;
  LOG(INFO) << "Update cached status of " << participant_dialog_id << " in " << channel_id << " from "
            << participant_info.participant_.status_ << " to " << dialog_participant_status;
  participant_info.participant_.status_ = std::move(dialog_participant_status);
  participant_info.last_access_date_ = G()->unix_time();
}

void DialogParticipantManager::drop_channel_participant_cache(ChannelId channel_id) {
  channel_participants_.erase(channel_id);
}

const DialogParticipant *DialogParticipantManager::get_channel_participant_from_cache(ChannelId channel_id,
                                                                                      DialogId participant_dialog_id) {
  auto channel_participants_it = channel_participants_.find(channel_id);
  if (channel_participants_it == channel_participants_.end()) {
    return nullptr;
  }

  auto &participants = channel_participants_it->second.participants_;
  CHECK(!participants.empty());
  auto it = participants.find(participant_dialog_id);
  if (it != participants.end()) {
    it->second.participant_.status_.update_restrictions();
    it->second.last_access_date_ = G()->unix_time();
    return &it->second.participant_;
  }
  return nullptr;
}

}  // namespace td
