//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"

#include <limits>
#include <map>
#include <unordered_set>
#include <utility>

namespace td {

class CreateGroupCallQuery : public Td::ResultHandler {
  Promise<InputGroupCallId> promise_;
  DialogId dialog_id_;

 public:
  explicit CreateGroupCallQuery(Promise<InputGroupCallId> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;

    auto input_peer = td->messages_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::phone_createGroupCall(std::move(input_peer), Random::secure_int32())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_createGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateGroupCallQuery: " << to_string(ptr);

    auto group_call_ids = td->updates_manager_->get_update_new_group_call_ids(ptr.get());
    if (group_call_ids.empty()) {
      LOG(ERROR) << "Receive wrong CreateGroupCallQuery response " << to_string(ptr);
      return on_error(id, Status::Error(500, "Receive wrong response"));
    }
    auto group_call_id = group_call_ids[0];
    for (auto other_group_call_id : group_call_ids) {
      if (group_call_id != other_group_call_id) {
        LOG(ERROR) << "Receive wrong CreateGroupCallQuery response " << to_string(ptr);
        return on_error(id, Status::Error(500, "Receive wrong response"));
      }
    }

    td->updates_manager_->on_get_updates(
        std::move(ptr), PromiseCreator::lambda([promise = std::move(promise_), group_call_id](Unit) mutable {
          promise.set_value(std::move(group_call_id));
        }));
  }

  void on_error(uint64 id, Status status) override {
    td->messages_manager_->on_get_dialog_error(dialog_id_, status, "CreateGroupCallQuery");
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallQuery : public Td::ResultHandler {
  Promise<tl_object_ptr<telegram_api::phone_groupCall>> promise_;

 public:
  explicit GetGroupCallQuery(Promise<tl_object_ptr<telegram_api::phone_groupCall>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(
        G()->net_query_creator().create(telegram_api::phone_getGroupCall(input_group_call_id.get_input_group_call())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallParticipantQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;

 public:
  explicit GetGroupCallParticipantQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<int32> user_ids, vector<int32> audio_sources) {
    input_group_call_id_ = input_group_call_id;
    auto limit = narrow_cast<int32>(max(user_ids.size(), audio_sources.size()));
    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupParticipants(
        input_group_call_id.get_input_group_call(), std::move(user_ids), std::move(audio_sources), string(), limit)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->group_call_manager_->on_get_group_call_participants(input_group_call_id_, result_ptr.move_as_ok(), false,
                                                            string());

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallParticipantsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  string offset_;

 public:
  explicit GetGroupCallParticipantsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, string offset, int32 limit) {
    input_group_call_id_ = input_group_call_id;
    offset_ = std::move(offset);
    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupParticipants(
        input_group_call_id.get_input_group_call(), vector<int32>(), vector<int32>(), offset_, limit)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->group_call_manager_->on_get_group_call_participants(input_group_call_id_, result_ptr.move_as_ok(), true,
                                                            offset_);

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class JoinGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  uint64 generation_ = 0;

 public:
  explicit JoinGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  NetQueryRef send(InputGroupCallId input_group_call_id, const string &payload, bool is_muted, uint64 generation) {
    input_group_call_id_ = input_group_call_id;
    generation_ = generation;

    int32 flags = 0;
    if (is_muted) {
      flags |= telegram_api::phone_joinGroupCall::MUTED_MASK;
    }
    auto query = G()->net_query_creator().create(
        telegram_api::phone_joinGroupCall(flags, false /*ignored*/, input_group_call_id.get_input_group_call(),
                                          make_tl_object<telegram_api::dataJSON>(payload)));
    auto join_query_ref = query.get_weak();
    send_query(std::move(query));
    return join_query_ref;
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_joinGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for JoinGroupCallQuery with generation " << generation_ << ": " << to_string(ptr);
    td->group_call_manager_->process_join_group_call_response(input_group_call_id_, generation_, std::move(ptr),
                                                              std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class ToggleGroupCallSettingsQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleGroupCallSettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(int32 flags, InputGroupCallId input_group_call_id, bool join_muted) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_toggleGroupCallSettings(flags, input_group_call_id.get_input_group_call(), join_muted)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_toggleGroupCallSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ToggleGroupCallSettingsQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    if (status.message() == "GROUPCALL_NOT_MODIFIED") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class InviteToGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit InviteToGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::InputUser>> input_users) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_inviteToGroupCall(input_group_call_id.get_input_group_call(), std::move(input_users))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_inviteToGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for InviteToGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class EditGroupCallMemberQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditGroupCallMemberQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, UserId user_id, bool is_muted, int32 volume_level) {
    auto input_user = td->contacts_manager_->get_input_user(user_id);
    CHECK(input_user != nullptr);

    int32 flags = 0;
    if (volume_level) {
      flags |= telegram_api::phone_editGroupCallMember::VOLUME_MASK;
    } else if (is_muted) {
      flags |= telegram_api::phone_editGroupCallMember::MUTED_MASK;
    }

    send_query(G()->net_query_creator().create(telegram_api::phone_editGroupCallMember(
        flags, false /*ignored*/, input_group_call_id.get_input_group_call(), std::move(input_user), volume_level)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_editGroupCallMember>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditGroupCallMemberQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class CheckGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CheckGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, int32 audio_source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_checkGroupCall(input_group_call_id.get_input_group_call(), audio_source)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_checkGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    bool success = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CheckGroupCallQuery: " << success;

    if (success) {
      promise_.set_value(Unit());
    } else {
      promise_.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
    }
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class LeaveGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit LeaveGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, int32 audio_source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_leaveGroupCall(input_group_call_id.get_input_group_call(), audio_source)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_leaveGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for LeaveGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

class DiscardGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DiscardGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_discardGroupCall(input_group_call_id.get_input_group_call())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_discardGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DiscardGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

struct GroupCallManager::GroupCall {
  GroupCallId group_call_id;
  DialogId dialog_id;
  bool is_inited = false;
  bool is_active = false;
  bool is_joined = false;
  bool need_rejoin = false;
  bool is_being_left = false;
  bool is_speaking = false;
  bool can_self_unmute = false;
  bool can_be_managed = false;
  bool syncing_participants = false;
  bool loaded_all_participants = false;
  bool mute_new_participants = false;
  bool allowed_change_mute_new_participants = false;
  int32 participant_count = 0;
  int32 version = -1;
  int32 duration = 0;
  int32 audio_source = 0;
  int32 joined_date = 0;
  vector<Promise<Unit>> after_join;

  bool have_pending_mute_new_participants = false;
  bool pending_mute_new_participants = false;
};

struct GroupCallManager::GroupCallParticipants {
  vector<GroupCallParticipant> participants;
  string next_offset;
  int64 min_order = std::numeric_limits<int64>::max();

  bool are_administrators_loaded = false;
  vector<UserId> administrator_user_ids;

  std::map<int32, vector<GroupCallParticipant>> pending_version_updates_;
  std::map<int32, vector<GroupCallParticipant>> pending_mute_updates_;
};

struct GroupCallManager::GroupCallRecentSpeakers {
  vector<std::pair<UserId, int32>> users;  // user + time; sorted by time
  bool is_changed = false;
  vector<std::pair<UserId, bool>> last_sent_users;
};

struct GroupCallManager::PendingJoinRequest {
  NetQueryRef query_ref;
  uint64 generation = 0;
  int32 audio_source = 0;
  Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> promise;
};

GroupCallManager::GroupCallManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  check_group_call_is_joined_timeout_.set_callback(on_check_group_call_is_joined_timeout_callback);
  check_group_call_is_joined_timeout_.set_callback_data(static_cast<void *>(this));

  pending_send_speaking_action_timeout_.set_callback(on_pending_send_speaking_action_timeout_callback);
  pending_send_speaking_action_timeout_.set_callback_data(static_cast<void *>(this));

  recent_speaker_update_timeout_.set_callback(on_recent_speaker_update_timeout_callback);
  recent_speaker_update_timeout_.set_callback_data(static_cast<void *>(this));

  sync_participants_timeout_.set_callback(on_sync_participants_timeout_callback);
  sync_participants_timeout_.set_callback_data(static_cast<void *>(this));
}

GroupCallManager::~GroupCallManager() = default;

void GroupCallManager::tear_down() {
  parent_.reset();
}

void GroupCallManager::on_check_group_call_is_joined_timeout_callback(void *group_call_manager_ptr,
                                                                      int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_check_group_call_is_joined_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_check_group_call_is_joined_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Receive check group call is_joined timeout in " << group_call_id;
  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (!group_call->is_joined || pending_join_requests_.count(input_group_call_id) != 0 ||
      check_group_call_is_joined_timeout_.has_timeout(group_call_id.get())) {
    return;
  }

  auto audio_source = group_call->audio_source;
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, audio_source](Result<Unit> &&result) mutable {
        if (result.is_error() && result.error().message() == "GROUPCALL_JOIN_MISSING") {
          send_closure(actor_id, &GroupCallManager::on_group_call_left, input_group_call_id, audio_source, true);
          result = Unit();
        }
        send_closure(actor_id, &GroupCallManager::finish_check_group_call_is_joined, input_group_call_id, audio_source,
                     std::move(result));
      });
  td_->create_handler<CheckGroupCallQuery>(std::move(promise))->send(input_group_call_id, audio_source);
}

void GroupCallManager::on_pending_send_speaking_action_timeout_callback(void *group_call_manager_ptr,
                                                                        int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_send_speaking_action_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_send_speaking_action_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Receive send_speaking_action timeout in " << group_call_id;
  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited && group_call->dialog_id.is_valid());
  if (!group_call->is_joined || !group_call->is_speaking) {
    return;
  }

  on_user_speaking_in_group_call(group_call_id, td_->contacts_manager_->get_my_id(), G()->unix_time());

  pending_send_speaking_action_timeout_.add_timeout_in(group_call_id.get(), 4.0);

  td_->messages_manager_->send_dialog_action(group_call->dialog_id, MessageId(), DialogAction::get_speaking_action(),
                                             Promise<Unit>());
}

void GroupCallManager::on_recent_speaker_update_timeout_callback(void *group_call_manager_ptr,
                                                                 int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_recent_speaker_update_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_recent_speaker_update_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Receive recent speaker update timeout in " << group_call_id;
  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();

  get_recent_speakers(get_group_call(input_group_call_id),
                      false);  // will update the list and send updateGroupCall if needed
}

void GroupCallManager::on_sync_participants_timeout_callback(void *group_call_manager_ptr, int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager), &GroupCallManager::on_sync_participants_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_sync_participants_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Receive sync participants timeout in " << group_call_id;
  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();

  sync_group_call_participants(input_group_call_id);
}

GroupCallId GroupCallManager::get_group_call_id(InputGroupCallId input_group_call_id, DialogId dialog_id) {
  if (td_->auth_manager_->is_bot() || !input_group_call_id.is_valid()) {
    return GroupCallId();
  }
  return add_group_call(input_group_call_id, dialog_id)->group_call_id;
}

Result<InputGroupCallId> GroupCallManager::get_input_group_call_id(GroupCallId group_call_id) {
  if (!group_call_id.is_valid()) {
    return Status::Error(400, "Invalid group call identifier specified");
  }
  if (group_call_id.get() <= 0 || group_call_id.get() > max_group_call_id_.get()) {
    return Status::Error(400, "Wrong group call identifier specified");
  }
  CHECK(static_cast<size_t>(group_call_id.get()) <= input_group_call_ids_.size());
  auto input_group_call_id = input_group_call_ids_[group_call_id.get() - 1];
  LOG(DEBUG) << "Found " << input_group_call_id;
  return input_group_call_id;
}

GroupCallId GroupCallManager::get_next_group_call_id(InputGroupCallId input_group_call_id) {
  max_group_call_id_ = GroupCallId(max_group_call_id_.get() + 1);
  input_group_call_ids_.push_back(input_group_call_id);
  return max_group_call_id_;
}

GroupCallManager::GroupCall *GroupCallManager::add_group_call(InputGroupCallId input_group_call_id,
                                                              DialogId dialog_id) {
  CHECK(!td_->auth_manager_->is_bot());
  auto &group_call = group_calls_[input_group_call_id];
  if (group_call == nullptr) {
    group_call = make_unique<GroupCall>();
    group_call->group_call_id = get_next_group_call_id(input_group_call_id);
    LOG(INFO) << "Add " << input_group_call_id << " from " << dialog_id << " as " << group_call->group_call_id;
  }
  if (!group_call->dialog_id.is_valid()) {
    group_call->dialog_id = dialog_id;
  }
  return group_call.get();
}

const GroupCallManager::GroupCall *GroupCallManager::get_group_call(InputGroupCallId input_group_call_id) const {
  auto it = group_calls_.find(input_group_call_id);
  if (it == group_calls_.end()) {
    return nullptr;
  } else {
    return it->second.get();
  }
}

GroupCallManager::GroupCall *GroupCallManager::get_group_call(InputGroupCallId input_group_call_id) {
  auto it = group_calls_.find(input_group_call_id);
  if (it == group_calls_.end()) {
    return nullptr;
  } else {
    return it->second.get();
  }
}

Status GroupCallManager::can_manage_group_calls(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::Chat: {
      auto chat_id = dialog_id.get_chat_id();
      if (!td_->contacts_manager_->get_chat_permissions(chat_id).can_manage_calls()) {
        return Status::Error(400, "Not enough rights in the chat");
      }
      break;
    }
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      switch (td_->contacts_manager_->get_channel_type(channel_id)) {
        case ChannelType::Unknown:
          return Status::Error(400, "Chat info not found");
        case ChannelType::Megagroup:
          // OK
          break;
        case ChannelType::Broadcast:
          return Status::Error(400, "Chat is not a group");
        default:
          UNREACHABLE();
          break;
      }
      if (!td_->contacts_manager_->get_channel_permissions(channel_id).can_manage_calls()) {
        return Status::Error(400, "Not enough rights in the chat");
      }
      break;
    }
    case DialogType::User:
    case DialogType::SecretChat:
      return Status::Error(400, "Chat can't have a voice chat");
    case DialogType::None:
      // OK
      break;
    default:
      UNREACHABLE();
  }
  return Status::OK();
}

bool GroupCallManager::can_manage_group_call(InputGroupCallId input_group_call_id) const {
  auto group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr) {
    return false;
  }
  return can_manage_group_calls(group_call->dialog_id).is_ok();
}

void GroupCallManager::create_voice_chat(DialogId dialog_id, Promise<GroupCallId> &&promise) {
  if (!dialog_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Invalid chat identifier specified"));
  }
  if (!td_->messages_manager_->have_dialog_force(dialog_id)) {
    return promise.set_error(Status::Error(400, "Chat not found"));
  }
  if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
    return promise.set_error(Status::Error(400, "Can't access chat"));
  }

