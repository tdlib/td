//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/Random.h"

#include <utility>

namespace td {

class CreateGroupCallQuery : public Td::ResultHandler {
  Promise<InputGroupCallId> promise_;
  ChannelId channel_id_;

 public:
  explicit CreateGroupCallQuery(Promise<InputGroupCallId> &&promise) : promise_(std::move(promise)) {
  }

  void send(ChannelId channel_id) {
    channel_id_ = channel_id;

    auto input_channel = td->contacts_manager_->get_input_channel(channel_id);
    CHECK(input_channel != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::phone_createGroupCall(std::move(input_channel), Random::secure_int32())));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_createGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateGroupCallQuery: " << to_string(ptr);

    auto group_call_ids = td->updates_manager_->get_update_new_group_call_ids(ptr.get());
    if (group_call_ids.size() != 1) {
      LOG(ERROR) << "Receive wrong CreateGroupCallQuery response " << to_string(ptr);
      return on_error(id, Status::Error(500, "Receive wrong response"));
    }

    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(std::move(group_call_ids[0]));
  }

  void on_error(uint64 id, Status status) override {
    td->contacts_manager_->on_get_channel_error(channel_id_, status, "CreateGroupCallQuery");
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

  void send(InputGroupCallId input_group_call_id, vector<int32> user_ids, vector<int32> sources) {
    input_group_call_id_ = input_group_call_id;
    auto limit = narrow_cast<int32>(max(user_ids.size(), sources.size()));
    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupParticipants(
        input_group_call_id.get_input_group_call(), std::move(user_ids), std::move(sources), string(), limit)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    td->group_call_manager_->on_get_group_call_participants(input_group_call_id_, result_ptr.move_as_ok(), false);

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

    td->group_call_manager_->process_join_group_call_response(input_group_call_id_, generation_,
                                                              result_ptr.move_as_ok(), std::move(promise_));
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
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
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
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
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

  void send(InputGroupCallId input_group_call_id, UserId user_id, bool is_muted) {
    auto input_user = td->contacts_manager_->get_input_user(user_id);
    CHECK(input_user != nullptr);

    int32 flags = 0;
    if (is_muted) {
      flags |= telegram_api::phone_editGroupCallMember::MUTED_MASK;
    }

    send_query(G()->net_query_creator().create(telegram_api::phone_editGroupCallMember(
        flags, false /*ignored*/, input_group_call_id.get_input_group_call(), std::move(input_user))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_editGroupCallMember>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditGroupCallMemberQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
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

  void send(InputGroupCallId input_group_call_id, int32 source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_checkGroupCall(input_group_call_id.get_input_group_call(), source)));
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
      promise_.set_error(Status::Error(400, "GROUP_CALL_JOIN_MISSING"));
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

  void send(InputGroupCallId input_group_call_id, int32 source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_leaveGroupCall(input_group_call_id.get_input_group_call(), source)));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::phone_leaveGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for LeaveGroupCallQuery: " << to_string(ptr);
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
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
    td->updates_manager_->on_get_updates(std::move(ptr));

    // TODO set promise after updates are processed
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    promise_.set_error(std::move(status));
  }
};

struct GroupCallManager::GroupCall {
  GroupCallId group_call_id;
  ChannelId channel_id;
  bool is_inited = false;
  bool is_active = false;
  bool is_joined = false;
  bool is_speaking = false;
  bool mute_new_participants = false;
  bool allowed_change_mute_new_participants = false;
  int32 participant_count = 0;
  int32 version = -1;
  int32 duration = 0;
  int32 source = 0;
};

struct GroupCallManager::GroupCallParticipants {
  vector<GroupCallParticipant> participants;
  string next_offset;
  int64 min_order = std::numeric_limits<int64>::max();
};

struct GroupCallManager::GroupCallRecentSpeakers {
  vector<std::pair<UserId, int32>> users;  // user + time; sorted by time
  bool is_changed = false;
  vector<int32> last_sent_user_ids;
};

struct GroupCallManager::PendingJoinRequest {
  NetQueryRef query_ref;
  uint64 generation = 0;
  int32 source = 0;
  Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> promise;
};

GroupCallManager::GroupCallManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  pending_send_speaking_action_timeout_.set_callback(on_pending_send_speaking_action_timeout_callback);
  pending_send_speaking_action_timeout_.set_callback_data(static_cast<void *>(this));

  recent_speaker_update_timeout_.set_callback(on_recent_speaker_update_timeout_callback);
  recent_speaker_update_timeout_.set_callback_data(static_cast<void *>(this));
}

GroupCallManager::~GroupCallManager() = default;

void GroupCallManager::tear_down() {
  parent_.reset();
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
  CHECK(group_call != nullptr && group_call->is_inited && group_call->channel_id.is_valid());
  if (!group_call->is_joined || !group_call->is_speaking) {
    return;
  }

  on_user_speaking_in_group_call(group_call_id, td_->contacts_manager_->get_my_id(), G()->unix_time());

  pending_send_speaking_action_timeout_.add_timeout_in(group_call_id.get(), 4.0);

  td_->messages_manager_->send_dialog_action(DialogId(group_call->channel_id), MessageId(),
                                             DialogAction::get_speaking_action(), Promise<Unit>());
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

  get_recent_speaker_user_ids(get_group_call(input_group_call_id),
                              false);  // will update the list and send updateGroupCall if needed
}

GroupCallId GroupCallManager::get_group_call_id(InputGroupCallId input_group_call_id, ChannelId channel_id) {
  if (td_->auth_manager_->is_bot() || !input_group_call_id.is_valid()) {
    return GroupCallId();
  }
  return add_group_call(input_group_call_id, channel_id)->group_call_id;
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
                                                              ChannelId channel_id) {
  CHECK(!td_->auth_manager_->is_bot());
  auto &group_call = group_calls_[input_group_call_id];
  if (group_call == nullptr) {
    group_call = make_unique<GroupCall>();
    group_call->group_call_id = get_next_group_call_id(input_group_call_id);
    LOG(INFO) << "Add " << input_group_call_id << " from " << channel_id << " as " << group_call->group_call_id;
  }
  if (!group_call->channel_id.is_valid()) {
    group_call->channel_id = channel_id;
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

void GroupCallManager::create_voice_chat(ChannelId channel_id, Promise<InputGroupCallId> &&promise) {
  td_->create_handler<CreateGroupCallQuery>(std::move(promise))->send(channel_id);
}

void GroupCallManager::get_group_call(GroupCallId group_call_id,
                                      Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto group_call = get_group_call(input_group_call_id);
  if (group_call != nullptr && group_call->is_inited) {
    return promise.set_value(get_group_call_object(group_call, get_recent_speaker_user_ids(group_call, false)));
  }

  reload_group_call(input_group_call_id, std::move(promise));
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

    if (update_group_call(result.ok()->call_, ChannelId()) != input_group_call_id) {
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

  auto group_call = get_group_call(input_group_call_id);
  for (auto &promise : promises) {
    promise.set_value(get_group_call_object(group_call, get_recent_speaker_user_ids(group_call, false)));
  }
}

bool GroupCallManager::need_group_call_participants(InputGroupCallId input_group_call_id) const {
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    return false;
  }
  if (group_call->is_joined) {
    return true;
  }
  if (pending_join_requests_.count(input_group_call_id) != 0) {
    return true;
  }
  return false;
}

void GroupCallManager::on_get_group_call_participants(
    InputGroupCallId input_group_call_id, tl_object_ptr<telegram_api::phone_groupParticipants> &&participants,
    bool is_load) {
  LOG(INFO) << "Receive group call participants: " << to_string(participants);

  CHECK(participants != nullptr);
  td_->contacts_manager_->on_get_users(std::move(participants->users_), "on_get_group_call_participants");

  process_group_call_participants(input_group_call_id, std::move(participants->participants_), false);

  on_receive_group_call_version(input_group_call_id, participants->version_);

  if (is_load) {
    // TODO use count, next_offset
  }
}

void GroupCallManager::on_update_group_call_participants(
    InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::groupCallParticipant>> &&participants,
    int32 version) {
  if (!need_group_call_participants(input_group_call_id)) {
    LOG(INFO) << "Ignore updateGroupCallParticipants in unknown " << input_group_call_id;
    return;
  }

  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->version >= version) {
    if (participants.size() == 1 && group_call->version == version) {
      GroupCallParticipant participant(participants[0]);
      if (participant.user_id == td_->contacts_manager_->get_my_id()) {
        process_group_call_participant(input_group_call_id, std::move(participant));
        return;
      }
    }
    LOG(INFO) << "Ignore already applied updateGroupCallParticipants in " << input_group_call_id;
    return;
  }
  if (group_call->version + static_cast<int32>(participants.size()) == version) {
    process_group_call_participants(input_group_call_id, std::move(participants), true);
    return;
  }

  // TODO sync group call participant list
}

void GroupCallManager::process_group_call_participants(
    InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::groupCallParticipant>> &&participants,
    bool from_update) {
  if (!need_group_call_participants(input_group_call_id)) {
    return;
  }

  auto group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (from_update) {
    CHECK(group_call->version != -1);
    group_call->version += static_cast<int32>(participants.size());
  }
  auto old_participant_count = group_call->participant_count;
  for (auto &participant : participants) {
    int diff = process_group_call_participant(input_group_call_id, GroupCallParticipant(participant));
    if (from_update) {
      group_call->participant_count += diff;
    }
  }
  if (group_call->participant_count) {
    LOG(ERROR) << "Participant count became negative in " << input_group_call_id;
    group_call->participant_count = 0;
  }
  if (group_call->participant_count != old_participant_count) {
    send_update_group_call(group_call);
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

  auto &participants = group_call_participants_[input_group_call_id];
  if (participants == nullptr) {
    participants = make_unique<GroupCallParticipants>();
  }

  for (size_t i = 0; i < participants->participants.size(); i++) {
    auto &old_participant = participants->participants[i];
    if (old_participant.user_id == participant.user_id) {
      if (participant.joined_date == 0) {
        // removed participant
        if (old_participant.order != 0) {
          send_update_group_call_participant(input_group_call_id, participant);
        }
        participants->participants.erase(participants->participants.begin() + i);
        return -1;
      }

      if (participant.joined_date < old_participant.joined_date) {
        LOG(ERROR) << "Join date of " << participant.user_id << " in " << input_group_call_id << " decreased from "
                   << old_participant.joined_date << " to " << participant.joined_date;
        participant.joined_date = old_participant.joined_date;
      }
      if (participant.active_date < old_participant.active_date) {
        participant.active_date = old_participant.active_date;
      }
      participant.local_active_date = old_participant.local_active_date;
      participant.is_speaking = old_participant.is_speaking;
      auto real_order = participant.get_real_order();
      if (real_order >= participants->min_order) {
        participant.order = real_order;
      }
      participant.is_just_joined = false;

      if (old_participant != participant) {
        bool need_update = old_participant.order != 0 || participant.order != 0;
        old_participant = std::move(participant);
        if (need_update) {
          send_update_group_call_participant(input_group_call_id, old_participant);
        }
      }
      return 0;
    }
  }

  if (participant.joined_date == 0) {
    // unknown removed participant
    return -1;
  }

  // unknown added or edited participant
  int diff = participant.is_just_joined ? 1 : 0;
  auto real_order = participant.get_real_order();
  if (real_order >= participants->min_order) {
    participant.order = real_order;
  }
  participant.is_just_joined = false;
  participants->participants.push_back(std::move(participant));
  if (participants->participants.back().order != 0) {
    send_update_group_call_participant(input_group_call_id, participants->participants.back());
  }
  return diff;
}

void GroupCallManager::join_group_call(GroupCallId group_call_id,
                                       td_api::object_ptr<td_api::groupCallPayload> &&payload, int32 source,
                                       bool is_muted,
                                       Promise<td_api::object_ptr<td_api::groupCallJoinResponse>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (group_call->is_joined) {
    CHECK(group_call->is_inited);
    return promise.set_error(Status::Error(400, "Group call is already joined"));
  }
  if (group_call->is_inited && !group_call->is_active) {
    return promise.set_error(Status::Error(400, "Group call is finished"));
  }

  if (pending_join_requests_.count(input_group_call_id)) {
    auto it = pending_join_requests_.find(input_group_call_id);
    CHECK(it != pending_join_requests_.end());
    CHECK(it->second != nullptr);
    if (!it->second->query_ref.empty()) {
      cancel_query(it->second->query_ref);
    }
    it->second->promise.set_error(Status::Error(200, "Cancelled by another joinGroupCall request"));
    pending_join_requests_.erase(it);
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

  auto json_payload = json_encode<string>(json_object([&payload, source](auto &o) {
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
    o("ssrc", source);
  }));

  auto generation = ++join_group_request_generation_;
  auto &request = pending_join_requests_[input_group_call_id];
  request = make_unique<PendingJoinRequest>();
  request->generation = generation;
  request->source = source;
  request->promise = std::move(promise);

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation, input_group_call_id](Result<Unit> &&result) {
        CHECK(result.is_error());
        send_closure(actor_id, &GroupCallManager::finish_join_group_call, input_group_call_id, generation,
                     result.move_as_error());
      });
  request->query_ref = td_->create_handler<JoinGroupCallQuery>(std::move(query_promise))
                           ->send(input_group_call_id, json_payload, is_muted, generation);
}

void GroupCallManager::process_join_group_call_response(InputGroupCallId input_group_call_id, uint64 generation,
                                                        tl_object_ptr<telegram_api::Updates> &&updates,
                                                        Promise<Unit> &&promise) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end() || it->second->generation != generation) {
    LOG(INFO) << "Ignore JoinGroupCallQuery response with " << input_group_call_id << " and generation " << generation;
    return;
  }

