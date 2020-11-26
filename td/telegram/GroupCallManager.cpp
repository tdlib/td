//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallManager.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/Random.h"

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

class LeaveGroupCallQuery : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit LeaveGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId group_call_id, int32 source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_leaveGroupCall(group_call_id.get_input_group_call(), source)));
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

  void send(InputGroupCallId group_call_id) {
    send_query(
        G()->net_query_creator().create(telegram_api::phone_discardGroupCall(group_call_id.get_input_group_call())));
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
  bool is_active = false;
  int32 member_count = 0;
  int32 version = -1;
  int32 duration = 0;
};

GroupCallManager::GroupCallManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void GroupCallManager::tear_down() {
  parent_.reset();
}

void GroupCallManager::create_group_call(ChannelId channel_id, Promise<InputGroupCallId> &&promise) {
  td_->create_handler<CreateGroupCallQuery>(std::move(promise))->send(channel_id);
}

void GroupCallManager::leave_group_call(InputGroupCallId group_call_id, int32 source, Promise<Unit> &&promise) {
  td_->create_handler<LeaveGroupCallQuery>(std::move(promise))->send(group_call_id, source);
}

void GroupCallManager::discard_group_call(InputGroupCallId group_call_id, Promise<Unit> &&promise) {
  td_->create_handler<DiscardGroupCallQuery>(std::move(promise))->send(group_call_id);
}

void GroupCallManager::on_update_group_call(tl_object_ptr<telegram_api::GroupCall> group_call_ptr) {
  auto call_id = update_group_call(group_call_ptr);
  if (call_id.is_valid()) {
    LOG(INFO) << "Update " << call_id;
  } else {
    LOG(ERROR) << "Receive invalid " << to_string(group_call_ptr);
  }
}

InputGroupCallId GroupCallManager::update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr) {
  CHECK(group_call_ptr != nullptr);

  InputGroupCallId call_id;
  GroupCall call;
  switch (group_call_ptr->get_id()) {
    case telegram_api::groupCall::ID: {
      auto group_call = static_cast<const telegram_api::groupCall *>(group_call_ptr.get());
      call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      call.is_active = true;
      call.member_count = group_call->participants_count_;
      call.version = group_call->version_;
      break;
    }
    case telegram_api::groupCallDiscarded::ID: {
      auto group_call = static_cast<const telegram_api::groupCallDiscarded *>(group_call_ptr.get());
      call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      call.duration = group_call->duration_;
      break;
    }
    default:
      UNREACHABLE();
  }
  if (!call_id.is_valid() || call.member_count < 0) {
    return {};
  }

  auto &group_call = group_calls_[call_id];
  bool need_update = false;
  if (group_call == nullptr) {
    group_call = make_unique<GroupCall>();
    *group_call = std::move(call);
    need_update = true;
  } else {
    if (!group_call->is_active) {
      // never update ended calls
    } else if (!call.is_active) {
      // always update to an ended call
      *group_call = std::move(call);
      need_update = true;
    } else {
      if (call.version > group_call->version) {
        need_update = call.member_count != group_call->member_count;
        *group_call = std::move(call);
      }
    }
  }

  if (need_update) {
    send_closure(G()->td(), &Td::send_update, get_update_group_call_object(call_id, group_call.get()));
  }

  return call_id;
}

tl_object_ptr<td_api::groupCall> GroupCallManager::get_group_call_object(InputGroupCallId group_call_id,
                                                                         const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return td_api::make_object<td_api::groupCall>(group_call_id.get_group_call_id(), group_call->is_active,
                                                group_call->member_count, group_call->duration);
}

tl_object_ptr<td_api::updateGroupCall> GroupCallManager::get_update_group_call_object(InputGroupCallId group_call_id,
                                                                                      const GroupCall *group_call) {
  return td_api::make_object<td_api::updateGroupCall>(get_group_call_object(group_call_id, group_call));
}

}  // namespace td