  TRY_STATUS_PROMISE(promise, can_manage_group_calls(dialog_id));

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, promise = std::move(promise)](Result<InputGroupCallId> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure(actor_id, &GroupCallManager::on_voice_chat_created, dialog_id, result.move_as_ok(),
                       std::move(promise));
        }
      });
  td_->create_handler<CreateGroupCallQuery>(std::move(query_promise))->send(dialog_id);
}

void GroupCallManager::on_voice_chat_created(DialogId dialog_id, InputGroupCallId input_group_call_id,
                                             Promise<GroupCallId> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }
  if (!input_group_call_id.is_valid()) {
    return promise.set_error(Status::Error(500, "Receive invalid group call identifier"));
  }

  td_->messages_manager_->on_update_dialog_group_call(dialog_id, true, true, "on_voice_chat_created");
  td_->messages_manager_->on_update_dialog_group_call_id(dialog_id, input_group_call_id);

  promise.set_value(get_group_call_id(input_group_call_id, dialog_id));
}

void GroupCallManager::get_group_call(GroupCallId group_call_id,
                                      Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto group_call = get_group_call(input_group_call_id);
  if (group_call != nullptr && group_call->is_inited) {
    return promise.set_value(get_group_call_object(group_call, get_recent_speakers(group_call, false)));
  }

  reload_group_call(input_group_call_id, std::move(promise));
}

void GroupCallManager::on_update_group_call_rights(InputGroupCallId input_group_call_id) {
  auto group_call = get_group_call(input_group_call_id);
  if (need_group_call_participants(input_group_call_id, group_call)) {
    CHECK(group_call != nullptr && group_call->is_inited);
    try_load_group_call_administrators(input_group_call_id, group_call->dialog_id);

    auto participants_it = group_call_participants_.find(input_group_call_id);
    if (participants_it != group_call_participants_.end()) {
      CHECK(participants_it->second != nullptr);
      if (participants_it->second->are_administrators_loaded) {
        update_group_call_participants_can_be_muted(
            input_group_call_id, can_manage_group_calls(group_call->dialog_id).is_ok(), participants_it->second.get());
      }
    }
  }

  if (group_call != nullptr && group_call->is_inited) {
    bool can_be_managed = group_call->is_active && can_manage_group_calls(group_call->dialog_id).is_ok();
    if (can_be_managed != group_call->can_be_managed) {
      group_call->can_be_managed = can_be_managed;
      send_update_group_call(group_call, "on_update_group_call_rights");
    }
  }

  reload_group_call(input_group_call_id, Auto());
}

void GroupCallManager::reload_group_call(InputGroupCallId input_group_call_id,
                                         Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  auto &queries = load_group_call_queries_[input_group_call_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](
                                                    Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
      send_closure(actor_id, &GroupCallManager::finish_get_group_call, input_group_call_id, std::move(result));
    });
    td_->create_handler<GetGroupCallQuery>(std::move(query_promise))->send(input_group_call_id);
  }
}

void GroupCallManager::finish_get_group_call(InputGroupCallId input_group_call_id,
                                             Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
  auto it = load_group_call_queries_.find(input_group_call_id);
  CHECK(it != load_group_call_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  load_group_call_queries_.erase(it);

  if (result.is_ok()) {
    td_->contacts_manager_->on_get_users(std::move(result.ok_ref()->users_), "finish_get_group_call");

    if (update_group_call(result.ok()->call_, DialogId()) != input_group_call_id) {
      LOG(ERROR) << "Expected " << input_group_call_id << ", but received " << to_string(result.ok());
      result = Status::Error(500, "Receive another group call");
    }
  }

  if (result.is_error()) {
    for (auto &promise : promises) {
      promise.set_error(result.error().clone());
    }
    return;
  }

  auto call = result.move_as_ok();
  process_group_call_participants(input_group_call_id, std::move(call->participants_), true, false);
  if (need_group_call_participants(input_group_call_id)) {
    auto participants_it = group_call_participants_.find(input_group_call_id);
    if (participants_it != group_call_participants_.end()) {
      CHECK(participants_it->second != nullptr);
      if (participants_it->second->next_offset.empty()) {
        participants_it->second->next_offset = std::move(call->participants_next_offset_);
      }
    }
  }

  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  for (auto &promise : promises) {
    if (promise) {
      promise.set_value(get_group_call_object(group_call, get_recent_speakers(group_call, false)));
    }
  }
}

void GroupCallManager::finish_check_group_call_is_joined(InputGroupCallId input_group_call_id, int32 audio_source,
                                                         Result<Unit> &&result) {
  LOG(INFO) << "Finish check group call is_joined for " << input_group_call_id;

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (!group_call->is_joined || pending_join_requests_.count(input_group_call_id) != 0 ||
      check_group_call_is_joined_timeout_.has_timeout(group_call->group_call_id.get()) ||
      group_call->audio_source != audio_source) {
    return;
  }

  int32 next_timeout = result.is_ok() ? CHECK_GROUP_CALL_IS_JOINED_TIMEOUT : 1;
  check_group_call_is_joined_timeout_.set_timeout_in(group_call->group_call_id.get(), next_timeout);
}

bool GroupCallManager::get_group_call_mute_new_participants(const GroupCall *group_call) {
  return group_call->have_pending_mute_new_participants ? group_call->pending_mute_new_participants
                                                        : group_call->mute_new_participants;
}

bool GroupCallManager::need_group_call_participants(InputGroupCallId input_group_call_id) const {
  return need_group_call_participants(input_group_call_id, get_group_call(input_group_call_id));
}

bool GroupCallManager::need_group_call_participants(InputGroupCallId input_group_call_id,
                                                    const GroupCall *group_call) const {
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return false;
  }
  if (group_call->is_joined || group_call->need_rejoin || pending_join_requests_.count(input_group_call_id) != 0) {
    return true;
  }
  return false;
}