  LOG(INFO) << "Receive result for JoinGroupCallQuery: " << to_string(updates);
  td_->updates_manager_->on_get_updates(std::move(updates));

  promise.set_error(Status::Error(500, "Wrong join response received"));
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
    group_call->source = it->second->source;
    it->second->promise.set_value(result.move_as_ok());
    need_update = true;
  }
  pending_join_requests_.erase(it);
  try_clear_group_call_participants(input_group_call_id);
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
}

void GroupCallManager::toggle_group_call_mute_new_participants(GroupCallId group_call_id, bool mute_new_participants,
                                                               Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  int32 flags = telegram_api::phone_toggleGroupCallSettings::JOIN_MUTED_MASK;
  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))
      ->send(flags, input_group_call_id, mute_new_participants);
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

void GroupCallManager::set_group_call_participant_is_speaking(GroupCallId group_call_id, int32 source, bool is_speaking,
                                                              Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined) {
    return promise.set_value(Unit());
  }
  if (group_call->source == source) {
    if (!group_call->channel_id.is_valid()) {
      return promise.set_value(Unit());
    }
    if (group_call->is_speaking != is_speaking) {
      group_call->is_speaking = is_speaking;
      if (is_speaking) {
        pending_send_speaking_action_timeout_.add_timeout_in(group_call_id.get(), 0.0);
      }
    }
    return promise.set_value(Unit());
  }

  if (is_speaking) {
    on_source_speaking_in_group_call(group_call_id, source, G()->unix_time(), false);
  }

  // TODO update participant list by others speaking actions

  promise.set_value(Unit());
}

void GroupCallManager::toggle_group_call_participant_is_muted(GroupCallId group_call_id, UserId user_id, bool is_muted,
                                                              Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  if (!td_->contacts_manager_->have_input_user(user_id)) {
    return promise.set_error(Status::Error(400, "Have no access to the user"));
  }
  td_->create_handler<EditGroupCallMemberQuery>(std::move(promise))->send(input_group_call_id, user_id, is_muted);
}

void GroupCallManager::check_group_call_is_joined(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    return promise.set_error(Status::Error(400, "GROUP_CALL_JOIN_MISSING"));
  }
  if (!group_call->is_active || !group_call->is_joined) {
    return promise.set_value(Unit());
  }
  auto source = group_call->source;

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, source,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error() && result.error().message() == "GROUP_CALL_JOIN_MISSING") {
      send_closure(actor_id, &GroupCallManager::on_group_call_left, input_group_call_id, source);
      result = Unit();
    }
    promise.set_result(std::move(result));
  });
  td_->create_handler<CheckGroupCallQuery>(std::move(query_promise))->send(input_group_call_id, source);
}

void GroupCallManager::leave_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined) {
    return promise.set_error(Status::Error(400, "GROUP_CALL_JOIN_MISSING"));
  }
  auto source = group_call->source;

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, source,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_ok()) {
      send_closure(actor_id, &GroupCallManager::on_group_call_left, input_group_call_id, source);
    }
    promise.set_result(std::move(result));
  });
  td_->create_handler<LeaveGroupCallQuery>(std::move(query_promise))->send(input_group_call_id, source);
}