void GroupCallManager::on_get_group_call_participants(
    InputGroupCallId input_group_call_id, tl_object_ptr<telegram_api::phone_groupParticipants> &&participants,
    bool is_load, const string &offset) {
  LOG(INFO) << "Receive group call participants: " << to_string(participants);

  CHECK(participants != nullptr);
  td_->contacts_manager_->on_get_users(std::move(participants->users_), "on_get_group_call_participants");

  if (!need_group_call_participants(input_group_call_id)) {
    return;
  }

  bool is_sync = is_load && offset.empty();
  if (is_sync) {
    auto group_call = get_group_call(input_group_call_id);
    CHECK(group_call != nullptr && group_call->is_inited);
    is_sync = group_call->syncing_participants;
    if (is_sync) {
      group_call->syncing_participants = false;

      if (group_call->version >= participants->version_) {
        LOG(INFO) << "Ignore result of outdated participants sync with version " << participants->version_ << " in "
                  << input_group_call_id << " from " << group_call->dialog_id << ", because current version is "
                  << group_call->version;
        return;
      }
      LOG(INFO) << "Finish syncing participants in " << input_group_call_id << " from " << group_call->dialog_id
                << " with version " << participants->version_;
      group_call->version = participants->version_;
    }
  }

  auto is_empty = participants->participants_.empty();
  process_group_call_participants(input_group_call_id, std::move(participants->participants_), is_load, is_sync);

  if (!is_sync) {
    on_receive_group_call_version(input_group_call_id, participants->version_);
  }

  if (is_load) {
    auto participants_it = group_call_participants_.find(input_group_call_id);
    if (participants_it != group_call_participants_.end()) {
      CHECK(participants_it->second != nullptr);
      if (participants_it->second->next_offset == offset) {
        participants_it->second->next_offset = std::move(participants->next_offset_);
      }
    }

    if (is_empty || is_sync) {
      bool need_update = false;
      auto group_call = get_group_call(input_group_call_id);
      CHECK(group_call != nullptr && group_call->is_inited);
      if (is_empty && !group_call->loaded_all_participants) {
        group_call->loaded_all_participants = true;
        need_update = true;
      }

      auto real_participant_count = participants->count_;
      if (is_empty) {
        auto known_participant_count = participants_it != group_call_participants_.end()
                                           ? static_cast<int32>(participants_it->second->participants.size())
                                           : 0;
        if (real_participant_count != known_participant_count) {
          LOG(ERROR) << "Receive participant count " << real_participant_count << ", but know "
                     << known_participant_count << " participants in " << input_group_call_id << " from "
                     << group_call->dialog_id;
          real_participant_count = known_participant_count;
        }
      }
      if (real_participant_count != group_call->participant_count) {
        if (!is_sync) {
          LOG(ERROR) << "Have participant count " << group_call->participant_count << " instead of "
                     << real_participant_count << " in " << input_group_call_id << " from " << group_call->dialog_id;
        }
        group_call->participant_count = real_participant_count;
        need_update = true;

        update_group_call_dialog(group_call, "on_get_group_call_participants");
      }
      if (!is_empty && is_sync && group_call->loaded_all_participants && group_call->participant_count > 50) {
        group_call->loaded_all_participants = false;
        need_update = true;
      }
      if (process_pending_group_call_participant_updates(input_group_call_id)) {
        need_update = false;
      }
      if (need_update) {
        send_update_group_call(group_call, "on_get_group_call_participants");
      }
    }
  }
}

GroupCallManager::GroupCallParticipants *GroupCallManager::add_group_call_participants(
    InputGroupCallId input_group_call_id) {
  CHECK(need_group_call_participants(input_group_call_id));

  auto &participants = group_call_participants_[input_group_call_id];
  if (participants == nullptr) {
    participants = make_unique<GroupCallParticipants>();
  }
  return participants.get();
}

GroupCallParticipant *GroupCallManager::get_group_call_participant(InputGroupCallId input_group_call_id,
                                                                   UserId user_id) {
  return get_group_call_participant(add_group_call_participants(input_group_call_id), user_id);
}

GroupCallParticipant *GroupCallManager::get_group_call_participant(GroupCallParticipants *group_call_participants,
                                                                   UserId user_id) {
  for (auto &group_call_participant : group_call_participants->participants) {
    if (group_call_participant.user_id == user_id) {
      return &group_call_participant;
    }
  }
  return nullptr;
}

void GroupCallManager::on_update_group_call_participants(
    InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::groupCallParticipant>> &&participants,
    int32 version) {
  if (!need_group_call_participants(input_group_call_id)) {
    int32 diff = 0;
    bool need_update = false;
    auto group_call = get_group_call(input_group_call_id);
    for (auto &group_call_participant : participants) {
      GroupCallParticipant participant(group_call_participant);
      if (participant.user_id == td_->contacts_manager_->get_my_id() && group_call != nullptr &&
          group_call->is_inited && group_call->is_joined &&
          (participant.joined_date == 0) == (participant.audio_source == group_call->audio_source)) {
        on_group_call_left_impl(group_call, participant.joined_date == 0);
        need_update = true;
      }
      if (participant.joined_date == 0) {
        diff--;
        remove_recent_group_call_speaker(input_group_call_id, participant.user_id);
      } else {
        if (participant.is_just_joined) {
          diff++;
        }
        on_participant_speaking_in_group_call(input_group_call_id, participant);
      }
    }

    if (group_call != nullptr && group_call->is_inited && group_call->is_active && group_call->version == -1) {
      if (diff != 0 && (group_call->participant_count != 0 || diff > 0)) {
        group_call->participant_count += diff;
        if (group_call->participant_count < 0) {
          LOG(ERROR) << "Participant count became negative in " << input_group_call_id << " from "
                     << group_call->dialog_id << " after applying " << to_string(participants);
          group_call->participant_count = 0;
        }
        update_group_call_dialog(group_call, "on_update_group_call_participants");
        need_update = true;
      }
    }
    if (need_update) {
      send_update_group_call(group_call, "on_update_group_call_participants");
    }

    LOG(INFO) << "Ignore updateGroupCallParticipants in " << input_group_call_id;
    return;
  }
  if (version <= 0) {
    LOG(ERROR) << "Ignore updateGroupCallParticipants with invalid version " << version << " in "
               << input_group_call_id;
    return;
  }
  if (participants.empty()) {
    LOG(INFO) << "Ignore empty updateGroupCallParticipants with version " << version << " in " << input_group_call_id;
    return;
  }

  auto *group_call_participants = add_group_call_participants(input_group_call_id);
  auto &pending_mute_updates = group_call_participants->pending_mute_updates_[version];
  vector<GroupCallParticipant> version_updates;
  for (auto &group_call_participant : participants) {
    GroupCallParticipant participant(group_call_participant);
    if (participant.is_min && participant.joined_date != 0) {
      auto old_participant = get_group_call_participant(group_call_participants, participant.user_id);
      if (old_participant == nullptr) {
        LOG(INFO) << "Can't apply min update about " << participant.user_id << " in " << input_group_call_id;
        // TODO instead of synchronization, such participants can be received through GetGroupCallParticipantQuery
        on_receive_group_call_version(input_group_call_id, version, true);
        return;
      }

      participant.update_from(*old_participant);
      CHECK(!participant.is_min);
    }

    if (GroupCallParticipant::is_versioned_update(group_call_participant)) {
      version_updates.push_back(std::move(participant));
    } else {
      pending_mute_updates.push_back(std::move(participant));
    }
  }
  if (!version_updates.empty()) {
    auto &pending_version_updates = group_call_participants->pending_version_updates_[version];
    if (version_updates.size() <= pending_version_updates.size()) {
      LOG(INFO) << "Receive duplicate updateGroupCallParticipants with version " << version << " in "
                << input_group_call_id;
      return;
    }
    pending_version_updates = std::move(version_updates);
  }

  process_pending_group_call_participant_updates(input_group_call_id);
}

bool GroupCallManager::process_pending_group_call_participant_updates(InputGroupCallId input_group_call_id) {
  if (!need_group_call_participants(input_group_call_id)) {
    return false;
  }

  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return false;
  }
  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->version == -1 || !group_call->is_active) {
    return false;
  }

  int32 diff = 0;
  bool is_left = false;
  bool need_rejoin = true;
  auto &pending_version_updates = participants_it->second->pending_version_updates_;
  while (!pending_version_updates.empty()) {
    auto it = pending_version_updates.begin();
    auto version = it->first;
    auto &participants = it->second;
    if (version <= group_call->version) {
      auto my_user_id = td_->contacts_manager_->get_my_id();
      auto my_participant = get_group_call_participant(participants_it->second.get(), my_user_id);
      for (auto &participant : participants) {
        on_participant_speaking_in_group_call(input_group_call_id, participant);
        if (participant.user_id == my_user_id && (my_participant == nullptr || my_participant->is_fake ||
                                                  my_participant->joined_date < participant.joined_date ||
                                                  (my_participant->joined_date <= participant.joined_date &&
                                                   my_participant->audio_source != participant.audio_source))) {
          process_group_call_participant(input_group_call_id, std::move(participant));
        }
      }
      LOG(INFO) << "Ignore already applied updateGroupCallParticipants with version " << version << " in "
                << input_group_call_id << " from " << group_call->dialog_id;
      pending_version_updates.erase(it);
      continue;
    }

    if (version == group_call->version + 1) {
      group_call->version = version;
      for (auto &participant : participants) {
        GroupCallParticipant group_call_participant(participant);
        if (group_call_participant.user_id == td_->contacts_manager_->get_my_id() && group_call->is_joined &&
            (group_call_participant.joined_date == 0) ==
                (group_call_participant.audio_source == group_call->audio_source)) {
          is_left = true;
          if (group_call_participant.joined_date != 0) {
            need_rejoin = false;
          }
        }
        diff += process_group_call_participant(input_group_call_id, std::move(group_call_participant));
      }
      pending_version_updates.erase(it);
    } else if (!group_call->syncing_participants) {
      // found a gap
      LOG(INFO) << "Receive " << participants.size() << " group call participant updates with version " << version
                << ", but current version is " << group_call->version;
      sync_participants_timeout_.add_timeout_in(group_call->group_call_id.get(), 1.0);
      break;
    }
  }

  auto &pending_mute_updates = participants_it->second->pending_mute_updates_;
  while (!pending_mute_updates.empty()) {
    auto it = pending_mute_updates.begin();
    auto version = it->first;
    if (version <= group_call->version) {
      auto &participants = it->second;
      for (auto &participant : participants) {
        on_participant_speaking_in_group_call(input_group_call_id, participant);
        int mute_diff = process_group_call_participant(input_group_call_id, std::move(participant));
        CHECK(mute_diff == 0);
      }
      pending_mute_updates.erase(it);
      continue;
    }
    on_receive_group_call_version(input_group_call_id, version);
    break;
  }

  if (pending_version_updates.empty() && pending_mute_updates.empty()) {
    sync_participants_timeout_.cancel_timeout(group_call->group_call_id.get());
  }

  bool need_update = false;
  if (diff != 0 && (group_call->participant_count != 0 || diff > 0)) {
    group_call->participant_count += diff;
    if (group_call->participant_count < 0) {
      LOG(ERROR) << "Participant count became negative in " << input_group_call_id << " from " << group_call->dialog_id;
      group_call->participant_count = 0;
    }
    need_update = true;
    update_group_call_dialog(group_call, "process_pending_group_call_participant_updates");
  }
  if (is_left && group_call->is_joined) {
    on_group_call_left_impl(group_call, need_rejoin);
    need_update = true;
  }
  if (need_update) {
    send_update_group_call(group_call, "process_pending_group_call_participant_updates");
  }
  try_clear_group_call_participants(input_group_call_id);

  return need_update;
}

void GroupCallManager::sync_group_call_participants(InputGroupCallId input_group_call_id) {
  if (!need_group_call_participants(input_group_call_id)) {
    return;
  }

  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);

  sync_participants_timeout_.cancel_timeout(group_call->group_call_id.get());

  if (group_call->syncing_participants) {
    return;
  }
  group_call->syncing_participants = true;

  LOG(INFO) << "Force participants synchronization in " << input_group_call_id << " from " << group_call->dialog_id;
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](Result<Unit> &&result) {
    if (result.is_error()) {
      send_closure(actor_id, &GroupCallManager::on_sync_group_call_participants_failed, input_group_call_id);
    }
  });
  td_->create_handler<GetGroupCallParticipantsQuery>(std::move(promise))->send(input_group_call_id, string(), 100);
}