void GroupCallManager::on_group_call_left(InputGroupCallId input_group_call_id, int32 source) {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->is_joined && group_call->source == source) {
    group_call->is_joined = false;
    group_call->is_speaking = false;
    group_call->source = 0;
    send_update_group_call(group_call);

    try_clear_group_call_participants(input_group_call_id);
  }
}

void GroupCallManager::discard_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  td_->create_handler<DiscardGroupCallQuery>(std::move(promise))->send(input_group_call_id);
}

void GroupCallManager::on_update_group_call(tl_object_ptr<telegram_api::GroupCall> group_call_ptr,
                                            ChannelId channel_id) {
  if (td_->auth_manager_->is_bot()) {
    LOG(ERROR) << "Receive " << to_string(group_call_ptr);
    return;
  }
  if (!channel_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(group_call_ptr) << " in invalid " << channel_id;
    channel_id = ChannelId();
  }
  auto input_group_call_id = update_group_call(group_call_ptr, channel_id);
  if (input_group_call_id.is_valid()) {
    LOG(INFO) << "Update " << input_group_call_id;
  } else {
    LOG(ERROR) << "Receive invalid " << to_string(group_call_ptr);
  }
}

void GroupCallManager::try_clear_group_call_participants(InputGroupCallId input_group_call_id) {
  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return;
  }
  if (need_group_call_participants(input_group_call_id)) {
    return;
  }

  auto participants = std::move(participants_it->second);
  CHECK(participants != nullptr);
  group_call_participants_.erase(participants_it);

  for (auto &participant : participants->participants) {
    if (participant.order != 0) {
      CHECK(participant.order >= participants->min_order);
      participant.order = 0;
      send_update_group_call_participant(input_group_call_id, participant);
    }
  }
}

InputGroupCallId GroupCallManager::update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr,
                                                     ChannelId channel_id) {
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
  auto *group_call = add_group_call(input_group_call_id, channel_id);
  call.group_call_id = group_call->group_call_id;
  call.channel_id = channel_id.is_valid() ? channel_id : group_call->channel_id;
  if (!group_call->is_inited) {
    *group_call = std::move(call);
    need_update = true;
  } else {
    if (!group_call->is_active) {
      // never update ended calls
    } else if (!call.is_active) {
      // always update to an ended call, droping also is_joined and is_speaking flags
      *group_call = std::move(call);
      need_update = true;
      if (group_call->channel_id.is_valid()) {
        td_->contacts_manager_->on_update_channel_group_call(group_call->channel_id, false, false);
      }
    } else {
      auto mute_flags_changed =
          call.mute_new_participants != group_call->mute_new_participants ||
          call.allowed_change_mute_new_participants != group_call->allowed_change_mute_new_participants;
      if (call.version > group_call->version) {
        if (group_call->version != -1) {
          on_receive_group_call_version(input_group_call_id, call.version);

          // if we know group call version, then update participants only by corresponding updates
          call.participant_count = group_call->participant_count;
          call.version = group_call->version;
        }
        if (group_call->channel_id.is_valid()) {
          td_->contacts_manager_->on_update_channel_group_call(group_call->channel_id, true,
                                                               call.participant_count == 0);
        }
        need_update = call.participant_count != group_call->participant_count || mute_flags_changed;
        *group_call = std::move(call);
      } else if (call.version == group_call->version) {
        if (mute_flags_changed) {
          group_call->mute_new_participants = call.mute_new_participants;
          group_call->allowed_change_mute_new_participants = call.allowed_change_mute_new_participants;
          need_update = true;
        }
      }
    }
  }
  if (!group_call->is_active && group_call_recent_speakers_.erase(group_call->group_call_id) != 0) {
    need_update = true;
  }
  if (!group_call->channel_id.is_valid()) {
    group_call->channel_id = channel_id;
  }
  if (!join_params.empty()) {
    need_update |= on_join_group_call_response(input_group_call_id, std::move(join_params));
  }
  if (need_update) {
    send_update_group_call(group_call);
  }
  try_clear_group_call_participants(input_group_call_id);
  return input_group_call_id;
}