void GroupCallManager::on_sync_group_call_participants_failed(InputGroupCallId input_group_call_id) {
  if (G()->close_flag() || !need_group_call_participants(input_group_call_id)) {
    return;
  }

  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  CHECK(group_call->syncing_participants);
  group_call->syncing_participants = false;

  sync_participants_timeout_.add_timeout_in(group_call->group_call_id.get(), 1.0);
}

int64 GroupCallManager::get_real_participant_order(const GroupCallParticipant &participant, int64 min_order) const {
  auto real_order = participant.get_real_order();
  if (real_order < min_order && participant.user_id == td_->contacts_manager_->get_my_id()) {
    return min_order;
  }
  if (real_order >= min_order) {
    return real_order;
  }
  return 0;
}

void GroupCallManager::process_group_call_participants(
    InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::groupCallParticipant>> &&participants,
    bool is_load, bool is_sync) {
  if (!need_group_call_participants(input_group_call_id)) {
    for (auto &participant : participants) {
      GroupCallParticipant group_call_participant(participant);
      if (!group_call_participant.is_valid()) {
        LOG(ERROR) << "Receive invalid " << to_string(participant);
        continue;
      }

      on_participant_speaking_in_group_call(input_group_call_id, group_call_participant);
    }
    return;
  }

  std::unordered_set<UserId, UserIdHash> old_participant_user_ids;
  if (is_sync) {
    auto participants_it = group_call_participants_.find(input_group_call_id);
    if (participants_it != group_call_participants_.end()) {
      CHECK(participants_it->second != nullptr);
      for (auto &participant : participants_it->second->participants) {
        old_participant_user_ids.insert(participant.user_id);
      }
    }
  }

  int64 min_order = std::numeric_limits<int64>::max();
  for (auto &participant : participants) {
    GroupCallParticipant group_call_participant(participant);
    if (!group_call_participant.is_valid()) {
      LOG(ERROR) << "Receive invalid " << to_string(participant);
      continue;
    }
    if (group_call_participant.is_min) {
      LOG(ERROR) << "Receive unexpected min " << to_string(participant);
      continue;
    }

    auto real_order = group_call_participant.get_real_order();
    if (real_order > min_order) {
      LOG(ERROR) << "Receive call participant with order " << real_order << " after call participant with order "
                 << min_order;
    } else {
      min_order = real_order;
    }
    if (is_sync) {
      old_participant_user_ids.erase(group_call_participant.user_id);
    }
    process_group_call_participant(input_group_call_id, std::move(group_call_participant));
  }
  if (is_sync) {
    auto participants_it = group_call_participants_.find(input_group_call_id);
    if (participants_it != group_call_participants_.end()) {
      CHECK(participants_it->second != nullptr);
      auto &group_participants = participants_it->second->participants;
      for (auto participant_it = group_participants.begin(); participant_it != group_participants.end();) {
        auto &participant = *participant_it;
        if (old_participant_user_ids.count(participant.user_id) == 0) {
          CHECK(participant.order == 0 || participant.order >= min_order);
          ++participant_it;
          continue;
        }

        // not synced user, needs to be deleted
        if (participant.order != 0) {
          CHECK(participant.order >= participants_it->second->min_order);
          if (participant.user_id == td_->contacts_manager_->get_my_id()) {
            if (participant.order != min_order) {
              participant.order = min_order;
              send_update_group_call_participant(input_group_call_id, participant);
            }
          } else {
            participant.order = 0;
            send_update_group_call_participant(input_group_call_id, participant);
          }
        }
        participant_it = group_participants.erase(participant_it);
      }
      if (participants_it->second->min_order < min_order) {
        // if previously known more users, adjust min_order
        participants_it->second->min_order = min_order;
      }
    }
  }
  if (is_load) {
    auto participants_it = group_call_participants_.find(input_group_call_id);
    if (participants_it != group_call_participants_.end()) {
      CHECK(participants_it->second != nullptr);
      auto old_min_order = participants_it->second->min_order;
      if (old_min_order > min_order) {
        participants_it->second->min_order = min_order;

        for (auto &participant : participants_it->second->participants) {
          auto real_order = get_real_participant_order(participant, min_order);
          if (old_min_order > real_order && real_order >= min_order) {
            CHECK(participant.order == 0 || participant.order == old_min_order);
            participant.order = real_order;
            send_update_group_call_participant(input_group_call_id, participant);
          }
        }
      }
    }
  }
}

bool GroupCallManager::update_group_call_participant_can_be_muted(bool can_manage,
                                                                  const GroupCallParticipants *participants,
                                                                  GroupCallParticipant &participant) {
  bool is_self = participant.user_id == td_->contacts_manager_->get_my_id();
  bool is_admin = td::contains(participants->administrator_user_ids, participant.user_id);
  return participant.update_can_be_muted(can_manage, is_self, is_admin);
}

void GroupCallManager::update_group_call_participants_can_be_muted(InputGroupCallId input_group_call_id,
                                                                   bool can_manage,
                                                                   GroupCallParticipants *participants) {
  CHECK(participants != nullptr);
  LOG(INFO) << "Update group call participants can_be_muted in " << input_group_call_id;
  for (auto &participant : participants->participants) {
    if (update_group_call_participant_can_be_muted(can_manage, participants, participant) && participant.order != 0) {
      send_update_group_call_participant(input_group_call_id, participant);
    }
  }
}

int GroupCallManager::process_group_call_participant(InputGroupCallId input_group_call_id,
                                                     GroupCallParticipant &&participant) {
  if (!participant.is_valid()) {
    LOG(ERROR) << "Receive invalid " << participant;
    return 0;
  }
  if (!need_group_call_participants(input_group_call_id)) {
    return 0;
  }

  LOG(INFO) << "Process " << participant << " in " << input_group_call_id;

  if (participant.user_id == td_->contacts_manager_->get_my_id()) {
    auto *group_call = get_group_call(input_group_call_id);
    CHECK(group_call != nullptr && group_call->is_inited);
    if (group_call->is_joined && group_call->is_active) {
      auto can_self_unmute = !participant.get_is_muted_by_admin();
      if (can_self_unmute != group_call->can_self_unmute) {
        group_call->can_self_unmute = can_self_unmute;
        send_update_group_call(group_call, "process_group_call_participant");
      }
    }
  }

  bool can_manage = can_manage_group_call(input_group_call_id);
  auto *participants = add_group_call_participants(input_group_call_id);
  for (size_t i = 0; i < participants->participants.size(); i++) {
    auto &old_participant = participants->participants[i];
    if (old_participant.user_id == participant.user_id) {
      if (participant.joined_date == 0) {
        LOG(INFO) << "Remove " << old_participant;
        if (old_participant.order != 0) {
          send_update_group_call_participant(input_group_call_id, participant);
        }
        remove_recent_group_call_speaker(input_group_call_id, participant.user_id);
        participants->participants.erase(participants->participants.begin() + i);
        return -1;
      }

      participant.update_from(old_participant);

      participant.is_just_joined = false;
      participant.order = get_real_participant_order(participant, participants->min_order);
      update_group_call_participant_can_be_muted(can_manage, participants, participant);

      LOG(INFO) << "Edit " << old_participant << " to " << participant;
      if (old_participant != participant && (old_participant.order != 0 || participant.order != 0)) {
        send_update_group_call_participant(input_group_call_id, participant);
      }
      on_participant_speaking_in_group_call(input_group_call_id, participant);
      old_participant = std::move(participant);
      return 0;
    }
  }

  if (participant.joined_date == 0) {
    LOG(INFO) << "Remove unknown " << participant;
    remove_recent_group_call_speaker(input_group_call_id, participant.user_id);
    return -1;
  }

  CHECK(!participant.is_min);
  int diff = participant.is_just_joined ? 1 : 0;
  if (participant.is_just_joined) {
    LOG(INFO) << "Add new " << participant;
  } else {
    LOG(INFO) << "Receive new " << participant;
  }
  participant.order = get_real_participant_order(participant, participants->min_order);
  participant.is_just_joined = false;
  update_group_call_participant_can_be_muted(can_manage, participants, participant);
  participants->participants.push_back(std::move(participant));
  if (participants->participants.back().order != 0) {
    send_update_group_call_participant(input_group_call_id, participants->participants.back());
  }
  on_participant_speaking_in_group_call(input_group_call_id, participants->participants.back());
  return diff;
}

int32 GroupCallManager::cancel_join_group_call_request(InputGroupCallId input_group_call_id) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end()) {
    return 0;
  }

  CHECK(it->second != nullptr);
  if (!it->second->query_ref.empty()) {
    cancel_query(it->second->query_ref);
  }
  it->second->promise.set_error(Status::Error(200, "Cancelled"));
  auto audio_source = it->second->audio_source;
  pending_join_requests_.erase(it);
  return audio_source;
}

void GroupCallManager::join_group_call(GroupCallId group_call_id,
                                       td_api::object_ptr<td_api::groupCallPayload> &&payload, int32 audio_source,
                                       bool is_muted,
                                       Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (group_call->is_inited && !group_call->is_active) {
    return promise.set_error(Status::Error(400, "Group call is finished"));
  }
  bool need_update = false;
  bool is_rejoin = group_call->need_rejoin;
  if (group_call->need_rejoin) {
    group_call->need_rejoin = false;
    need_update = true;
  }

  cancel_join_group_call_request(input_group_call_id);

  if (audio_source == 0) {
    return promise.set_error(Status::Error(400, "Audio source must be non-zero"));
  }
  if (payload == nullptr) {
    return promise.set_error(Status::Error(400, "Payload must be non-empty"));
  }
  if (!clean_input_string(payload->ufrag_)) {
    return promise.set_error(Status::Error(400, "Payload ufrag must be encoded in UTF-8"));
  }
  if (!clean_input_string(payload->pwd_)) {
    return promise.set_error(Status::Error(400, "Payload pwd must be encoded in UTF-8"));
  }
  for (auto &fingerprint : payload->fingerprints_) {
    if (fingerprint == nullptr) {
      return promise.set_error(Status::Error(400, "Payload fingerprint must be non-empty"));
    }
    if (!clean_input_string(fingerprint->hash_)) {
      return promise.set_error(Status::Error(400, "Fingerprint hash must be encoded in UTF-8"));
    }
    if (!clean_input_string(fingerprint->setup_)) {
      return promise.set_error(Status::Error(400, "Fingerprint setup must be encoded in UTF-8"));
    }
    if (!clean_input_string(fingerprint->fingerprint_)) {
      return promise.set_error(Status::Error(400, "Fingerprint must be encoded in UTF-8"));
    }
  }

  if (group_call->is_being_left) {
    group_call->is_being_left = false;
    need_update |= group_call->is_joined;
  }

  auto json_payload = json_encode<string>(json_object([&payload, audio_source](auto &o) {
    o("ufrag", payload->ufrag_);
    o("pwd", payload->pwd_);
    o("fingerprints", json_array(payload->fingerprints_,
                                 [](const td_api::object_ptr<td_api::groupCallPayloadFingerprint> &fingerprint) {
                                   return json_object([&fingerprint](auto &o) {
                                     o("hash", fingerprint->hash_);
                                     o("setup", fingerprint->setup_);
                                     o("fingerprint", fingerprint->fingerprint_);
                                   });
                                 }));
    o("ssrc", audio_source);
  }));

  auto generation = ++join_group_request_generation_;
  auto &request = pending_join_requests_[input_group_call_id];
  request = make_unique<PendingJoinRequest>();
  request->generation = generation;
  request->audio_source = audio_source;
  request->promise = std::move(promise);

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation, input_group_call_id](Result<Unit> &&result) {
        CHECK(result.is_error());
        send_closure(actor_id, &GroupCallManager::finish_join_group_call, input_group_call_id, generation,
                     result.move_as_error());
      });
  request->query_ref = td_->create_handler<JoinGroupCallQuery>(std::move(query_promise))
                           ->send(input_group_call_id, json_payload, is_muted, generation);

  if (group_call->is_inited && td_->contacts_manager_->have_user_force(td_->contacts_manager_->get_my_id())) {
    GroupCallParticipant group_call_participant;
    group_call_participant.user_id = td_->contacts_manager_->get_my_id();
    group_call_participant.audio_source = audio_source;
    group_call_participant.joined_date = G()->unix_time();
    // if can_self_unmute has never been inited from self-participant,
    // it contains reasonable default "!call.mute_new_participants || call.can_be_managed"
    group_call_participant.server_is_muted_by_admin =
        !group_call->can_self_unmute && !can_manage_group_call(input_group_call_id);
    group_call_participant.server_is_muted_by_themselves = is_muted && !group_call_participant.server_is_muted_by_admin;
    group_call_participant.is_just_joined = !is_rejoin;
    group_call_participant.is_fake = true;
    int diff = process_group_call_participant(input_group_call_id, std::move(group_call_participant));
    if (diff != 0) {
      CHECK(diff == 1);
      group_call->participant_count++;
      need_update = true;
      update_group_call_dialog(group_call, "join_group_call");
    }
  }

  if (group_call->is_inited && need_update) {
    send_update_group_call(group_call, "join_group_call");
  }

  try_load_group_call_administrators(input_group_call_id, group_call->dialog_id);
}