void GroupCallManager::on_receive_group_call_version(InputGroupCallId input_group_call_id, int32 version) {
  // TODO
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

  if (!td_->contacts_manager_->have_user_force(user_id)) {
    if (recursive) {
      LOG(ERROR) << "Failed to find speaking " << user_id << " from " << input_group_call_id << " in "
                 << group_call->channel_id;
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

  LOG(INFO) << "Add " << user_id << " as recent speaker at " << date << " in " << input_group_call_id << " from "
            << group_call->channel_id;
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
      bool is_updated = false;
      while (i > 0 && recent_speakers->users[i - 1].second < date) {
        std::swap(recent_speakers->users[i - 1], recent_speakers->users[i]);
        i--;
        is_updated = true;
      }
      if (is_updated) {
        on_group_call_recent_speakers_updated(group_call, recent_speakers.get());
      } else {
        LOG(INFO) << "Position of " << user_id << " in recent speakers list didn't change";
      }
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

void GroupCallManager::on_source_speaking_in_group_call(GroupCallId group_call_id, int32 source, int32 date,
                                                        bool recursive) {
  if (G()->close_flag()) {
    return;
  }

  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();
  UserId user_id = get_group_call_participant_by_source(input_group_call_id, source);
  if (user_id.is_valid()) {
    on_user_speaking_in_group_call(group_call_id, user_id, G()->unix_time());
  } else if (!recursive) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, source,
                                                 date = G()->unix_time()](Result<Unit> &&result) {
      if (!G()->close_flag() && result.is_ok()) {
        send_closure(actor_id, &GroupCallManager::on_source_speaking_in_group_call, group_call_id, source, date, true);
      }
    });
    td_->create_handler<GetGroupCallParticipantQuery>(std::move(query_promise))
        ->send(input_group_call_id, {}, {source});
  }
}

void GroupCallManager::on_group_call_recent_speakers_updated(const GroupCall *group_call,
                                                             GroupCallRecentSpeakers *recent_speakers) {
  if (group_call == nullptr || !group_call->is_inited || recent_speakers->is_changed) {
    LOG(INFO) << "Don't need to send update of recent speakers in " << group_call->group_call_id;
    return;
  }

  recent_speakers->is_changed = true;

  LOG(INFO) << "Schedule update of recent speakers in " << group_call->group_call_id;
  const double MAX_RECENT_SPEAKER_UPDATE_DELAY = 0.5;
  recent_speaker_update_timeout_.set_timeout_in(group_call->group_call_id.get(), MAX_RECENT_SPEAKER_UPDATE_DELAY);
}