void GroupCallManager::try_load_group_call_administrators(InputGroupCallId input_group_call_id, DialogId dialog_id) {
  if (!dialog_id.is_valid() || !need_group_call_participants(input_group_call_id) ||
      can_manage_group_calls(dialog_id).is_error()) {
    LOG(INFO) << "Don't need to load administrators in " << input_group_call_id << " from " << dialog_id;
    return;
  }

  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](Result<DialogParticipants> &&result) {
        send_closure(actor_id, &GroupCallManager::finish_load_group_call_administrators, input_group_call_id,
                     std::move(result));
      });
  td_->contacts_manager_->search_dialog_participants(
      dialog_id, string(), 100, DialogParticipantsFilter(DialogParticipantsFilter::Type::Administrators), true,
      std::move(promise));
}

void GroupCallManager::finish_load_group_call_administrators(InputGroupCallId input_group_call_id,
                                                             Result<DialogParticipants> &&result) {
  if (G()->close_flag()) {
    return;
  }
  if (result.is_error()) {
    LOG(WARNING) << "Failed to get administrators of " << input_group_call_id << ": " << result.error();
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!need_group_call_participants(input_group_call_id, group_call)) {
    return;
  }
  CHECK(group_call != nullptr);
  if (!group_call->dialog_id.is_valid() || !can_manage_group_calls(group_call->dialog_id).is_ok()) {
    return;
  }

  vector<UserId> administrator_user_ids;
  auto participants = result.move_as_ok();
  for (auto &administrator : participants.participants_) {
    if (administrator.status.can_manage_calls() && administrator.user_id != td_->contacts_manager_->get_my_id()) {
      administrator_user_ids.push_back(administrator.user_id);
    }
  }

  auto *group_call_participants = add_group_call_participants(input_group_call_id);
  if (group_call_participants->are_administrators_loaded &&
      group_call_participants->administrator_user_ids == administrator_user_ids) {
    return;
  }

  LOG(INFO) << "Set administrators of " << input_group_call_id << " to " << administrator_user_ids;
  group_call_participants->are_administrators_loaded = true;
  group_call_participants->administrator_user_ids = std::move(administrator_user_ids);

  update_group_call_participants_can_be_muted(input_group_call_id, true, group_call_participants);
}

void GroupCallManager::process_join_group_call_response(InputGroupCallId input_group_call_id, uint64 generation,
                                                        tl_object_ptr<telegram_api::Updates> &&updates,
                                                        Promise<Unit> &&promise) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end() || it->second->generation != generation) {
    LOG(INFO) << "Ignore JoinGroupCallQuery response with " << input_group_call_id << " and generation " << generation;
    return;
  }

  td_->updates_manager_->on_get_updates(std::move(updates),
                                        PromiseCreator::lambda([promise = std::move(promise)](Unit) mutable {
                                          promise.set_error(Status::Error(500, "Wrong join response received"));
                                        }));
}

Result<td_api::object_ptr<td_api::groupCallJoinResponse>> GroupCallManager::get_group_call_join_response_object(
    string json_response) {
  auto r_value = json_decode(json_response);
  if (r_value.is_error()) {
    return Status::Error("Can't parse JSON object");
  }

  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected an Object");
  }

  auto &value_object = value.get_object();
  TRY_RESULT(transport, get_json_object_field(value_object, "transport", JsonValue::Type::Object, false));
  CHECK(transport.type() == JsonValue::Type::Object);
  auto &transport_object = transport.get_object();

  TRY_RESULT(candidates, get_json_object_field(transport_object, "candidates", JsonValue::Type::Array, false));
  TRY_RESULT(fingerprints, get_json_object_field(transport_object, "fingerprints", JsonValue::Type::Array, false));
  TRY_RESULT(ufrag, get_json_object_string_field(transport_object, "ufrag", false));
  TRY_RESULT(pwd, get_json_object_string_field(transport_object, "pwd", false));
  // skip "xmlns", "rtcp-mux"

  vector<td_api::object_ptr<td_api::groupCallPayloadFingerprint>> fingerprints_object;
  for (auto &fingerprint : fingerprints.get_array()) {
    if (fingerprint.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object as fingerprint");
    }
    auto &fingerprint_object = fingerprint.get_object();
    TRY_RESULT(hash, get_json_object_string_field(fingerprint_object, "hash", false));
    TRY_RESULT(setup, get_json_object_string_field(fingerprint_object, "setup", false));
    TRY_RESULT(fingerprint_value, get_json_object_string_field(fingerprint_object, "fingerprint", false));
    fingerprints_object.push_back(
        td_api::make_object<td_api::groupCallPayloadFingerprint>(hash, setup, fingerprint_value));
  }

  vector<td_api::object_ptr<td_api::groupCallJoinResponseCandidate>> candidates_object;
  for (auto &candidate : candidates.get_array()) {
    if (candidate.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object as candidate");
    }
    auto &candidate_object = candidate.get_object();
    TRY_RESULT(port, get_json_object_string_field(candidate_object, "port", false));
    TRY_RESULT(protocol, get_json_object_string_field(candidate_object, "protocol", false));
    TRY_RESULT(network, get_json_object_string_field(candidate_object, "network", false));
    TRY_RESULT(generation, get_json_object_string_field(candidate_object, "generation", false));
    TRY_RESULT(id, get_json_object_string_field(candidate_object, "id", false));
    TRY_RESULT(component, get_json_object_string_field(candidate_object, "component", false));
    TRY_RESULT(foundation, get_json_object_string_field(candidate_object, "foundation", false));
    TRY_RESULT(priority, get_json_object_string_field(candidate_object, "priority", false));
    TRY_RESULT(ip, get_json_object_string_field(candidate_object, "ip", false));
    TRY_RESULT(type, get_json_object_string_field(candidate_object, "type", false));
    TRY_RESULT(tcp_type, get_json_object_string_field(candidate_object, "tcptype"));
    TRY_RESULT(rel_addr, get_json_object_string_field(candidate_object, "rel-addr"));
    TRY_RESULT(rel_port, get_json_object_string_field(candidate_object, "rel-port"));
    candidates_object.push_back(td_api::make_object<td_api::groupCallJoinResponseCandidate>(
        port, protocol, network, generation, id, component, foundation, priority, ip, type, tcp_type, rel_addr,
        rel_port));
  }

  auto payload = td_api::make_object<td_api::groupCallPayload>(ufrag, pwd, std::move(fingerprints_object));
  return td_api::make_object<td_api::groupCallJoinResponse>(std::move(payload), std::move(candidates_object));
}

bool GroupCallManager::on_join_group_call_response(InputGroupCallId input_group_call_id, string json_response) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end()) {
    return false;
  }
  CHECK(it->second != nullptr);

  auto result = get_group_call_join_response_object(std::move(json_response));
  bool need_update = false;
  if (result.is_error()) {
    LOG(ERROR) << "Failed to parse join response JSON object: " << result.error().message();
    it->second->promise.set_error(Status::Error(500, "Receive invalid join group call response payload"));
  } else {
    auto group_call = get_group_call(input_group_call_id);
    CHECK(group_call != nullptr);
    group_call->is_joined = true;
    group_call->need_rejoin = false;
    group_call->is_being_left = false;
    group_call->joined_date = G()->unix_time();
    group_call->audio_source = it->second->audio_source;
    it->second->promise.set_value(result.move_as_ok());
    check_group_call_is_joined_timeout_.set_timeout_in(group_call->group_call_id.get(),
                                                       CHECK_GROUP_CALL_IS_JOINED_TIMEOUT);
    need_update = true;
  }
  pending_join_requests_.erase(it);
  try_clear_group_call_participants(input_group_call_id);
  process_group_call_after_join_requests(input_group_call_id);
  return need_update;
}

void GroupCallManager::finish_join_group_call(InputGroupCallId input_group_call_id, uint64 generation, Status error) {
  CHECK(error.is_error());
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end() || (generation != 0 && it->second->generation != generation)) {
    return;
  }
  it->second->promise.set_error(std::move(error));
  pending_join_requests_.erase(it);
  try_clear_group_call_participants(input_group_call_id);
  process_group_call_after_join_requests(input_group_call_id);
}

void GroupCallManager::process_group_call_after_join_requests(InputGroupCallId input_group_call_id) {
  GroupCall *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    return;
  }
  if (pending_join_requests_.count(input_group_call_id) != 0 || group_call->need_rejoin) {
    LOG(ERROR) << "Failed to process after-join requests: " << pending_join_requests_.count(input_group_call_id) << " "
               << group_call->need_rejoin;
    return;
  }
  if (group_call->after_join.empty()) {
    return;
  }

  auto promises = std::move(group_call->after_join);
  reset_to_empty(group_call->after_join);
  if (!group_call->is_active || !group_call->is_joined) {
    for (auto &promise : promises) {
      promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
    }
  } else {
    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
  }
}