UserId GroupCallManager::get_group_call_participant_by_source(InputGroupCallId input_group_call_id, int32 source) {
  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return UserId();
  }

  for (auto &participant : participants_it->second->participants) {
    if (participant.source == source) {
      return participant.user_id;
    }
  }
  return UserId();
}

vector<int32> GroupCallManager::get_recent_speaker_user_ids(const GroupCall *group_call, bool for_update) {
  CHECK(group_call != nullptr && group_call->is_inited);

  vector<int32> recent_speaker_user_ids;
  auto recent_speakers_it = group_call_recent_speakers_.find(group_call->group_call_id);
  if (recent_speakers_it == group_call_recent_speakers_.end()) {
    return recent_speaker_user_ids;
  }

  auto *recent_speakers = recent_speakers_it->second.get();
  CHECK(recent_speakers != nullptr);
  LOG(INFO) << "Found " << recent_speakers->users.size() << " recent speakers in " << group_call->group_call_id;
  while (!recent_speakers->users.empty() &&
         recent_speakers->users.back().second < G()->unix_time() - RECENT_SPEAKER_TIMEOUT) {
    recent_speakers->users.pop_back();
  }

  for (auto &recent_speaker : recent_speakers->users) {
    recent_speaker_user_ids.push_back(recent_speaker.first.get());
  }

  if (recent_speakers->is_changed) {
    recent_speakers->is_changed = false;
    recent_speaker_update_timeout_.cancel_timeout(group_call->group_call_id.get());
  }
  if (!recent_speakers->users.empty()) {
    auto next_timeout = recent_speakers->users.back().second + RECENT_SPEAKER_TIMEOUT - G()->unix_time() + 1;
    recent_speaker_update_timeout_.add_timeout_in(group_call->group_call_id.get(), next_timeout);
  }

  if (recent_speakers->last_sent_user_ids != recent_speaker_user_ids) {
    recent_speakers->last_sent_user_ids = recent_speaker_user_ids;

    if (!for_update) {
      // the change must be received through update first
      send_update_group_call(group_call);
    }
  }
  return recent_speaker_user_ids;
}

tl_object_ptr<td_api::groupCall> GroupCallManager::get_group_call_object(const GroupCall *group_call,
                                                                         vector<int32> recent_speaker_user_ids) const {
  CHECK(group_call != nullptr);
  CHECK(group_call->is_inited);

  return td_api::make_object<td_api::groupCall>(group_call->group_call_id.get(), group_call->is_active,
                                                group_call->is_joined, group_call->participant_count,
                                                std::move(recent_speaker_user_ids), group_call->mute_new_participants,
                                                group_call->allowed_change_mute_new_participants, group_call->duration);
}

tl_object_ptr<td_api::updateGroupCall> GroupCallManager::get_update_group_call_object(
    const GroupCall *group_call, vector<int32> recent_speaker_user_ids) const {
  return td_api::make_object<td_api::updateGroupCall>(
      get_group_call_object(group_call, std::move(recent_speaker_user_ids)));
}

tl_object_ptr<td_api::updateGroupCallParticipant> GroupCallManager::get_update_group_call_participant_object(
    GroupCallId group_call_id, const GroupCallParticipant &participant) {
  return td_api::make_object<td_api::updateGroupCallParticipant>(
      group_call_id.get(), participant.get_group_call_participant_object(td_->contacts_manager_.get()));
}

void GroupCallManager::send_update_group_call(const GroupCall *group_call) {
  send_closure(G()->td(), &Td::send_update,
               get_update_group_call_object(group_call, get_recent_speaker_user_ids(group_call, true)));
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