void GroupCallManager::toggle_group_call_mute_new_participants(GroupCallId group_call_id, bool mute_new_participants,
                                                               Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->can_be_managed ||
      !group_call->allowed_change_mute_new_participants) {
    return promise.set_error(Status::Error(400, "Can't change mute_new_participant setting"));
  }

  if (mute_new_participants == get_group_call_mute_new_participants(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_mute_new_participants = mute_new_participants;
  if (!group_call->have_pending_mute_new_participants) {
    group_call->have_pending_mute_new_participants = true;
    send_toggle_group_call_mute_new_participants_query(input_group_call_id, mute_new_participants);
  }
  send_update_group_call(group_call, "toggle_group_call_mute_new_participants");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_mute_new_participants_query(InputGroupCallId input_group_call_id,
                                                                          bool mute_new_participants) {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, mute_new_participants](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_mute_new_participants, input_group_call_id,
                     mute_new_participants, std::move(result));
      });
  int32 flags = telegram_api::phone_toggleGroupCallSettings::JOIN_MUTED_MASK;
  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))
      ->send(flags, input_group_call_id, mute_new_participants);
}

void GroupCallManager::on_toggle_group_call_mute_new_participants(InputGroupCallId input_group_call_id,
                                                                  bool mute_new_participants, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return;
  }

  CHECK(group_call->have_pending_mute_new_participants);
  if (result.is_error()) {
    group_call->have_pending_mute_new_participants = false;
    if (group_call->can_be_managed && group_call->allowed_change_mute_new_participants) {
      LOG(ERROR) << "Failed to set mute_new_participants to " << mute_new_participants << " in " << input_group_call_id
                 << ": " << result.error();
    }
    group_call->have_pending_mute_new_participants = false;
    if (group_call->pending_mute_new_participants != group_call->mute_new_participants) {
      send_update_group_call(group_call, "on_toggle_group_call_mute_new_participants failed");
    }
  } else {
    if (group_call->pending_mute_new_participants != mute_new_participants) {
      // need to send another request
      send_toggle_group_call_mute_new_participants_query(input_group_call_id,
                                                         group_call->pending_mute_new_participants);
      return;
    }

    group_call->have_pending_mute_new_participants = false;
    if (group_call->mute_new_participants != mute_new_participants) {
      LOG(ERROR) << "Failed to set mute_new_participants to " << mute_new_participants << " in " << input_group_call_id;
      send_update_group_call(group_call, "on_toggle_group_call_mute_new_participants failed 2");
    }
  }
}

void GroupCallManager::invite_group_call_participants(GroupCallId group_call_id, vector<UserId> &&user_ids,
                                                      Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  vector<tl_object_ptr<telegram_api::InputUser>> input_users;
  auto my_user_id = td_->contacts_manager_->get_my_id();
  for (auto user_id : user_ids) {
    auto input_user = td_->contacts_manager_->get_input_user(user_id);
    if (input_user == nullptr) {
      return promise.set_error(Status::Error(400, "User not found"));
    }

    if (user_id == my_user_id) {
      // can't invite self
      continue;
    }
    input_users.push_back(std::move(input_user));
  }

  if (input_users.empty()) {
    return promise.set_value(Unit());
  }

  td_->create_handler<InviteToGroupCallQuery>(std::move(promise))->send(input_group_call_id, std::move(input_users));
}

void GroupCallManager::set_group_call_participant_is_speaking(GroupCallId group_call_id, int32 audio_source,
                                                              bool is_speaking, Promise<Unit> &&promise, int32 date) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return promise.set_value(Unit());
  }
  if (!group_call->is_joined) {
    if (pending_join_requests_.count(input_group_call_id) || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, audio_source, is_speaking,
                                  promise = std::move(promise), date](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_value(Unit());
            } else {
              send_closure(actor_id, &GroupCallManager::set_group_call_participant_is_speaking, group_call_id,
                           audio_source, is_speaking, std::move(promise), date);
            }
          }));
      return;
    }
    return promise.set_value(Unit());
  }
  if (audio_source == 0) {
    audio_source = group_call->audio_source;
  }

  bool recursive = false;
  if (date == 0) {
    date = G()->unix_time();
  } else {
    recursive = true;
  }
  if (audio_source != group_call->audio_source && !recursive && is_speaking &&
      check_group_call_is_joined_timeout_.has_timeout(group_call_id.get())) {
    check_group_call_is_joined_timeout_.set_timeout_in(group_call_id.get(), CHECK_GROUP_CALL_IS_JOINED_TIMEOUT);
  }
  UserId user_id =
      set_group_call_participant_is_speaking_by_source(input_group_call_id, audio_source, is_speaking, date);
  if (!user_id.is_valid()) {
    if (!recursive) {
      auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, audio_source, is_speaking,
                                                   promise = std::move(promise), date](Result<Unit> &&result) mutable {
        if (G()->close_flag()) {
          return promise.set_error(Status::Error(500, "Request aborted"));
        }
        if (result.is_error()) {
          promise.set_value(Unit());
        } else {
          send_closure(actor_id, &GroupCallManager::set_group_call_participant_is_speaking, group_call_id, audio_source,
                       is_speaking, std::move(promise), date);
        }
      });
      td_->create_handler<GetGroupCallParticipantQuery>(std::move(query_promise))
          ->send(input_group_call_id, {}, {audio_source});
    } else {
      LOG(INFO) << "Failed to find participant with source " << audio_source << " in " << group_call_id << " from "
                << group_call->dialog_id;
      promise.set_value(Unit());
    }
    return;
  }

  if (is_speaking) {
    on_user_speaking_in_group_call(group_call_id, user_id, date, recursive);
  }

  if (group_call->audio_source == audio_source && group_call->dialog_id.is_valid() &&
      group_call->is_speaking != is_speaking) {
    group_call->is_speaking = is_speaking;
    if (is_speaking) {
      pending_send_speaking_action_timeout_.add_timeout_in(group_call_id.get(), 0.0);
    }
  }

  promise.set_value(Unit());
}

void GroupCallManager::toggle_group_call_participant_is_muted(GroupCallId group_call_id, UserId user_id, bool is_muted,
                                                              Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  }
  if (!group_call->is_joined) {
    if (pending_join_requests_.count(input_group_call_id) || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, user_id, is_muted,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
            } else {
              send_closure(actor_id, &GroupCallManager::toggle_group_call_participant_is_muted, group_call_id, user_id,
                           is_muted, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  }
  if (!td_->contacts_manager_->have_input_user(user_id)) {
    return promise.set_error(Status::Error(400, "Have no access to the user"));
  }

  auto participants = add_group_call_participants(input_group_call_id);
  auto participant = get_group_call_participant(participants, user_id);
  if (participant == nullptr) {
    return promise.set_error(Status::Error(400, "Can't find group call participant"));
  }

  bool is_self = user_id == td_->contacts_manager_->get_my_id();
  bool can_manage = can_manage_group_call(input_group_call_id);
  bool is_admin = td::contains(participants->administrator_user_ids, user_id);

  auto participant_copy = *participant;
  if (!participant_copy.set_pending_is_muted(is_muted, can_manage, is_self, is_admin)) {
    return promise.set_error(Status::Error(400, PSLICE() << "Can't " << (is_muted ? "" : "un") << "mute user"));
  }
  if (participant_copy == *participant) {
    return promise.set_value(Unit());
  }
  *participant = std::move(participant_copy);

  participant->pending_is_muted_generation = ++toggle_is_muted_generation_;
  if (participant->order != 0) {
    send_update_group_call_participant(input_group_call_id, *participant);
  }

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, user_id,
                                               generation = participant->pending_is_muted_generation,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    }
    send_closure(actor_id, &GroupCallManager::on_toggle_group_call_participant_is_muted, input_group_call_id, user_id,
                 generation, std::move(promise));
  });
  td_->create_handler<EditGroupCallMemberQuery>(std::move(query_promise))
      ->send(input_group_call_id, user_id, is_muted, 0);
}

void GroupCallManager::on_toggle_group_call_participant_is_muted(InputGroupCallId input_group_call_id, UserId user_id,
                                                                 uint64 generation, Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(Unit());
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined) {
    return promise.set_value(Unit());
  }

  auto participant = get_group_call_participant(input_group_call_id, user_id);
  if (participant == nullptr || participant->pending_is_muted_generation != generation) {
    return promise.set_value(Unit());
  }

  CHECK(participant->have_pending_is_muted);
  participant->have_pending_is_muted = false;
  if (participant->server_is_muted_by_themselves != participant->pending_is_muted_by_themselves ||
      participant->server_is_muted_by_admin != participant->pending_is_muted_by_admin ||
      participant->server_is_muted_locally != participant->pending_is_muted_locally) {
    LOG(ERROR) << "Failed to mute/unmute " << user_id << " in " << input_group_call_id;
    if (participant->order != 0) {
      send_update_group_call_participant(input_group_call_id, *participant);
    }
  }
  promise.set_value(Unit());
}

void GroupCallManager::set_group_call_participant_volume_level(GroupCallId group_call_id, UserId user_id,
                                                               int32 volume_level, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  if (volume_level < GroupCallParticipant::MIN_VOLUME_LEVEL || volume_level > GroupCallParticipant::MAX_VOLUME_LEVEL) {
    return promise.set_error(Status::Error(400, "Wrong volume level specified"));
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  }
  if (!group_call->is_joined) {
    if (pending_join_requests_.count(input_group_call_id) || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, user_id, volume_level,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
            } else {
              send_closure(actor_id, &GroupCallManager::set_group_call_participant_volume_level, group_call_id, user_id,
                           volume_level, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  }
  if (!td_->contacts_manager_->have_input_user(user_id)) {
    return promise.set_error(Status::Error(400, "Have no access to the user"));
  }

  if (user_id == td_->contacts_manager_->get_my_id()) {
    return promise.set_error(Status::Error(400, "Can't change self volume level"));
  }

  auto participant = get_group_call_participant(input_group_call_id, user_id);
  if (participant == nullptr) {
    return promise.set_error(Status::Error(400, "Can't find group call participant"));
  }

  if (participant->get_volume_level() == volume_level) {
    return promise.set_value(Unit());
  }

  participant->pending_volume_level = volume_level;
  participant->pending_volume_level_generation = ++set_volume_level_generation_;
  if (participant->order != 0) {
    send_update_group_call_participant(input_group_call_id, *participant);
  }

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, user_id,
                                               generation = participant->pending_volume_level_generation,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
    }
    send_closure(actor_id, &GroupCallManager::on_set_group_call_participant_volume_level, input_group_call_id, user_id,
                 generation, std::move(promise));
  });
  td_->create_handler<EditGroupCallMemberQuery>(std::move(query_promise))
      ->send(input_group_call_id, user_id, false, volume_level);
}

void GroupCallManager::on_set_group_call_participant_volume_level(InputGroupCallId input_group_call_id, UserId user_id,
                                                                  uint64 generation, Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(Unit());
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined) {
    return promise.set_value(Unit());
  }

  auto participant = get_group_call_participant(input_group_call_id, user_id);
  if (participant == nullptr || participant->pending_volume_level_generation != generation) {
    return promise.set_value(Unit());
  }

  CHECK(participant->pending_volume_level != 0);
  if (participant->volume_level != participant->pending_volume_level) {
    LOG(ERROR) << "Failed to set volume level of " << user_id << " in " << input_group_call_id;
    participant->pending_volume_level = 0;
    if (participant->order != 0) {
      send_update_group_call_participant(input_group_call_id, *participant);
    }
  } else {
    participant->pending_volume_level = 0;
  }
  promise.set_value(Unit());
}

void GroupCallManager::load_group_call_participants(GroupCallId group_call_id, int32 limit, Promise<Unit> &&promise) {
  if (limit <= 0) {
    return promise.set_error(Status::Error(400, "Parameter limit must be positive"));
  }

  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!need_group_call_participants(input_group_call_id, group_call)) {
    return promise.set_error(Status::Error(400, "Can't load group call participants"));
  }
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->loaded_all_participants) {
    return promise.set_value(Unit());
  }

  string next_offset;
  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it != group_call_participants_.end()) {
    CHECK(participants_it->second != nullptr);
    next_offset = participants_it->second->next_offset;
  }
  td_->create_handler<GetGroupCallParticipantsQuery>(std::move(promise))
      ->send(input_group_call_id, std::move(next_offset), limit);
}

void GroupCallManager::leave_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined ||
      group_call->is_being_left) {
    if (cancel_join_group_call_request(input_group_call_id) != 0) {
      try_clear_group_call_participants(input_group_call_id);
      process_group_call_after_join_requests(input_group_call_id);
      return promise.set_value(Unit());
    }
    if (group_call->need_rejoin) {
      group_call->need_rejoin = false;
      send_update_group_call(group_call, "leave_group_call");
      try_clear_group_call_participants(input_group_call_id);
      process_group_call_after_join_requests(input_group_call_id);
      return promise.set_value(Unit());
    }
    return promise.set_error(Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  }
  auto audio_source = cancel_join_group_call_request(input_group_call_id);
  if (audio_source == 0) {
    audio_source = group_call->audio_source;
  }
  group_call->is_being_left = true;
  group_call->need_rejoin = false;
  send_update_group_call(group_call, "leave_group_call");

  process_group_call_after_join_requests(input_group_call_id);

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, audio_source,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_ok()) {
      // just in case
      send_closure(actor_id, &GroupCallManager::on_group_call_left, input_group_call_id, audio_source, false);
    }
    promise.set_result(std::move(result));
  });
  td_->create_handler<LeaveGroupCallQuery>(std::move(query_promise))->send(input_group_call_id, audio_source);
}

void GroupCallManager::on_group_call_left(InputGroupCallId input_group_call_id, int32 audio_source, bool need_rejoin) {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->is_joined && group_call->audio_source == audio_source) {
    on_group_call_left_impl(group_call, need_rejoin);
    send_update_group_call(group_call, "on_group_call_left");
  }
}

void GroupCallManager::on_group_call_left_impl(GroupCall *group_call, bool need_rejoin) {
  CHECK(group_call != nullptr && group_call->is_inited && group_call->is_joined);
  group_call->is_joined = false;
  group_call->need_rejoin = need_rejoin && !group_call->is_being_left;
  if (group_call->need_rejoin && group_call->dialog_id.is_valid()) {
    auto dialog_id = group_call->dialog_id;
    if (!td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read) ||
        (dialog_id.get_type() == DialogType::Chat &&
         !td_->contacts_manager_->get_chat_status(dialog_id.get_chat_id()).is_member())) {
      group_call->need_rejoin = false;
    }
  }
  group_call->is_being_left = false;
  group_call->is_speaking = false;
  group_call->can_be_managed = false;
  group_call->joined_date = 0;
  group_call->audio_source = 0;
  check_group_call_is_joined_timeout_.cancel_timeout(group_call->group_call_id.get());
  auto input_group_call_id = get_input_group_call_id(group_call->group_call_id).ok();
  try_clear_group_call_participants(input_group_call_id);
  process_group_call_after_join_requests(input_group_call_id);
}

void GroupCallManager::discard_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  td_->create_handler<DiscardGroupCallQuery>(std::move(promise))->send(input_group_call_id);
}

void GroupCallManager::on_update_group_call(tl_object_ptr<telegram_api::GroupCall> group_call_ptr, DialogId dialog_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  if (dialog_id != DialogId() && !dialog_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(group_call_ptr) << " in invalid " << dialog_id;
    dialog_id = DialogId();
  }
  auto input_group_call_id = update_group_call(group_call_ptr, dialog_id);
  if (input_group_call_id.is_valid()) {
    LOG(INFO) << "Update " << input_group_call_id << " from " << dialog_id;
  } else {
    LOG(ERROR) << "Receive invalid " << to_string(group_call_ptr);
  }
}

void GroupCallManager::try_clear_group_call_participants(InputGroupCallId input_group_call_id) {
  if (need_group_call_participants(input_group_call_id)) {
    return;
  }

  remove_recent_group_call_speaker(input_group_call_id, td_->contacts_manager_->get_my_id());

  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return;
  }

  auto participants = std::move(participants_it->second);
  CHECK(participants != nullptr);
  group_call_participants_.erase(participants_it);

  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  LOG(INFO) << "Clear participants in " << input_group_call_id << " from " << group_call->dialog_id;
  if (group_call->loaded_all_participants) {
    group_call->loaded_all_participants = false;
    send_update_group_call(group_call, "try_clear_group_call_participants");
  }
  group_call->version = -1;

  for (auto &participant : participants->participants) {
    if (participant.order != 0) {
      CHECK(participant.order >= participants->min_order);
      participant.order = 0;
      send_update_group_call_participant(input_group_call_id, participant);
    }
  }
}

InputGroupCallId GroupCallManager::update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr,
                                                     DialogId dialog_id) {
  CHECK(group_call_ptr != nullptr);

  InputGroupCallId input_group_call_id;
  GroupCall call;
  call.is_inited = true;

  string join_params;
  switch (group_call_ptr->get_id()) {
    case telegram_api::groupCall::ID: {
      auto group_call = static_cast<const telegram_api::groupCall *>(group_call_ptr.get());
      input_group_call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      call.is_active = true;
      call.mute_new_participants = group_call->join_muted_;
      call.allowed_change_mute_new_participants = group_call->can_change_join_muted_;
      call.participant_count = group_call->participants_count_;
      call.version = group_call->version_;
      if (group_call->params_ != nullptr) {
        join_params = std::move(group_call->params_->data_);
      }
      break;
    }
    case telegram_api::groupCallDiscarded::ID: {
      auto group_call = static_cast<const telegram_api::groupCallDiscarded *>(group_call_ptr.get());
      input_group_call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      call.duration = group_call->duration_;
      finish_join_group_call(input_group_call_id, 0, Status::Error(400, "Group call ended"));
      break;
    }
    default:
      UNREACHABLE();
  }
  if (!input_group_call_id.is_valid() || call.participant_count < 0) {
    return {};
  }

  bool need_update = false;
  auto *group_call = add_group_call(input_group_call_id, dialog_id);
  call.group_call_id = group_call->group_call_id;
  call.dialog_id = dialog_id.is_valid() ? dialog_id : group_call->dialog_id;
  call.can_be_managed = call.is_active && can_manage_group_calls(call.dialog_id).is_ok();
  call.can_self_unmute = call.is_active && (!call.mute_new_participants || call.can_be_managed);
  if (!group_call->dialog_id.is_valid()) {
    group_call->dialog_id = dialog_id;
  }
  LOG(INFO) << "Update " << call.group_call_id << " with " << group_call->participant_count
            << " participants and version " << group_call->version;
  if (!group_call->is_inited) {
    call.is_joined = group_call->is_joined;
    call.need_rejoin = group_call->need_rejoin;
    call.is_being_left = group_call->is_being_left;
    call.is_speaking = group_call->is_speaking;
    call.syncing_participants = group_call->syncing_participants;
    call.loaded_all_participants = group_call->loaded_all_participants;
    call.audio_source = group_call->audio_source;
    *group_call = std::move(call);

    need_update = true;
    if (need_group_call_participants(input_group_call_id, group_call)) {
      // init version
      if (process_pending_group_call_participant_updates(input_group_call_id)) {
        need_update = false;
      }
      try_load_group_call_administrators(input_group_call_id, group_call->dialog_id);
    } else {
      group_call->version = -1;
    }
  } else {
    if (!group_call->is_active) {
      // never update ended calls
    } else if (!call.is_active) {
      // always update to an ended call, droping also is_joined, is_speaking and other local flags
      auto promises = std::move(group_call->after_join);
      for (auto &promise : promises) {
        promise.set_error(Status::Error(400, "Group call ended"));
      }
      *group_call = std::move(call);
      need_update = true;
    } else {
      auto mute_flags_changed =
          call.mute_new_participants != group_call->mute_new_participants ||
          call.allowed_change_mute_new_participants != group_call->allowed_change_mute_new_participants;
      if (mute_flags_changed && call.version >= group_call->version) {
        auto old_mute_new_participants = get_group_call_mute_new_participants(group_call);
        need_update |= (call.allowed_change_mute_new_participants && call.can_be_managed) !=
                       (group_call->allowed_change_mute_new_participants && group_call->can_be_managed);
        group_call->mute_new_participants = call.mute_new_participants;
        group_call->allowed_change_mute_new_participants = call.allowed_change_mute_new_participants;
        if (old_mute_new_participants != get_group_call_mute_new_participants(group_call)) {
          need_update = true;
        }
      }
      if (call.can_be_managed != group_call->can_be_managed) {
        group_call->can_be_managed = call.can_be_managed;
        need_update = true;
      }
      if (call.version > group_call->version) {
        if (group_call->version != -1) {
          // if we know group call version, then update participants only by corresponding updates
          on_receive_group_call_version(input_group_call_id, call.version);
        } else {
          if (call.participant_count != group_call->participant_count) {
            LOG(INFO) << "Set " << call.group_call_id << " participant count to " << call.participant_count;
            group_call->participant_count = call.participant_count;
            need_update = true;
          }
          if (need_group_call_participants(input_group_call_id, group_call) && !join_params.empty() &&
              group_call->version == -1) {
            LOG(INFO) << "Init " << call.group_call_id << " version to " << call.version;
            group_call->version = call.version;
            if (process_pending_group_call_participant_updates(input_group_call_id)) {
              need_update = false;
            }
          }
        }
      }
    }
  }
  update_group_call_dialog(group_call, "update_group_call");
  if (!group_call->is_active && group_call_recent_speakers_.erase(group_call->group_call_id) != 0) {
    need_update = true;
  }
  if (!join_params.empty()) {
    need_update |= on_join_group_call_response(input_group_call_id, std::move(join_params));
  }
  if (need_update) {
    send_update_group_call(group_call, "update_group_call");
  }
  try_clear_group_call_participants(input_group_call_id);
  return input_group_call_id;
}

void GroupCallManager::on_receive_group_call_version(InputGroupCallId input_group_call_id, int32 version,
                                                     bool immediate_sync) {
  auto *group_call = get_group_call(input_group_call_id);
  if (!need_group_call_participants(input_group_call_id, group_call)) {
    return;
  }
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->version == -1) {
    return;
  }
  if (version <= group_call->version) {
    return;
  }
  if (group_call->syncing_participants) {
    return;
  }

  // found a gap
  LOG(INFO) << "Receive version " << version << " for group call " << input_group_call_id;
  auto *group_call_participants = add_group_call_participants(input_group_call_id);
  group_call_participants->pending_version_updates_[version];  // reserve place for updates
  if (immediate_sync) {
    sync_participants_timeout_.set_timeout_in(group_call->group_call_id.get(), 0.0);
  } else {
    sync_participants_timeout_.add_timeout_in(group_call->group_call_id.get(), 1.0);
  }
}

void GroupCallManager::on_participant_speaking_in_group_call(InputGroupCallId input_group_call_id,
                                                             const GroupCallParticipant &participant) {
  auto active_date = td::max(participant.active_date, participant.joined_date - 60);
  if (active_date < G()->unix_time() - RECENT_SPEAKER_TIMEOUT) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr) {
    return;
  }

  on_user_speaking_in_group_call(group_call->group_call_id, participant.user_id, active_date, true);
}

void GroupCallManager::on_user_speaking_in_group_call(GroupCallId group_call_id, UserId user_id, int32 date,
                                                      bool recursive) {
  if (G()->close_flag()) {
    return;
  }
  if (date < G()->unix_time() - RECENT_SPEAKER_TIMEOUT) {
    return;
  }

  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call != nullptr && group_call->is_inited && !group_call->is_active) {
    return;
  }

  if (!td_->contacts_manager_->have_user_force(user_id) ||
      (!recursive && need_group_call_participants(input_group_call_id, group_call) &&
       get_group_call_participant(input_group_call_id, user_id) == nullptr)) {
    if (recursive) {
      LOG(ERROR) << "Failed to find speaking " << user_id << " from " << input_group_call_id;
    } else {
      auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, user_id,
                                                   date](Result<Unit> &&result) {
        if (!G()->close_flag() && result.is_ok()) {
          send_closure(actor_id, &GroupCallManager::on_user_speaking_in_group_call, group_call_id, user_id, date, true);
        }
      });
      td_->create_handler<GetGroupCallParticipantQuery>(std::move(query_promise))
          ->send(input_group_call_id, {user_id.get()}, {});
    }
    return;
  }

  LOG(INFO) << "Add " << user_id << " as recent speaker at " << date << " in " << input_group_call_id;
  auto &recent_speakers = group_call_recent_speakers_[group_call_id];
  if (recent_speakers == nullptr) {
    recent_speakers = make_unique<GroupCallRecentSpeakers>();
  }

  for (size_t i = 0; i < recent_speakers->users.size(); i++) {
    if (recent_speakers->users[i].first == user_id) {
      if (recent_speakers->users[i].second >= date) {
        LOG(INFO) << "Ignore outdated speaking information";
        return;
      }
      recent_speakers->users[i].second = date;
      while (i > 0 && recent_speakers->users[i - 1].second < date) {
        std::swap(recent_speakers->users[i - 1], recent_speakers->users[i]);
        i--;
      }
      on_group_call_recent_speakers_updated(group_call, recent_speakers.get());
      return;
    }
  }

  for (size_t i = 0; i <= recent_speakers->users.size(); i++) {
    if (i == recent_speakers->users.size() || recent_speakers->users[i].second <= date) {
      recent_speakers->users.insert(recent_speakers->users.begin() + i, {user_id, date});
      break;
    }
  }
  static constexpr size_t MAX_RECENT_SPEAKERS = 3;
  if (recent_speakers->users.size() > MAX_RECENT_SPEAKERS) {
    recent_speakers->users.pop_back();
  }

  on_group_call_recent_speakers_updated(group_call, recent_speakers.get());
}

void GroupCallManager::remove_recent_group_call_speaker(InputGroupCallId input_group_call_id, UserId user_id) {
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr) {
    return;
  }

  auto recent_speakers_it = group_call_recent_speakers_.find(group_call->group_call_id);
  if (recent_speakers_it == group_call_recent_speakers_.end()) {
    return;
  }
  auto &recent_speakers = recent_speakers_it->second;
  CHECK(recent_speakers != nullptr);
  for (size_t i = 0; i < recent_speakers->users.size(); i++) {
    if (recent_speakers->users[i].first == user_id) {
      LOG(INFO) << "Remove " << user_id << " from recent speakers in " << input_group_call_id << " from "
                << group_call->dialog_id;
      recent_speakers->users.erase(recent_speakers->users.begin() + i);
      on_group_call_recent_speakers_updated(group_call, recent_speakers.get());
      return;
    }
  }
}

void GroupCallManager::on_group_call_recent_speakers_updated(const GroupCall *group_call,
                                                             GroupCallRecentSpeakers *recent_speakers) {
  if (group_call == nullptr || !group_call->is_inited || recent_speakers->is_changed) {
    LOG(INFO) << "Don't need to send update of recent speakers in " << group_call->group_call_id << " from "
              << group_call->dialog_id;
    return;
  }

  recent_speakers->is_changed = true;

  LOG(INFO) << "Schedule update of recent speakers in " << group_call->group_call_id << " from "
            << group_call->dialog_id;
  const double MAX_RECENT_SPEAKER_UPDATE_DELAY = 0.5;
  recent_speaker_update_timeout_.set_timeout_in(group_call->group_call_id.get(), MAX_RECENT_SPEAKER_UPDATE_DELAY);
}

UserId GroupCallManager::set_group_call_participant_is_speaking_by_source(InputGroupCallId input_group_call_id,
                                                                          int32 audio_source, bool is_speaking,
                                                                          int32 date) {
  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return UserId();
  }

  for (auto &participant : participants_it->second->participants) {
    if (participant.audio_source == audio_source) {
      if (participant.is_speaking != is_speaking) {
        participant.is_speaking = is_speaking;
        if (is_speaking) {
          participant.local_active_date = max(participant.local_active_date, date);
        }
        participant.order = get_real_participant_order(participant, participants_it->second->min_order);
        if (participant.order != 0) {
          send_update_group_call_participant(input_group_call_id, participant);
        }
      }

      return participant.user_id;
    }
  }
  return UserId();
}

void GroupCallManager::update_group_call_dialog(const GroupCall *group_call, const char *source) {
  if (!group_call->dialog_id.is_valid()) {
    return;
  }

  td_->messages_manager_->on_update_dialog_group_call(group_call->dialog_id, group_call->is_active,
                                                      group_call->participant_count == 0, source);
}

vector<td_api::object_ptr<td_api::groupCallRecentSpeaker>> GroupCallManager::get_recent_speakers(
    const GroupCall *group_call, bool for_update) {
  CHECK(group_call != nullptr && group_call->is_inited);

  auto recent_speakers_it = group_call_recent_speakers_.find(group_call->group_call_id);
  if (recent_speakers_it == group_call_recent_speakers_.end()) {
    return Auto();
  }

  auto *recent_speakers = recent_speakers_it->second.get();
  CHECK(recent_speakers != nullptr);
  LOG(INFO) << "Found " << recent_speakers->users.size() << " recent speakers in " << group_call->group_call_id
            << " from " << group_call->dialog_id;
  auto now = G()->unix_time();
  while (!recent_speakers->users.empty() && recent_speakers->users.back().second < now - RECENT_SPEAKER_TIMEOUT) {
    recent_speakers->users.pop_back();
  }

  vector<std::pair<UserId, bool>> recent_speaker_users;
  for (auto &recent_speaker : recent_speakers->users) {
    recent_speaker_users.emplace_back(recent_speaker.first, recent_speaker.second > now - 8);
  }

  if (recent_speakers->is_changed) {
    recent_speakers->is_changed = false;
    recent_speaker_update_timeout_.cancel_timeout(group_call->group_call_id.get());
  }
  if (!recent_speaker_users.empty()) {
    auto next_timeout = recent_speakers->users.back().second + RECENT_SPEAKER_TIMEOUT - now + 1;
    if (recent_speaker_users[0].second) {  // if someone is speaking, recheck in 1 second
      next_timeout = 1;
    }
    recent_speaker_update_timeout_.add_timeout_in(group_call->group_call_id.get(), next_timeout);
  }

  auto get_result = [recent_speaker_users] {
    return transform(recent_speaker_users, [](const std::pair<UserId, bool> &recent_speaker_user) {
      return td_api::make_object<td_api::groupCallRecentSpeaker>(recent_speaker_user.first.get(),
                                                                 recent_speaker_user.second);
    });
  };
  if (recent_speakers->last_sent_users != recent_speaker_users) {
    recent_speakers->last_sent_users = std::move(recent_speaker_users);

    if (!for_update) {
      // the change must be received through update first
      send_closure(G()->td(), &Td::send_update, get_update_group_call_object(group_call, get_result()));
    }
  }

  return get_result();
}

tl_object_ptr<td_api::groupCall> GroupCallManager::get_group_call_object(
    const GroupCall *group_call, vector<td_api::object_ptr<td_api::groupCallRecentSpeaker>> recent_speakers) const {
  CHECK(group_call != nullptr);
  CHECK(group_call->is_inited);

  bool is_joined = group_call->is_joined && !group_call->is_being_left;
  bool can_self_unmute = is_joined && group_call->can_self_unmute;
  bool mute_new_participants = get_group_call_mute_new_participants(group_call);
  bool can_change_mute_new_participants =
      group_call->is_active && group_call->can_be_managed && group_call->allowed_change_mute_new_participants;
  return td_api::make_object<td_api::groupCall>(
      group_call->group_call_id.get(), group_call->is_active, is_joined, group_call->need_rejoin, can_self_unmute,
      group_call->can_be_managed, group_call->participant_count, group_call->loaded_all_participants,
      std::move(recent_speakers), mute_new_participants, can_change_mute_new_participants, group_call->duration);
}

tl_object_ptr<td_api::updateGroupCall> GroupCallManager::get_update_group_call_object(
    const GroupCall *group_call, vector<td_api::object_ptr<td_api::groupCallRecentSpeaker>> recent_speakers) const {
  return td_api::make_object<td_api::updateGroupCall>(get_group_call_object(group_call, std::move(recent_speakers)));
}

tl_object_ptr<td_api::updateGroupCallParticipant> GroupCallManager::get_update_group_call_participant_object(
    GroupCallId group_call_id, const GroupCallParticipant &participant) {
  return td_api::make_object<td_api::updateGroupCallParticipant>(
      group_call_id.get(), participant.get_group_call_participant_object(td_->contacts_manager_.get()));
}

void GroupCallManager::send_update_group_call(const GroupCall *group_call, const char *source) {
  LOG(INFO) << "Send update about " << group_call->group_call_id << " from " << source;
  send_closure(G()->td(), &Td::send_update,
               get_update_group_call_object(group_call, get_recent_speakers(group_call, true)));
}

void GroupCallManager::send_update_group_call_participant(GroupCallId group_call_id,
                                                          const GroupCallParticipant &participant) {
  send_closure(G()->td(), &Td::send_update, get_update_group_call_participant_object(group_call_id, participant));
}

void GroupCallManager::send_update_group_call_participant(InputGroupCallId input_group_call_id,
                                                          const GroupCallParticipant &participant) {
  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  send_update_group_call_participant(group_call->group_call_id, participant);
}

}  // namespace td
