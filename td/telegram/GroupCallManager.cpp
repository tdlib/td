//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallManager.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/AuthManager.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogAction.h"
#include "td/telegram/DialogActionManager.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DialogParticipantFilter.h"
#include "td/telegram/DialogParticipantManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/GroupCallJoinParameters.h"
#include "td/telegram/GroupCallMessage.h"
#include "td/telegram/GroupCallMessageLimit.hpp"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageReactor.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/MessageTopic.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/StoryManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UpdatesManager.h"
#include "td/telegram/UserManager.h"

#include "td/actor/MultiPromise.h"
#include "td/actor/SleepActor.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/UInt.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <functional>
#include <map>
#include <utility>

namespace td {

namespace {

template <class T>
T tde2e_move_as_ok_impl(tde2e_api::Result<T> result, int line) {
  LOG_CHECK(result.is_ok()) << static_cast<int>(result.error().code) << " : " << result.error().message << " at line "
                            << line;
  return std::move(result.value());
}

#define tde2e_move_as_ok(result) tde2e_move_as_ok_impl((result), __LINE__)

}  // namespace

class GetGroupCallStreamChannelsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::groupCallStreams>> promise_;

 public:
  explicit GetGroupCallStreamChannelsQuery(Promise<td_api::object_ptr<td_api::groupCallStreams>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DcId stream_dc_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupCallStreamChannels(input_group_call_id.get_input_group_call()), {}, stream_dc_id,
        NetQuery::Type::DownloadSmall));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCallStreamChannels>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    auto streams = transform(ptr->channels_, [](const tl_object_ptr<telegram_api::groupCallStreamChannel> &channel) {
      return td_api::make_object<td_api::groupCallStream>(channel->channel_, channel->scale_,
                                                          channel->last_timestamp_ms_);
    });
    promise_.set_value(td_api::make_object<td_api::groupCallStreams>(std::move(streams)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallStreamQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit GetGroupCallStreamQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DcId stream_dc_id, int64 time_offset, int32 scale, int32 channel_id,
            int32 video_quality) {
    int32 flags = 0;
    if (channel_id != 0) {
      flags |= telegram_api::inputGroupCallStream::VIDEO_CHANNEL_MASK;
    }
    auto input_stream = telegram_api::make_object<telegram_api::inputGroupCallStream>(
        flags, input_group_call_id.get_input_group_call(), time_offset, scale, channel_id, video_quality);
    auto query = G()->net_query_creator().create(
        telegram_api::upload_getFile(0, false, false, std::move(input_stream), 0, 1 << 20), {}, stream_dc_id,
        NetQuery::Type::DownloadSmall);
    query->total_timeout_limit_ = 0;
    send_query(std::move(query));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::upload_getFile>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    if (ptr->get_id() != telegram_api::upload_file::ID) {
      return on_error(Status::Error(500, "Receive unexpected server response"));
    }

    auto file = move_tl_object_as<telegram_api::upload_file>(ptr);
    promise_.set_value(file->bytes_.as_slice().str());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallJoinAsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::messageSenders>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetGroupCallJoinAsQuery(Promise<td_api::object_ptr<td_api::messageSenders>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupCallJoinAs(std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCallJoinAs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallJoinAsQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetGroupCallJoinAsQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetGroupCallJoinAsQuery");

    promise_.set_value(convert_message_senders_object(td_, ptr->peers_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetGroupCallJoinAsQuery");
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallSendAsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  DialogId dialog_id_;

 public:
  explicit GetGroupCallSendAsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DialogId dialog_id) {
    input_group_call_id_ = input_group_call_id;
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Have no access to the chat"));
    }

    send_query(
        G()->net_query_creator().create(telegram_api::channels_getSendAs(0, false, true, std::move(input_peer))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::channels_getSendAs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallSendAsQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetGroupCallSendAsQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetGroupCallSendAsQuery");

    bool can_choose_message_sender = false;
    for (auto &peer : ptr->peers_) {
      DialogId dialog_id(peer->peer_);
      if (dialog_id != td_->dialog_manager_->get_my_dialog_id() && dialog_id != dialog_id_) {
        can_choose_message_sender = true;
      }
    }
    td_->group_call_manager_->on_update_group_call_can_choose_message_sender(input_group_call_id_,
                                                                             can_choose_message_sender);
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetGroupCallSendAsQuery");
    promise_.set_error(std::move(status));
  }
};

class SaveDefaultGroupCallJoinAsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SaveDefaultGroupCallJoinAsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, DialogId as_dialog_id) {
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    auto as_input_peer = td_->dialog_manager_->get_input_peer(as_dialog_id, AccessRights::Read);
    CHECK(as_input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::phone_saveDefaultGroupCallJoinAs(std::move(input_peer), std::move(as_input_peer)),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_saveDefaultGroupCallJoinAs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto success = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SaveDefaultGroupCallJoinAsQuery: " << success;

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    // td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "SaveDefaultGroupCallJoinAsQuery");
    promise_.set_error(std::move(status));
  }
};

class SaveDefaultGroupCallSendAsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  DialogId as_dialog_id_;

 public:
  explicit SaveDefaultGroupCallSendAsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DialogId as_dialog_id) {
    as_dialog_id_ = as_dialog_id;
    auto as_input_peer = td_->dialog_manager_->get_input_peer(as_dialog_id, AccessRights::Read);
    CHECK(as_input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::phone_saveDefaultSendAs(input_group_call_id.get_input_group_call(), std::move(as_input_peer)),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_saveDefaultSendAs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto success = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SaveDefaultGroupCallSendAsQuery: " << success;
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(as_dialog_id_, status, "SaveDefaultGroupCallSendAsQuery");
    promise_.set_error(std::move(status));
  }
};

class CreateGroupCallQuery final : public Td::ResultHandler {
  Promise<InputGroupCallId> promise_;
  DialogId dialog_id_;

 public:
  explicit CreateGroupCallQuery(Promise<InputGroupCallId> &&promise) : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, const string &title, int32 start_date, bool is_rtmp_stream) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    int32 flags = 0;
    if (!title.empty()) {
      flags |= telegram_api::phone_createGroupCall::TITLE_MASK;
    }
    if (start_date > 0) {
      flags |= telegram_api::phone_createGroupCall::SCHEDULE_DATE_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::phone_createGroupCall(flags, is_rtmp_stream, std::move(input_peer), Random::secure_int32(), title,
                                            start_date),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_createGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateGroupCallQuery: " << to_string(ptr);

    auto input_group_call_id = td_->updates_manager_->get_update_new_group_call_id(ptr.get());
    if (!input_group_call_id.is_valid()) {
      return on_error(Status::Error(500, "Receive wrong response"));
    }
    td_->updates_manager_->on_get_updates(
        std::move(ptr), PromiseCreator::lambda([promise = std::move(promise_), input_group_call_id](Unit) mutable {
          promise.set_value(std::move(input_group_call_id));
        }));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "CreateGroupCallQuery");
    promise_.set_error(std::move(status));
  }
};

class CreateConferenceCallQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::Updates>> promise_;

 public:
  explicit CreateConferenceCallQuery(Promise<telegram_api::object_ptr<telegram_api::Updates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(int32 random_id, bool is_join, const GroupCallJoinParameters &join_parameters,
            tde2e_api::PrivateKeyId private_key_id, tde2e_api::PublicKeyId public_key_id) {
    UInt256 public_key{};
    BufferSlice block;
    if (is_join) {
      auto public_key_string = tde2e_move_as_ok(tde2e_api::key_to_public_key(private_key_id));
      CHECK(public_key_string.size() == public_key.as_slice().size());
      public_key.as_mutable_slice().copy_from(public_key_string);

      tde2e_api::CallParticipant participant;
      participant.user_id = td_->user_manager_->get_my_id().get();
      participant.public_key_id = public_key_id;
      participant.permissions = 3;

      tde2e_api::CallState state;
      state.participants.push_back(std::move(participant));

      block = BufferSlice(tde2e_move_as_ok(tde2e_api::call_create_zero_block(private_key_id, state)));
    }
    send_query(G()->net_query_creator().create(telegram_api::phone_createConferenceCall(
        0, join_parameters.is_muted_, !join_parameters.is_my_video_enabled_, is_join, random_id, public_key,
        std::move(block), telegram_api::make_object<telegram_api::dataJSON>(join_parameters.payload_))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_createConferenceCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CreateConferenceCallQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallStreamRtmpUrlQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::rtmpUrl>> promise_;
  DialogId dialog_id_;

 public:
  explicit GetGroupCallStreamRtmpUrlQuery(Promise<td_api::object_ptr<td_api::rtmpUrl>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(DialogId dialog_id, bool is_story, bool revoke) {
    dialog_id_ = dialog_id;

    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Read);
    CHECK(input_peer != nullptr);

    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupCallStreamRtmpUrl(0, is_story, std::move(input_peer), revoke)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCallStreamRtmpUrl>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    promise_.set_value(td_api::make_object<td_api::rtmpUrl>(ptr->url_, ptr->key_));
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(dialog_id_, status, "GetGroupCallStreamRtmpUrlQuery");
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallQuery final : public Td::ResultHandler {
  Promise<tl_object_ptr<telegram_api::phone_groupCall>> promise_;

 public:
  explicit GetGroupCallQuery(Promise<tl_object_ptr<telegram_api::phone_groupCall>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, int32 limit) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupCall(input_group_call_id.get_input_group_call(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallQuery: " << to_string(ptr);

    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallStreamerQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::groupCallParticipant>> promise_;
  InputGroupCallId input_group_call_id_;
  DialogId dialog_id_;

 public:
  explicit GetGroupCallStreamerQuery(Promise<td_api::object_ptr<td_api::groupCallParticipant>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DialogId dialog_id) {
    input_group_call_id_ = input_group_call_id;
    dialog_id_ = dialog_id;
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupCall(input_group_call_id.get_input_group_call(), 10)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallStreamerQuery: " << to_string(ptr);

    td_->user_manager_->on_get_users(std::move(ptr->users_), "GetGroupCallStreamerQuery");
    td_->chat_manager_->on_get_chats(std::move(ptr->chats_), "GetGroupCallStreamerQuery");

    if (td_->group_call_manager_->on_update_group_call(std::move(ptr->call_), dialog_id_, true) !=
        input_group_call_id_) {
      LOG(ERROR) << "Expected " << input_group_call_id_ << ", but received " << to_string(ptr);
      return on_error(Status::Error(500, "Receive another group call"));
    }

    for (auto &group_call_participant : ptr->participants_) {
      GroupCallParticipant participant(group_call_participant, 0);
      if (participant.is_valid() && (participant.dialog_id == dialog_id_ || !participant.video_payload.is_empty())) {
        return promise_.set_value(participant.get_group_call_participant_object(td_));
      }
    }

    promise_.set_value(nullptr);
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallParticipantQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;

 public:
  explicit GetGroupCallParticipantQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::InputPeer>> &&input_peers,
            vector<int32> &&source_ids) {
    input_group_call_id_ = input_group_call_id;
    auto limit = narrow_cast<int32>(max(input_peers.size(), source_ids.size()));
    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupParticipants(
        input_group_call_id.get_input_group_call(), std::move(input_peers), std::move(source_ids), string(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->group_call_manager_->on_get_group_call_participants(input_group_call_id_, result_ptr.move_as_ok(), false,
                                                             string());

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallChainBlocksQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit GetGroupCallChainBlocksQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, int32 sub_chain_id, int32 offset, int32 limit) {
    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupCallChainBlocks(
        input_group_call_id.get_input_group_call(), sub_chain_id, offset, limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCallChainBlocks>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallChainBlocksQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallLastBlockQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::Updates>> promise_;

 public:
  explicit GetGroupCallLastBlockQuery(Promise<telegram_api::object_ptr<telegram_api::Updates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const InputGroupCall &input_group_call) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupCallChainBlocks(input_group_call.get_input_group_call(), 0, -1, 1)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCallChainBlocks>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallLastBlockQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class SendConferenceCallBroadcastQuery final : public Td::ResultHandler {
 public:
  void send(InputGroupCallId input_group_call_id, const string &query) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_sendConferenceCallBroadcast(input_group_call_id.get_input_group_call(), BufferSlice(query)),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_sendConferenceCallBroadcast>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendConferenceCallBroadcastQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), Promise<Unit>());
  }

  void on_error(Status status) final {
  }
};

class GetGroupCallParticipantsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  string offset_;

 public:
  explicit GetGroupCallParticipantsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, string offset, int32 limit) {
    input_group_call_id_ = input_group_call_id;
    offset_ = std::move(offset);
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupParticipants(input_group_call_id.get_input_group_call(),
                                                 vector<telegram_api::object_ptr<telegram_api::InputPeer>>(),
                                                 vector<int32>(), offset_, limit),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->group_call_manager_->on_get_group_call_participants(input_group_call_id_, result_ptr.move_as_ok(), true,
                                                             offset_);

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetInputGroupCallParticipantsQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::groupCallParticipants>> promise_;

 public:
  explicit GetInputGroupCallParticipantsQuery(Promise<td_api::object_ptr<td_api::groupCallParticipants>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(const InputGroupCall &input_group_call, int32 limit) {
    send_query(G()->net_query_creator().create(telegram_api::phone_getGroupParticipants(
        input_group_call.get_input_group_call(), vector<telegram_api::object_ptr<telegram_api::InputPeer>>(),
        vector<int32>(), string(), limit)));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto participants = result_ptr.move_as_ok();

    td_->user_manager_->on_get_users(std::move(participants->users_), "GetInputGroupCallParticipantsQuery");
    td_->chat_manager_->on_get_chats(std::move(participants->chats_), "GetInputGroupCallParticipantsQuery");

    auto total_count = participants->count_;
    auto version = participants->version_;
    vector<td_api::object_ptr<td_api::MessageSender>> result;
    for (auto &group_call_participant : participants->participants_) {
      GroupCallParticipant participant(group_call_participant, version);
      if (!participant.is_valid()) {
        LOG(ERROR) << "Receive invalid " << to_string(group_call_participant);
        continue;
      }
      result.push_back(get_message_sender_object(td_, participant.dialog_id, "GetInputGroupCallParticipantsQuery"));
    }
    if (total_count < static_cast<int32>(result.size())) {
      LOG(ERROR) << "Receive total " << total_count << " participant count and " << result.size() << " participants";
      total_count = static_cast<int32>(result.size());
    }
    promise_.set_value(td_api::make_object<td_api::groupCallParticipants>(total_count, std::move(result)));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallParticipantsToCheckQuery final : public Td::ResultHandler {
  Promise<vector<int64>> promise_;

 public:
  explicit GetGroupCallParticipantsToCheckQuery(Promise<vector<int64>> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupParticipants(input_group_call_id.get_input_group_call(),
                                                 vector<telegram_api::object_ptr<telegram_api::InputPeer>>(),
                                                 vector<int32>(), string(), 1000),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto participants = result_ptr.move_as_ok();
    auto version = participants->version_;
    vector<int64> result;
    for (auto &group_call_participant : participants->participants_) {
      GroupCallParticipant participant(group_call_participant, version);
      if (!participant.is_valid()) {
        LOG(ERROR) << "Receive invalid " << to_string(group_call_participant);
        continue;
      }
      result.push_back(participant.dialog_id.get());
    }
    promise_.set_value(std::move(result));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class StartScheduledGroupCallQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit StartScheduledGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_startScheduledGroupCall(input_group_call_id.get_input_group_call()),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_startScheduledGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for StartScheduledGroupCallQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "GROUPCALL_NOT_MODIFIED") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class JoinGroupCallQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::Updates>> promise_;

 public:
  explicit JoinGroupCallQuery(Promise<telegram_api::object_ptr<telegram_api::Updates>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCall input_group_call, const GroupCallJoinParameters &parameters, const string &public_key_string,
            BufferSlice &&block) {
    UInt256 public_key;
    CHECK(public_key_string.size() == public_key.as_slice().size());
    public_key.as_mutable_slice().copy_from(public_key_string);

    int32 flags = telegram_api::phone_joinGroupCall::PUBLIC_KEY_MASK | telegram_api::phone_joinGroupCall::BLOCK_MASK;
    send_query(G()->net_query_creator().create(telegram_api::phone_joinGroupCall(
        flags, parameters.is_muted_, !parameters.is_my_video_enabled_, input_group_call.get_input_group_call(),
        telegram_api::make_object<telegram_api::inputPeerSelf>(), string(), public_key, std::move(block),
        telegram_api::make_object<telegram_api::dataJSON>(parameters.payload_))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_joinGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for JoinGroupCallQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class JoinVideoChatQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  DialogId as_dialog_id_;
  uint64 generation_ = 0;

 public:
  explicit JoinVideoChatQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  NetQueryRef send(InputGroupCallId input_group_call_id, DialogId as_dialog_id,
                   const GroupCallJoinParameters &parameters, const string &invite_hash, uint64 generation) {
    input_group_call_id_ = input_group_call_id;
    as_dialog_id_ = as_dialog_id;
    generation_ = generation;

    telegram_api::object_ptr<telegram_api::InputPeer> join_as_input_peer;
    if (as_dialog_id.is_valid()) {
      join_as_input_peer = td_->dialog_manager_->get_input_peer(as_dialog_id, AccessRights::Read);
    } else {
      join_as_input_peer = telegram_api::make_object<telegram_api::inputPeerSelf>();
    }
    CHECK(join_as_input_peer != nullptr);

    int32 flags = 0;
    if (!invite_hash.empty()) {
      flags |= telegram_api::phone_joinGroupCall::INVITE_HASH_MASK;
    }
    auto query = G()->net_query_creator().create(telegram_api::phone_joinGroupCall(
        flags, parameters.is_muted_, !parameters.is_my_video_enabled_, input_group_call_id.get_input_group_call(),
        std::move(join_as_input_peer), invite_hash, UInt256(), BufferSlice(),
        telegram_api::make_object<telegram_api::dataJSON>(parameters.payload_)));
    auto join_query_ref = query.get_weak();
    send_query(std::move(query));
    return join_query_ref;
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_joinGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for JoinVideoChatQuery with generation " << generation_ << ": " << to_string(ptr);
    td_->group_call_manager_->process_join_video_chat_response(input_group_call_id_, generation_, std::move(ptr),
                                                               std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class JoinGroupCallPresentationQuery final : public Td::ResultHandler {
  InputGroupCallId input_group_call_id_;
  uint64 generation_ = 0;

 public:
  NetQueryRef send(InputGroupCallId input_group_call_id, const string &payload, uint64 generation) {
    input_group_call_id_ = input_group_call_id;
    generation_ = generation;

    auto query = G()->net_query_creator().create(telegram_api::phone_joinGroupCallPresentation(
        input_group_call_id.get_input_group_call(), make_tl_object<telegram_api::dataJSON>(payload)));
    auto join_query_ref = query.get_weak();
    send_query(std::move(query));
    return join_query_ref;
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_joinGroupCallPresentation>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for JoinGroupCallPresentationQuery with generation " << generation_ << ": "
              << to_string(ptr);
    td_->group_call_manager_->process_join_group_call_presentation_response(input_group_call_id_, generation_,
                                                                            std::move(ptr), Status::OK());
  }

  void on_error(Status status) final {
    td_->group_call_manager_->process_join_group_call_presentation_response(input_group_call_id_, generation_, nullptr,
                                                                            std::move(status));
  }
};

class LeaveGroupCallPresentationQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit LeaveGroupCallPresentationQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_leaveGroupCallPresentation(input_group_call_id.get_input_group_call()),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_editGroupCallTitle>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for LeaveGroupCallPresentationQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "PARTICIPANT_PRESENTATION_MISSING") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class EditGroupCallTitleQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditGroupCallTitleQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, const string &title) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_editGroupCallTitle(input_group_call_id.get_input_group_call(), title),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_editGroupCallTitle>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditGroupCallTitleQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "GROUPCALL_NOT_MODIFIED") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleGroupCallStartSubscriptionQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleGroupCallStartSubscriptionQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, bool start_subscribed) {
    send_query(G()->net_query_creator().create(telegram_api::phone_toggleGroupCallStartSubscription(
                                                   input_group_call_id.get_input_group_call(), start_subscribed),
                                               {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_toggleGroupCallStartSubscription>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ToggleGroupCallStartSubscriptionQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "GROUPCALL_NOT_MODIFIED") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class ToggleGroupCallSettingsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleGroupCallSettingsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, bool reset_invite_hash, bool set_join_muted, bool join_muted,
            bool set_messages_enabled, bool messages_enabled, bool set_paid_message_star_count,
            int64 paid_message_star_count) {
    int32 flags = 0;
    if (set_join_muted) {
      flags |= telegram_api::phone_toggleGroupCallSettings::JOIN_MUTED_MASK;
    }
    if (set_messages_enabled) {
      flags |= telegram_api::phone_toggleGroupCallSettings::MESSAGES_ENABLED_MASK;
    }
    if (set_paid_message_star_count) {
      flags |= telegram_api::phone_toggleGroupCallSettings::SEND_PAID_MESSAGES_STARS_MASK;
    }
    send_query(G()->net_query_creator().create(telegram_api::phone_toggleGroupCallSettings(
                                                   flags, reset_invite_hash, input_group_call_id.get_input_group_call(),
                                                   join_muted, messages_enabled, paid_message_star_count),
                                               {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_toggleGroupCallSettings>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ToggleGroupCallSettingsQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "GROUPCALL_NOT_MODIFIED") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class SendGroupCallMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  int32 message_id_;
  DialogId as_dialog_id_;
  int64 paid_message_star_count_;
  bool is_live_story_;

 public:
  explicit SendGroupCallMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, int32 message_id, const FormattedText &text, DialogId as_dialog_id,
            int64 paid_message_star_count, bool is_live_story) {
    input_group_call_id_ = input_group_call_id;
    message_id_ = message_id;
    as_dialog_id_ = as_dialog_id;
    paid_message_star_count_ = paid_message_star_count;
    is_live_story_ = is_live_story;
    int32 flags = 0;
    telegram_api::object_ptr<telegram_api::InputPeer> send_as_input_peer;
    if (as_dialog_id != DialogId()) {
      send_as_input_peer = td_->dialog_manager_->get_input_peer(as_dialog_id, AccessRights::Read);
      if (send_as_input_peer == nullptr) {
        return on_error(Status::Error(400, "Can't access sender chat"));
      }
      flags |= telegram_api::phone_sendGroupCallMessage::SEND_AS_MASK;
    }
    if (paid_message_star_count > 0) {
      if (!text.text.empty()) {
        td_->star_manager_->add_pending_owned_star_count(-paid_message_star_count, false);
      }
      flags |= telegram_api::phone_sendGroupCallMessage::ALLOW_PAID_STARS_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::phone_sendGroupCallMessage(
            flags, input_group_call_id.get_input_group_call(), Random::secure_int64(),
            get_input_text_with_entities(td_->user_manager_.get(), text, "SendGroupCallMessageQuery"),
            paid_message_star_count, std::move(send_as_input_peer)),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_sendGroupCallMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    td_->star_manager_->add_pending_owned_star_count(paid_message_star_count_, true);

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for SendGroupCallMessageQuery: " << to_string(ptr);
    td_->updates_manager_->process_updates_users_and_chats(ptr.get());
    auto group_call_messages = UpdatesManager::extract_group_call_messages(ptr.get());
    if (group_call_messages.size() != 1u || InputGroupCallId(group_call_messages[0]->call_) != input_group_call_id_) {
      LOG(ERROR) << "Receive invalid response " << to_string(ptr) << " with " << group_call_messages.size()
                 << " messages";
      return on_error(Status::Error(500, "Receive invalid response"));
    }
    td_->group_call_manager_->on_group_call_message_sent(input_group_call_id_, message_id_,
                                                         std::move(group_call_messages[0]->message_));
    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    td_->dialog_manager_->on_get_dialog_error(as_dialog_id_, status, "SendGroupCallMessageQuery");
    td_->star_manager_->add_pending_owned_star_count(paid_message_star_count_, false);
    td_->group_call_manager_->on_group_call_message_sending_failed(input_group_call_id_, message_id_,
                                                                   paid_message_star_count_, status);
    promise_.set_error(std::move(status));
  }
};

class SendGroupCallEncryptedMessageQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit SendGroupCallEncryptedMessageQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, const string &data) {
    send_query(G()->net_query_creator().create(telegram_api::phone_sendGroupCallEncryptedMessage(
                                                   input_group_call_id.get_input_group_call(), BufferSlice(data)),
                                               {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_sendGroupCallEncryptedMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(Unit());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteGroupCallMessagesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteGroupCallMessagesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<int32> &&server_ids, bool report_spam) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_deleteGroupCallMessages(0, report_spam, input_group_call_id.get_input_group_call(),
                                                    std::move(server_ids)),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_deleteGroupCallMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DeleteGroupCallMessagesQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteGroupCallParticipantMessagesQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeleteGroupCallParticipantMessagesQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DialogId sender_dialog_id, bool report_spam) {
    auto input_peer = td_->dialog_manager_->get_input_peer(sender_dialog_id, AccessRights::Know);
    CHECK(input_peer != nullptr);
    send_query(G()->net_query_creator().create(
        telegram_api::phone_deleteGroupCallParticipantMessages(
            0, report_spam, input_group_call_id.get_input_group_call(), std::move(input_peer)),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_deleteGroupCallParticipantMessages>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DeleteGroupCallParticipantMessagesQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GetGroupCallStarsQuery final : public Td::ResultHandler {
  Promise<telegram_api::object_ptr<telegram_api::phone_groupCallStars>> promise_;

 public:
  explicit GetGroupCallStarsQuery(Promise<telegram_api::object_ptr<telegram_api::phone_groupCallStars>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_getGroupCallStars(input_group_call_id.get_input_group_call()), {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_getGroupCallStars>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetGroupCallStarsQuery: " << to_string(ptr);
    promise_.set_value(std::move(ptr));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class InviteConferenceCallParticipantQuery final : public Td::ResultHandler {
  Promise<td_api::object_ptr<td_api::InviteGroupCallParticipantResult>> promise_;

 public:
  explicit InviteConferenceCallParticipantQuery(
      Promise<td_api::object_ptr<td_api::InviteGroupCallParticipantResult>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, telegram_api::object_ptr<telegram_api::InputUser> input_user,
            bool is_video) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_inviteConferenceCallParticipant(0, is_video, input_group_call_id.get_input_group_call(),
                                                            std::move(input_user)),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_inviteConferenceCallParticipant>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for InviteConferenceCallParticipantQuery: " << to_string(ptr);
    auto new_messages = UpdatesManager::get_new_messages(ptr.get());
    if (new_messages.size() != 1u || new_messages[0].second) {
      return on_error(Status::Error(500, "Receive invalid response"));
    }
    auto message_full_id = MessageFullId::get_message_full_id(new_messages[0].first, new_messages[0].second);
    if (!message_full_id.get_message_id().is_valid() || !message_full_id.get_dialog_id().is_valid()) {
      return on_error(Status::Error(500, "Receive invalid message identifier"));
    }

    td_->messages_manager_->wait_message_add(
        message_full_id,
        PromiseCreator::lambda([message_full_id, promise = std::move(promise_)](Result<Unit> &&result) mutable {
          TRY_STATUS_PROMISE(promise, G()->close_status());
          promise.set_value(td_api::make_object<td_api::inviteGroupCallParticipantResultSuccess>(
              message_full_id.get_dialog_id().get(), message_full_id.get_message_id().get()));
        }));

    td_->updates_manager_->on_get_updates(std::move(ptr), Promise<Unit>());
  }

  void on_error(Status status) final {
    if (status.message() == "USER_PRIVACY_RESTRICTED") {
      return promise_.set_value(td_api::make_object<td_api::inviteGroupCallParticipantResultUserPrivacyRestricted>());
    }
    if (status.message() == "USER_ALREADY_PARTICIPANT") {
      return promise_.set_value(td_api::make_object<td_api::inviteGroupCallParticipantResultUserAlreadyParticipant>());
    }
    if (status.message() == "USER_WAS_KICKED") {
      return promise_.set_value(td_api::make_object<td_api::inviteGroupCallParticipantResultUserWasBanned>());
    }
    promise_.set_error(std::move(status));
  }
};

class DeclineConferenceCallInviteQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DeclineConferenceCallInviteQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(ServerMessageId server_message_id) {
    send_query(
        G()->net_query_creator().create(telegram_api::phone_declineConferenceCallInvite(server_message_id.get())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_declineConferenceCallInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DeclineConferenceCallInviteQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DeleteConferenceCallParticipantsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  InputGroupCallId input_group_call_id_;
  vector<int64> user_ids_;
  bool is_ban_ = false;

 public:
  explicit DeleteConferenceCallParticipantsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<int64> &&user_ids, bool is_ban, BufferSlice &&block) {
    input_group_call_id_ = input_group_call_id;
    user_ids_ = user_ids;
    is_ban_ = is_ban;
    send_query(G()->net_query_creator().create(telegram_api::phone_deleteConferenceCallParticipants(
        0, !is_ban, is_ban, input_group_call_id.get_input_group_call(), std::move(user_ids), std::move(block))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_deleteConferenceCallParticipants>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DeleteConferenceCallParticipantsQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (begins_with(status.message(), "CONF_WRITE_CHAIN_INVALID")) {
      return td_->group_call_manager_->do_delete_group_call_participants(input_group_call_id_, std::move(user_ids_),
                                                                         is_ban_, std::move(promise_));
    }
    promise_.set_error(std::move(status));
  }
};

class InviteToGroupCallQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit InviteToGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::InputUser>> input_users) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_inviteToGroupCall(input_group_call_id.get_input_group_call(), std::move(input_users))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_inviteToGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for InviteToGroupCallQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ExportGroupCallInviteQuery final : public Td::ResultHandler {
  Promise<string> promise_;

 public:
  explicit ExportGroupCallInviteQuery(Promise<string> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, bool can_self_unmute) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_exportGroupCallInvite(0, can_self_unmute, input_group_call_id.get_input_group_call())));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_exportGroupCallInvite>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    promise_.set_value(std::move(ptr->link_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class ToggleGroupCallRecordQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit ToggleGroupCallRecordQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, bool is_enabled, const string &title, bool record_video,
            bool use_portrait_orientation) {
    int32 flags = 0;
    if (!title.empty()) {
      flags |= telegram_api::phone_toggleGroupCallRecord::TITLE_MASK;
    }
    send_query(G()->net_query_creator().create(
        telegram_api::phone_toggleGroupCallRecord(flags, is_enabled, record_video,
                                                  input_group_call_id.get_input_group_call(), title,
                                                  use_portrait_orientation),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_toggleGroupCallRecord>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for ToggleGroupCallRecordQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    if (status.message() == "GROUPCALL_NOT_MODIFIED") {
      promise_.set_value(Unit());
      return;
    }
    promise_.set_error(std::move(status));
  }
};

class EditGroupCallParticipantQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit EditGroupCallParticipantQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, DialogId dialog_id, bool set_is_mited, bool is_muted,
            int32 volume_level, bool set_raise_hand, bool raise_hand, bool set_video_is_stopped, bool video_is_stopped,
            bool set_video_is_paused, bool video_is_paused, bool set_presentation_is_paused,
            bool presentation_is_paused) {
    auto input_peer = td_->dialog_manager_->get_input_peer(dialog_id, AccessRights::Know);
    if (input_peer == nullptr) {
      return on_error(Status::Error(400, "Can't access the chat"));
    }

    int32 flags = 0;
    if (set_raise_hand) {
      flags |= telegram_api::phone_editGroupCallParticipant::RAISE_HAND_MASK;
    } else if (volume_level) {
      flags |= telegram_api::phone_editGroupCallParticipant::VOLUME_MASK;
    } else if (set_is_mited) {
      flags |= telegram_api::phone_editGroupCallParticipant::MUTED_MASK;
    } else if (set_video_is_stopped) {
      flags |= telegram_api::phone_editGroupCallParticipant::VIDEO_STOPPED_MASK;
    } else if (set_video_is_paused) {
      flags |= telegram_api::phone_editGroupCallParticipant::VIDEO_PAUSED_MASK;
    } else if (set_presentation_is_paused) {
      flags |= telegram_api::phone_editGroupCallParticipant::PRESENTATION_PAUSED_MASK;
    }

    send_query(G()->net_query_creator().create(
        telegram_api::phone_editGroupCallParticipant(flags, input_group_call_id.get_input_group_call(),
                                                     std::move(input_peer), is_muted, volume_level, raise_hand,
                                                     video_is_stopped, video_is_paused, presentation_is_paused),
        {{dialog_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_editGroupCallParticipant>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for EditGroupCallParticipantQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class CheckGroupCallQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit CheckGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, vector<int32> &&audio_sources) {
    for (auto &audio_source : audio_sources) {
      CHECK(audio_source != 0);
    }
    send_query(G()->net_query_creator().create(
        telegram_api::phone_checkGroupCall(input_group_call_id.get_input_group_call(), std::move(audio_sources))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_checkGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    vector<int32> active_audio_sources = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for CheckGroupCallQuery: " << active_audio_sources;

    if (!active_audio_sources.empty()) {
      promise_.set_value(Unit());
    } else {
      promise_.set_error(400, "GROUPCALL_JOIN_MISSING");
    }
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class LeaveGroupCallQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit LeaveGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id, int32 audio_source) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_leaveGroupCall(input_group_call_id.get_input_group_call(), audio_source),
        {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_leaveGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for LeaveGroupCallQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class DiscardGroupCallQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;

 public:
  explicit DiscardGroupCallQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(InputGroupCallId input_group_call_id) {
    send_query(G()->net_query_creator().create(
        telegram_api::phone_discardGroupCall(input_group_call_id.get_input_group_call()), {{input_group_call_id}}));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::phone_discardGroupCall>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto ptr = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for DiscardGroupCallQuery: " << to_string(ptr);
    td_->updates_manager_->on_get_updates(std::move(ptr), std::move(promise_));
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

class GroupCallManager::GroupCallMessages {
  FlatHashMap<DialogId, FlatHashSet<int64>, DialogIdHash> random_ids_;
  FlatHashSet<int32> server_ids_;
  int32 current_message_id_ = 0;
  FlatHashMap<int32, int32> server_id_to_message_id_;
  FlatHashMap<int32, int32> message_id_to_server_id_;
  struct MessageInfo {
    DialogId sender_dialog_id_;
    double delete_time_;
    int64 star_count_;
  };
  std::map<int32, MessageInfo, std::greater<int32>> message_info_;

  bool is_new_message(const GroupCallMessage &message) {
    auto server_id = message.get_server_id();
    if (server_id != 0) {
      return server_ids_.insert(server_id).second;
    }
    auto random_id = message.get_random_id();
    if (random_id != 0) {
      auto sender_dialog_id = message.get_sender_dialog_id();
      CHECK(sender_dialog_id.is_valid());
      auto &random_ids = random_ids_[sender_dialog_id];
      return random_ids.insert(random_id).second;
    }
    return true;
  }

 public:
  int32 add_message(const GroupCallMessage &message, int32 delete_in) {
    if (!is_new_message(message)) {
      return 0;
    }
    auto message_id = ++current_message_id_;
    if (current_message_id_ == 2000000000) {
      current_message_id_ = 0;
    }
    auto server_id = message.get_server_id();
    if (server_id != 0) {
      server_id_to_message_id_[server_id] = message_id;
      message_id_to_server_id_[message_id] = server_id;
    }
    auto delete_time = delete_in == 0 ? 0.0 : Time::now() + delete_in;
    message_info_.emplace(
        message_id, MessageInfo{message.get_sender_dialog_id(), delete_time, message.get_paid_message_star_count()});
    return message_id;
  }

  bool on_message_sent(int32 message_id, const GroupCallMessage &message, int32 delete_in) {
    if (!is_new_message(message)) {
      return false;
    }
    auto it = message_info_.find(message_id);
    if (it == message_info_.end()) {
      return false;
    }
    if (it->second.sender_dialog_id_ != message.get_sender_dialog_id()) {
      LOG(ERROR) << "Sender changed from " << it->second.sender_dialog_id_ << " to " << message.get_sender_dialog_id();
      it->second.sender_dialog_id_ = message.get_sender_dialog_id();
    }
    it->second.delete_time_ = delete_in == 0 ? 0.0 : Time::now() + delete_in;
    auto server_id = message.get_server_id();
    CHECK(server_id != 0);
    server_id_to_message_id_[server_id] = message_id;
    auto &old_server_id = message_id_to_server_id_[message_id];
    CHECK(old_server_id == 0);
    old_server_id = server_id;
    return true;
  }

  bool has_message(int32 message_id) const {
    return message_info_.count(message_id) > 0;
  }

  FlatHashSet<int32> get_server_message_ids() const {
    FlatHashSet<int32> result;
    for (auto &it : server_id_to_message_id_) {
      result.insert(it.first);
    }
    return result;
  }

  DialogId get_message_sender_dialog_id(int32 message_id) const {
    auto it = message_info_.find(message_id);
    if (it == message_info_.end()) {
      return DialogId();
    }
    return it->second.sender_dialog_id_;
  }

  std::pair<int32, bool> delete_message(int32 message_id) {
    int32 server_id = 0;
    auto server_id_it = message_id_to_server_id_.find(message_id);
    if (server_id_it != message_id_to_server_id_.end()) {
      server_id = server_id_it->second;
      message_id_to_server_id_.erase(server_id_it);
      auto is_deleted = server_id_to_message_id_.erase(server_id) > 0;
      CHECK(is_deleted);
    }
    return {server_id, message_info_.erase(message_id) > 0};
  }

  vector<int32> delete_all_messages() {
    vector<int32> message_ids;
    for (const auto &it : message_info_) {
      message_ids.push_back(it.first);
    }
    for (auto message_id : message_ids) {
      auto result = delete_message(message_id);
      CHECK(result.second);
    }
    // don't need to clear random_ids_
    server_ids_.clear();
    return message_ids;
  }

  void delete_messages_by_sender(DialogId sender_dialog_id, vector<int32> &server_ids,
                                 vector<int32> &deleted_message_ids) {
    for (const auto &it : message_info_) {
      if (it.second.sender_dialog_id_ == sender_dialog_id) {
        deleted_message_ids.push_back(it.first);
      }
    }
    for (auto message_id : deleted_message_ids) {
      auto result = delete_message(message_id);
      CHECK(result.second);
      if (result.first != 0) {
        server_ids.push_back(result.first);
      }
    }
  }

  vector<int32> delete_old_group_call_messages(const GroupCallMessageLimits &message_limits) {
    constexpr int32 MAX_LEVEL_MESSAGE_COUNT = 100;
    constexpr int32 MAX_LEVEL = 20;
    int32 level_count[MAX_LEVEL] = {0};
    auto now = Time::now();
    vector<int32> deleted_message_ids;
    for (const auto &it : message_info_) {
      if (it.second.delete_time_ > 0.0 && it.second.delete_time_ < now) {
        deleted_message_ids.push_back(it.first);
      } else {
        auto level = clamp(message_limits.get_level(it.second.star_count_), 0, MAX_LEVEL - 1);
        if (++level_count[level] >= MAX_LEVEL_MESSAGE_COUNT) {
          deleted_message_ids.push_back(it.first);
        }
      }
    }
    for (auto message_id : deleted_message_ids) {
      auto result = delete_message(message_id);
      CHECK(result.second);
    }
    return deleted_message_ids;
  }

  vector<int32> delete_server_messages(const vector<int32> &server_ids) {
    vector<int32> deleted_message_ids;
    for (auto server_id : server_ids) {
      auto message_id_it = server_id_to_message_id_.find(server_id);
      if (message_id_it == server_id_to_message_id_.end()) {
        continue;
      }
      auto message_id = message_id_it->second;
      auto real_server_id = delete_message(message_id).first;
      CHECK(real_server_id == server_id);
      deleted_message_ids.push_back(message_id);
      return deleted_message_ids;
    }
    return deleted_message_ids;
  }

  bool is_empty() const {
    return message_info_.empty();
  }

  double get_next_delete_time() const {
    double next_delete_time = 0.0;
    for (const auto &it : message_info_) {
      if (it.second.delete_time_ != 0 && (next_delete_time == 0.0 || it.second.delete_time_ < next_delete_time)) {
        next_delete_time = it.second.delete_time_;
      }
    }
    return next_delete_time;
  }
};

struct GroupCallManager::GroupCall {
  InputGroupCallId input_group_call_id;
  GroupCallId group_call_id;
  DialogId dialog_id;
  string title;
  string invite_link;
  int64 paid_message_star_count = 0;
  DialogId message_sender_dialog_id;
  bool is_inited = false;
  bool is_active = false;
  bool is_conference = false;
  bool is_live_story = false;
  bool is_rtmp_stream = false;
  bool is_joined = false;
  bool need_rejoin = false;
  bool is_being_joined = false;
  bool is_being_left = false;
  bool is_speaking = false;
  bool can_self_unmute = false;
  bool is_creator = false;
  bool can_be_managed = false;
  bool has_hidden_listeners = false;
  bool syncing_participants = false;
  bool need_syncing_participants = false;
  bool loaded_all_participants = false;
  bool start_subscribed = false;
  bool is_my_video_paused = false;
  bool is_my_video_enabled = false;
  bool is_my_presentation_paused = false;
  bool mute_new_participants = false;
  bool allowed_toggle_mute_new_participants = false;
  bool joined_date_asc = false;
  bool is_video_recorded = false;
  bool are_messages_enabled = false;
  bool allowed_toggle_are_messages_enabled = false;
  bool is_blockchain_being_polled[2] = {false, false};
  bool can_choose_message_sender = true;
  bool loaded_available_message_senders = false;
  int32 scheduled_start_date = 0;
  int32 participant_count = 0;
  int32 duration = 0;
  int32 audio_source = 0;
  int32 joined_date = 0;
  int32 record_start_date = 0;
  int32 unmuted_video_count = 0;
  int32 unmuted_video_limit = 0;
  DcId stream_dc_id;
  DialogId as_dialog_id;
  tde2e_api::PrivateKeyId private_key_id{};
  tde2e_api::PublicKeyId public_key_id{};
  tde2e_api::CallId call_id{};
  tde2e_api::CallVerificationState call_verification_state;
  int32 block_next_offset[2] = {};
  vector<int64> blockchain_participant_ids;
  GroupCallMessages messages;
  vector<GroupCallMessage> old_messages;
  int64 pending_reaction_star_count = 0;

  int32 version = -1;
  int32 leave_version = -1;
  int32 title_version = -1;
  int32 start_subscribed_version = -1;
  int32 can_enable_video_version = -1;
  int32 mute_version = -1;
  int32 are_messages_enabled_version = -1;
  int32 paid_message_star_count_version = -1;
  int32 stream_dc_id_version = -1;
  int32 record_start_date_version = -1;
  int32 scheduled_start_date_version = -1;

  vector<Promise<Unit>> after_join;
  bool have_pending_start_subscribed = false;
  bool pending_start_subscribed = false;
  bool have_pending_is_my_video_paused = false;
  bool pending_is_my_video_paused = false;
  bool have_pending_is_my_video_enabled = false;
  bool pending_is_my_video_enabled = false;
  bool have_pending_is_my_presentation_paused = false;
  bool pending_is_my_presentation_paused = false;
  bool have_pending_mute_new_participants = false;
  bool pending_mute_new_participants = false;
  bool have_pending_are_messages_enabled = false;
  bool pending_are_messages_enabled = false;
  bool have_pending_paid_message_star_count = false;
  int64 pending_paid_message_star_count = 0;
  string pending_title;
  bool have_pending_record_start_date = false;
  int32 pending_record_start_date = 0;
  string pending_record_title;
  bool pending_record_record_video = false;
  bool pending_record_use_portrait_orientation = false;
  uint64 toggle_recording_generation = 0;
};

struct GroupCallManager::GroupCallParticipants {
  vector<GroupCallParticipant> participants;
  string next_offset;
  GroupCallParticipantOrder min_order = GroupCallParticipantOrder::max();
  bool joined_date_asc = false;
  int32 local_unmuted_video_count = 0;

  bool are_administrators_loaded = false;
  vector<DialogId> administrator_dialog_ids;

  bool are_top_donors_loaded = false;
  int64 total_star_count = 0;
  vector<MessageReactor> top_donors;

  struct PendingUpdates {
    FlatHashMap<DialogId, unique_ptr<GroupCallParticipant>, DialogIdHash> updates;
  };
  std::map<int32, PendingUpdates> pending_version_updates_;
  std::map<int32, PendingUpdates> pending_mute_updates_;
};

struct GroupCallManager::GroupCallRecentSpeakers {
  vector<std::pair<DialogId, int32>> users;  // participant + time; sorted by time
  bool is_changed = false;
  vector<std::pair<DialogId, bool>> last_sent_users;
};

struct GroupCallManager::PendingJoinRequest {
  NetQueryRef query_ref;
  uint64 generation = 0;
  int32 audio_source = 0;
  DialogId as_dialog_id;

  tde2e_api::PrivateKeyId private_key_id{};
  tde2e_api::PublicKeyId public_key_id{};

  Promise<string> promise;
};

struct GroupCallManager::PendingJoinPresentationRequest {
  NetQueryRef query_ref;
  uint64 generation = 0;
  int32 audio_source = 0;

  Promise<string> promise;
};

GroupCallManager::GroupCallManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  update_group_call_participant_order_timeout_.set_callback(on_update_group_call_participant_order_timeout_callback);
  update_group_call_participant_order_timeout_.set_callback_data(static_cast<void *>(this));

  check_group_call_is_joined_timeout_.set_callback(on_check_group_call_is_joined_timeout_callback);
  check_group_call_is_joined_timeout_.set_callback_data(static_cast<void *>(this));

  pending_send_speaking_action_timeout_.set_callback(on_pending_send_speaking_action_timeout_callback);
  pending_send_speaking_action_timeout_.set_callback_data(static_cast<void *>(this));

  recent_speaker_update_timeout_.set_callback(on_recent_speaker_update_timeout_callback);
  recent_speaker_update_timeout_.set_callback_data(static_cast<void *>(this));

  sync_participants_timeout_.set_callback(on_sync_participants_timeout_callback);
  sync_participants_timeout_.set_callback_data(static_cast<void *>(this));

  update_group_call_timeout_.set_callback(on_update_group_call_timeout_callback);
  update_group_call_timeout_.set_callback_data(static_cast<void *>(this));

  poll_group_call_blocks_timeout_.set_callback(on_poll_group_call_blocks_timeout_callback);
  poll_group_call_blocks_timeout_.set_callback_data(static_cast<void *>(this));

  delete_group_call_messages_timeout_.set_callback(on_delete_group_call_messages_timeout_callback);
  delete_group_call_messages_timeout_.set_callback_data(static_cast<void *>(this));

  poll_group_call_stars_timeout_.set_callback(on_poll_group_call_stars_timeout_callback);
  poll_group_call_stars_timeout_.set_callback_data(static_cast<void *>(this));

  if (!td_->auth_manager_->is_bot()) {
    auto status = log_event_parse(message_limits_, G()->td_db()->get_binlog_pmc()->get("group_call_message_limits"));
    if (status.is_error()) {
      message_limits_ = GroupCallMessageLimits::basic();
    }
    send_closure(G()->td(), &Td::send_update, message_limits_.get_update_group_call_message_levels_object());
  }
}

GroupCallManager::~GroupCallManager() = default;

void GroupCallManager::tear_down() {
  parent_.reset();
}

void GroupCallManager::on_update_group_call_participant_order_timeout_callback(void *group_call_manager_ptr,
                                                                               int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_update_group_call_participant_order_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_update_group_call_participant_order_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Receive update group call participant order timeout in " << group_call_id;
  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();

  if (!need_group_call_participants(input_group_call_id)) {
    return;
  }

  bool my_can_self_unmute = get_group_call_can_self_unmute(input_group_call_id);
  auto *participants =
      add_group_call_participants(input_group_call_id, "on_update_group_call_participant_order_timeout");
  update_group_call_participants_order(input_group_call_id, my_can_self_unmute, participants,
                                       "on_update_group_call_participant_order_timeout");
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
  auto audio_source = group_call->audio_source;
  if (!group_call->is_joined || group_call->is_being_joined ||
      check_group_call_is_joined_timeout_.has_timeout(group_call_id.get()) || audio_source == 0) {
    return;
  }

  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, audio_source](Result<Unit> &&result) mutable {
        send_closure(actor_id, &GroupCallManager::finish_check_group_call_is_joined, input_group_call_id, audio_source,
                     std::move(result));
      });
  td_->create_handler<CheckGroupCallQuery>(std::move(promise))->send(input_group_call_id, {audio_source});
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
  if (!group_call->is_joined || !group_call->is_speaking || group_call->is_live_story) {
    return;
  }

  CHECK(group_call->as_dialog_id.is_valid());
  on_user_speaking_in_group_call(group_call_id, group_call->as_dialog_id, false, G()->unix_time());

  pending_send_speaking_action_timeout_.add_timeout_in(group_call_id.get(), 4.0);

  td_->dialog_action_manager_->send_dialog_action(group_call->dialog_id, MessageTopic(), {},
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

void GroupCallManager::on_update_group_call_timeout_callback(void *group_call_manager_ptr, int64 call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager), &GroupCallManager::on_update_group_call_timeout,
                     call_id);
}

void GroupCallManager::on_update_group_call_timeout(int64 call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto it = group_call_message_full_ids_.find(call_id);
  if (it == group_call_message_full_ids_.end()) {
    return;
  }
  if (!td_->messages_manager_->need_poll_group_call_message(it->second)) {
    return;
  }
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), call_id](Unit) {
    send_closure(actor_id, &GroupCallManager::on_update_group_call_message, call_id);
  });
  td_->messages_manager_->get_message_from_server(it->second, std::move(promise), "on_update_group_call_timeout");
}

void GroupCallManager::on_update_group_call_message(int64 call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto it = group_call_message_full_ids_.find(call_id);
  if (it == group_call_message_full_ids_.end()) {
    return;
  }
  update_group_call_timeout_.add_timeout_in(call_id, 3);
}

void GroupCallManager::on_poll_group_call_blocks_timeout_callback(void *group_call_manager_ptr, int64 call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_poll_group_call_blocks_timeout, call_id);
}

void GroupCallManager::on_poll_group_call_blocks_timeout(int64 call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto input_group_call_id = get_input_group_call_id(GroupCallId(narrow_cast<int32>(call_id / 2))).move_as_ok();
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined ||
      group_call->is_being_left || !group_call->is_conference || group_call->call_id == tde2e_api::CallId()) {
    return;
  }
  poll_group_call_blocks(group_call, static_cast<int32>(call_id % 2));
}

void GroupCallManager::on_delete_group_call_messages_timeout_callback(void *group_call_manager_ptr,
                                                                      int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_delete_group_call_messages_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_delete_group_call_messages_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr) {
    return;
  }

  on_group_call_messages_deleted(group_call, group_call->messages.delete_old_group_call_messages(message_limits_));
}

void GroupCallManager::on_poll_group_call_stars_timeout_callback(void *group_call_manager_ptr,
                                                                 int64 group_call_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto group_call_manager = static_cast<GroupCallManager *>(group_call_manager_ptr);
  send_closure_later(group_call_manager->actor_id(group_call_manager),
                     &GroupCallManager::on_poll_group_call_stars_timeout,
                     GroupCallId(narrow_cast<int32>(group_call_id_int)));
}

void GroupCallManager::on_poll_group_call_stars_timeout(GroupCallId group_call_id) {
  if (G()->close_flag()) {
    return;
  }

  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();
  auto *group_call = get_group_call(input_group_call_id);
  if (!need_group_call_participants(input_group_call_id, group_call)) {
    return;
  }

  get_group_call_stars_from_server(input_group_call_id, Auto());
}

bool GroupCallManager::is_group_call_being_joined(InputGroupCallId input_group_call_id) const {
  return pending_join_requests_.count(input_group_call_id) != 0;
}

// use get_group_call_is_joined internally instead
bool GroupCallManager::is_group_call_joined(InputGroupCallId input_group_call_id) const {
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr) {
    return false;
  }
  return group_call->is_joined && !group_call->is_being_left;
}

GroupCallId GroupCallManager::get_group_call_id(InputGroupCallId input_group_call_id, DialogId dialog_id,
                                                bool is_live_story) {
  if (td_->auth_manager_->is_bot() || !input_group_call_id.is_valid()) {
    return GroupCallId();
  }
  return add_group_call(input_group_call_id, dialog_id, is_live_story)->group_call_id;
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

GroupCallManager::GroupCall *GroupCallManager::add_group_call(InputGroupCallId input_group_call_id, DialogId dialog_id,
                                                              bool is_live_story) {
  CHECK(!td_->auth_manager_->is_bot());
  auto &group_call = group_calls_[input_group_call_id];
  if (group_call == nullptr) {
    group_call = make_unique<GroupCall>();
    group_call->input_group_call_id = input_group_call_id;
    group_call->group_call_id = get_next_group_call_id(input_group_call_id);
    LOG(INFO) << "Add " << input_group_call_id << " from " << dialog_id << " as " << group_call->group_call_id;
  }
  if (!group_call->dialog_id.is_valid()) {
    group_call->dialog_id = dialog_id;
  }
  if (is_live_story) {
    group_call->is_live_story = is_live_story;
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

Status GroupCallManager::can_join_video_chats(DialogId dialog_id) const {
  TRY_STATUS(td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "can_join_video_chats"));
  switch (dialog_id.get_type()) {
    case DialogType::Chat:
    case DialogType::Channel:
      break;
    case DialogType::User:
      return Status::Error(400, "Chat can't have a video chat");
    case DialogType::SecretChat:
    case DialogType::None:
    default:
      UNREACHABLE();
      break;
  }
  return Status::OK();
}

Status GroupCallManager::can_manage_video_chats(DialogId dialog_id) const {
  switch (dialog_id.get_type()) {
    case DialogType::Chat: {
      auto chat_id = dialog_id.get_chat_id();
      if (!td_->chat_manager_->get_chat_permissions(chat_id).can_manage_calls()) {
        return Status::Error(400, "Not enough rights in the chat");
      }
      break;
    }
    case DialogType::Channel: {
      auto channel_id = dialog_id.get_channel_id();
      if (!td_->chat_manager_->get_channel_permissions(channel_id).can_manage_calls()) {
        return Status::Error(400, "Not enough rights in the chat");
      }
      break;
    }
    case DialogType::User:
    case DialogType::SecretChat:
      return Status::Error(400, "Chat can't have a video chat");
    case DialogType::None:
    default:
      UNREACHABLE();
  }
  return Status::OK();
}

bool GroupCallManager::get_group_call_is_creator(const GroupCall *group_call) {
  if (group_call == nullptr || !group_call->is_creator) {
    return false;
  }
  return group_call->is_conference || group_call->is_live_story;
}

bool GroupCallManager::can_manage_group_call(InputGroupCallId input_group_call_id) const {
  return can_manage_group_call(get_group_call(input_group_call_id));
}

bool GroupCallManager::can_manage_group_call(const GroupCall *group_call) const {
  if (group_call == nullptr) {
    return false;
  }
  if (group_call->is_conference) {
    return group_call->is_creator;
  }
  auto dialog_id = group_call->dialog_id;
  if (group_call->is_live_story) {
    if (group_call->is_creator) {
      return true;
    }
    switch (dialog_id.get_type()) {
      case DialogType::User:
        return dialog_id == td_->dialog_manager_->get_my_dialog_id();
      case DialogType::Channel:
        return td_->chat_manager_->get_channel_permissions(dialog_id.get_channel_id()).can_manage_calls();
      case DialogType::Chat:
      case DialogType::SecretChat:
      case DialogType::None:
      default:
        return false;
    }
  }
  // video chat
  switch (dialog_id.get_type()) {
    case DialogType::Chat:
      return td_->chat_manager_->get_chat_permissions(dialog_id.get_chat_id()).can_manage_calls();
    case DialogType::Channel:
      return td_->chat_manager_->get_channel_permissions(dialog_id.get_channel_id()).can_manage_calls();
    case DialogType::User:
    case DialogType::SecretChat:
    case DialogType::None:
    default:
      return false;
  }
}

bool GroupCallManager::get_group_call_can_self_unmute(InputGroupCallId input_group_call_id) const {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  return group_call->can_self_unmute;
}

bool GroupCallManager::get_group_call_joined_date_asc(InputGroupCallId input_group_call_id) const {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  return group_call->joined_date_asc;
}

void GroupCallManager::get_group_call_join_as(DialogId dialog_id,
                                              Promise<td_api::object_ptr<td_api::messageSenders>> &&promise) {
  TRY_STATUS_PROMISE(promise, can_join_video_chats(dialog_id));

  td_->create_handler<GetGroupCallJoinAsQuery>(std::move(promise))->send(dialog_id);
}

void GroupCallManager::get_group_call_streamer(GroupCallId group_call_id,
                                               Promise<td_api::object_ptr<td_api::groupCallParticipant>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  auto dialog_id = group_call->dialog_id;
  if (!group_call->is_live_story || !dialog_id.is_valid() || group_call->is_rtmp_stream) {
    return promise.set_value(nullptr);
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::get_group_call_streamer, group_call_id, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  td_->create_handler<GetGroupCallStreamerQuery>(std::move(promise))->send(input_group_call_id, dialog_id);
}

void GroupCallManager::on_update_group_call_can_choose_message_sender(InputGroupCallId input_group_call_id,
                                                                      bool can_choose_message_sender) {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  group_call->loaded_available_message_senders = true;
  group_call->can_choose_message_sender = can_choose_message_sender;
}

void GroupCallManager::get_group_call_send_as(GroupCallId group_call_id,
                                              Promise<td_api::object_ptr<td_api::chatMessageSenders>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  auto dialog_id = group_call->dialog_id;
  if (!group_call->is_live_story || !dialog_id.is_valid()) {
    return promise.set_value(td_api::make_object<td_api::chatMessageSenders>());
  }
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "get_group_call_send_as"));

  MultiPromiseActorSafe mpas{"GetGroupCallSendAsMultiPromiseActor"};
  mpas.add_promise(PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure_later(actor_id, &GroupCallManager::do_get_group_call_send_as, input_group_call_id,
                             std::move(promise));
        }
      }));
  auto lock = mpas.get_promise();
  td_->create_handler<GetGroupCallSendAsQuery>(group_call->loaded_available_message_senders ? Promise<Unit>()
                                                                                            : mpas.get_promise())
      ->send(input_group_call_id, dialog_id);
  td_->chat_manager_->load_created_public_broadcasts(mpas.get_promise());
  td_->user_manager_->get_me(mpas.get_promise());
  lock.set_value(Unit());
}

void GroupCallManager::do_get_group_call_send_as(InputGroupCallId input_group_call_id,
                                                 Promise<td_api::object_ptr<td_api::chatMessageSenders>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  CHECK(group_call->is_inited);
  if (!group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  auto dialog_id = group_call->dialog_id;
  if (!group_call->is_live_story || !dialog_id.is_valid()) {
    return promise.set_value(td_api::make_object<td_api::chatMessageSenders>());
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id,
                                                               promise =
                                                                   std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          promise.set_error(400, "GROUPCALL_JOIN_MISSING");
        } else {
          send_closure(actor_id, &GroupCallManager::do_get_group_call_send_as, input_group_call_id, std::move(promise));
        }
      }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "do_get_group_call_send_as"));

  auto senders = td_api::make_object<td_api::chatMessageSenders>();
  auto add_sender = [&senders, td = td_](DialogId sender_dialog_id) {
    senders->senders_.push_back(td_api::make_object<td_api::chatMessageSender>(
        get_message_sender_object(td, sender_dialog_id, "do_get_group_call_send_as"), false));
  };
  if (dialog_id.get_type() == DialogType::Channel && group_call->can_be_managed) {
    add_sender(dialog_id);
  }
  bool are_messages_enabled = get_group_call_are_messages_enabled(group_call);
  if (are_messages_enabled || group_call->can_be_managed) {
    add_sender(td_->dialog_manager_->get_my_dialog_id());
  }
  if (are_messages_enabled && group_call->can_choose_message_sender) {
    const auto &created_public_broadcasts = td_->chat_manager_->get_created_public_broadcasts();
    std::multimap<int64, ChannelId> sorted_channel_ids;
    for (auto channel_id : created_public_broadcasts) {
      int64 score = td_->chat_manager_->get_channel_participant_count(channel_id);
      sorted_channel_ids.emplace(-score, channel_id);
    };
    for (auto &channel_id : sorted_channel_ids) {
      auto channel_dialog_id = DialogId(channel_id.second);
      if (channel_dialog_id != dialog_id) {
        add_sender(channel_dialog_id);
      }
    }
  }
  promise.set_value(std::move(senders));
}

void GroupCallManager::set_group_call_default_join_as(DialogId dialog_id, DialogId as_dialog_id,
                                                      Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, can_join_video_chats(dialog_id));

  switch (as_dialog_id.get_type()) {
    case DialogType::User:
      if (as_dialog_id != td_->dialog_manager_->get_my_dialog_id()) {
        return promise.set_error(400, "Can't join video chat as another user");
      }
      break;
    case DialogType::Chat:
    case DialogType::Channel:
      if (!td_->dialog_manager_->have_dialog_force(as_dialog_id, "set_group_call_default_join_as 2")) {
        return promise.set_error(400, "Participant chat not found");
      }
      break;
    case DialogType::SecretChat:
      return promise.set_error(400, "Can't join video chat as a secret chat");
    default:
      return promise.set_error(400, "Invalid default participant identifier specified");
  }
  if (!td_->dialog_manager_->have_input_peer(as_dialog_id, false, AccessRights::Read)) {
    return promise.set_error(400, "Can't access specified default participant chat");
  }

  td_->create_handler<SaveDefaultGroupCallJoinAsQuery>(std::move(promise))->send(dialog_id, as_dialog_id);
  td_->messages_manager_->on_update_dialog_default_join_group_call_as_dialog_id(dialog_id, as_dialog_id, true);
}

void GroupCallManager::set_group_call_default_send_as(GroupCallId group_call_id, DialogId as_dialog_id,
                                                      Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_live_story) {
    return promise.set_error(400, "Group call message sender can't be set explicitly");
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, as_dialog_id,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::set_group_call_default_send_as, group_call_id, as_dialog_id,
                           std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(as_dialog_id, false, AccessRights::Read,
                                                                        "set_group_call_default_send_as"));
  if (as_dialog_id.get_type() == DialogType::User && as_dialog_id != td_->dialog_manager_->get_my_dialog_id()) {
    return promise.set_error(400, "Can't send live story comments as another user");
  }
  if (group_call->message_sender_dialog_id == as_dialog_id) {
    return promise.set_value(Unit());
  }
  group_call->message_sender_dialog_id = as_dialog_id;
  send_update_group_call(group_call, "set_group_call_default_send_as");

  td_->create_handler<SaveDefaultGroupCallSendAsQuery>(std::move(promise))->send(input_group_call_id, as_dialog_id);
}

void GroupCallManager::create_video_chat(DialogId dialog_id, string title, int32 start_date, bool is_rtmp_stream,
                                         Promise<GroupCallId> &&promise) {
  TRY_STATUS_PROMISE(
      promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read, "create_video_chat"));
  TRY_STATUS_PROMISE(promise, can_manage_video_chats(dialog_id));

  title = clean_name(title, MAX_TITLE_LENGTH);

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), dialog_id, promise = std::move(promise)](Result<InputGroupCallId> result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure(actor_id, &GroupCallManager::on_video_chat_created, dialog_id, result.move_as_ok(),
                       std::move(promise));
        }
      });
  td_->create_handler<CreateGroupCallQuery>(std::move(query_promise))
      ->send(dialog_id, title, start_date, is_rtmp_stream);
}

void GroupCallManager::create_group_call(td_api::object_ptr<td_api::groupCallJoinParameters> &&join_parameters,
                                         Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_RESULT_PROMISE(promise, parameters,
                     GroupCallJoinParameters::get_group_call_join_parameters(std::move(join_parameters), true));

  BeingCreatedCall data;
  if (!parameters.is_empty()) {
    data.is_join_ = true;
    auto r_private_key_id = tde2e_api::key_generate_temporary_private_key();
    if (r_private_key_id.is_error()) {
      return promise.set_error(400, "Failed to generate encryption key");
    }
    data.private_key_id_ = tde2e_move_as_ok(r_private_key_id);

    auto public_key_string = tde2e_move_as_ok(tde2e_api::key_to_public_key(data.private_key_id_));
    data.public_key_id_ = tde2e_move_as_ok(tde2e_api::key_from_public_key(public_key_string));
    data.audio_source_ = parameters.audio_source_;
  }

  auto random_id = 0;
  do {
    random_id = Random::secure_int32();
  } while (random_id == 0 || being_created_group_calls_.count(random_id) > 0);
  being_created_group_calls_[random_id] = data;

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), random_id, promise = std::move(promise)](
                                 Result<telegram_api::object_ptr<telegram_api::Updates>> &&r_updates) mutable {
        send_closure(actor_id, &GroupCallManager::on_create_group_call, random_id, std::move(r_updates),
                     std::move(promise));
      });
  td_->create_handler<CreateConferenceCallQuery>(std::move(query_promise))
      ->send(random_id, data.is_join_, parameters, data.private_key_id_, data.public_key_id_);
}

void GroupCallManager::on_create_group_call(int32 random_id,
                                            Result<telegram_api::object_ptr<telegram_api::Updates>> &&r_updates,
                                            Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto it = being_created_group_calls_.find(random_id);
  CHECK(it != being_created_group_calls_.end());
  auto data = std::move(it->second);
  being_created_group_calls_.erase(it);

  InputGroupCallId input_group_call_id;
  if (r_updates.is_ok()) {
    input_group_call_id = td_->updates_manager_->get_update_new_group_call_id(r_updates.ok().get());
    if (!input_group_call_id.is_valid()) {
      r_updates = Status::Error(500, "Receive wrong response");
    }
  }
  if (data.is_join_ && pending_join_requests_.count(input_group_call_id) != 0) {
    r_updates = Status::Error(500, "Join just created call");
  }
  if (r_updates.is_error()) {
    if (data.is_join_) {
      auto r_ok = tde2e_api::key_destroy(data.private_key_id_);
      CHECK(r_ok.is_ok());
      r_ok = tde2e_api::key_destroy(data.public_key_id_);
      CHECK(r_ok.is_ok());
    }
    return promise.set_error(r_updates.move_as_error());
  }

  process_join_group_call_response(input_group_call_id, data.is_join_, data.audio_source_, data.private_key_id_,
                                   data.public_key_id_, r_updates.move_as_ok(), std::move(promise));
}

void GroupCallManager::on_get_group_call_join_payload(InputGroupCallId input_group_call_id, string &&payload) {
  if (payload.empty()) {
    LOG(ERROR) << "Receive empty join payload";
    return;
  }
  auto &join_payload = group_call_join_payloads_[input_group_call_id];
  if (!join_payload.empty()) {
    LOG(ERROR) << "Receive multiple join payloads";
    return;
  }
  join_payload = std::move(payload);
  LOG(INFO) << "Save join payload for " << input_group_call_id;
}

void GroupCallManager::on_create_group_call_finished(InputGroupCallId input_group_call_id, bool is_join,
                                                     Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  LOG(INFO) << "Finish creation of " << input_group_call_id;
  string payload;
  if (is_join) {
    auto it = group_call_join_payloads_.find(input_group_call_id);
    if (it == group_call_join_payloads_.end()) {
      promise.set_error(500, "Receive no join payload");
      return finish_join_group_call(input_group_call_id, 1, Status::Error(500, "Receive no join payload"));
    }
    payload = std::move(it->second);
    group_call_join_payloads_.erase(it);
  }

  const auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  promise.set_value(td_api::make_object<td_api::groupCallInfo>(group_call->group_call_id.get(), payload));
}

void GroupCallManager::get_video_chat_rtmp_stream_url(DialogId dialog_id, bool is_story, bool revoke,
                                                      Promise<td_api::object_ptr<td_api::rtmpUrl>> &&promise) {
  TRY_STATUS_PROMISE(promise, td_->dialog_manager_->check_dialog_access(dialog_id, false, AccessRights::Read,
                                                                        "get_video_chat_rtmp_stream_url"));
  if (is_story) {
    if (!td_->story_manager_->can_post_stories(dialog_id)) {
      return promise.set_error(400, "Not enough rights");
    }
  } else {
    TRY_STATUS_PROMISE(promise, can_manage_video_chats(dialog_id));
  }

  td_->create_handler<GetGroupCallStreamRtmpUrlQuery>(std::move(promise))->send(dialog_id, is_story, revoke);
}

void GroupCallManager::on_video_chat_created(DialogId dialog_id, InputGroupCallId input_group_call_id,
                                             Promise<GroupCallId> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  CHECK(input_group_call_id.is_valid());

  td_->messages_manager_->on_update_dialog_group_call(dialog_id, true, true, "on_video_chat_created");
  td_->messages_manager_->on_update_dialog_group_call_id(dialog_id, input_group_call_id);

  promise.set_value(get_group_call_id(input_group_call_id, dialog_id, false));
}

void GroupCallManager::get_group_call(GroupCallId group_call_id,
                                      Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call != nullptr && group_call->is_inited) {
    return promise.set_value(get_group_call_object(td_, group_call, get_recent_speakers(group_call, false)));
  }

  reload_group_call(input_group_call_id, std::move(promise));
}

void GroupCallManager::on_update_group_call_rights(InputGroupCallId input_group_call_id) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (need_group_call_participants(input_group_call_id, group_call)) {
    CHECK(group_call != nullptr && group_call->is_inited);
    try_load_group_call_administrators(input_group_call_id, group_call->dialog_id);

    auto *group_call_participants = add_group_call_participants(input_group_call_id, "on_update_group_call_rights");
    if (group_call_participants->are_administrators_loaded) {
      update_group_call_participants_can_be_muted(input_group_call_id, can_manage_group_call(group_call),
                                                  group_call_participants, get_group_call_is_creator(group_call));
    }
  }

  if (group_call != nullptr && group_call->is_inited) {
    bool can_be_managed = !group_call->is_conference && group_call->is_active && can_manage_group_call(group_call);
    if (can_be_managed != group_call->can_be_managed) {
      group_call->can_be_managed = can_be_managed;
      send_update_group_call(group_call, "on_update_group_call_rights");
    }
  }

  reload_group_call(input_group_call_id, Auto());
}

void GroupCallManager::reload_group_call(InputGroupCallId input_group_call_id,
                                         Promise<td_api::object_ptr<td_api::groupCall>> &&promise) {
  if (td_->auth_manager_->is_bot()) {
    return promise.set_error(400, "Bots can't get group call info");
  }
  if (!input_group_call_id.is_valid()) {
    return promise.set_error(400, "Invalid group call identifier specified");
  }

  auto &queries = load_group_call_queries_[input_group_call_id];
  queries.push_back(std::move(promise));
  if (queries.size() == 1) {
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](
                                                    Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
      send_closure(actor_id, &GroupCallManager::finish_get_group_call, input_group_call_id, std::move(result));
    });
    td_->create_handler<GetGroupCallQuery>(std::move(query_promise))->send(input_group_call_id, 3);
  }
}

void GroupCallManager::finish_get_group_call(InputGroupCallId input_group_call_id,
                                             Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
  G()->ignore_result_if_closing(result);

  auto it = load_group_call_queries_.find(input_group_call_id);
  CHECK(it != load_group_call_queries_.end());
  CHECK(!it->second.empty());
  auto promises = std::move(it->second);
  load_group_call_queries_.erase(it);

  if (result.is_ok()) {
    td_->user_manager_->on_get_users(std::move(result.ok_ref()->users_), "finish_get_group_call");
    td_->chat_manager_->on_get_chats(std::move(result.ok_ref()->chats_), "finish_get_group_call");

    if (update_group_call(result.ok()->call_, DialogId(), false) != input_group_call_id) {
      LOG(ERROR) << "Expected " << input_group_call_id << ", but received " << to_string(result.ok());
      result = Status::Error(500, "Receive another group call");
    }
  }

  if (result.is_error()) {
    fail_promises(promises, result.move_as_error());
    return;
  }

  auto call = result.move_as_ok();
  int32 version = 0;
  if (call->call_->get_id() == telegram_api::groupCall::ID) {
    version = static_cast<const telegram_api::groupCall *>(call->call_.get())->version_;
  }
  process_group_call_participants(input_group_call_id, std::move(call->participants_), version, string(), true, false);
  auto *group_call = get_group_call(input_group_call_id);
  if (need_group_call_participants(input_group_call_id, group_call)) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "finish_get_group_call");
    if (group_call_participants->next_offset.empty()) {
      group_call_participants->next_offset = std::move(call->participants_next_offset_);
    }
  }

  CHECK(group_call != nullptr && group_call->is_inited);
  for (auto &promise : promises) {
    if (promise) {
      promise.set_value(get_group_call_object(td_, group_call, get_recent_speakers(group_call, false)));
    }
  }
}

void GroupCallManager::finish_check_group_call_is_joined(InputGroupCallId input_group_call_id, int32 audio_source,
                                                         Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  LOG(INFO) << "Finish check group call is_joined for " << input_group_call_id;

  if (result.is_error()) {
    auto message = result.error().message();
    if (message == "GROUPCALL_JOIN_MISSING" || message == "GROUPCALL_FORBIDDEN" || message == "GROUPCALL_INVALID") {
      on_group_call_left(input_group_call_id, audio_source, message == "GROUPCALL_JOIN_MISSING");
    }
  }

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  CHECK(audio_source != 0);
  if (!group_call->is_joined || group_call->is_being_joined ||
      check_group_call_is_joined_timeout_.has_timeout(group_call->group_call_id.get()) ||
      group_call->audio_source != audio_source) {
    return;
  }

  if (group_call->is_conference) {
    create_actor<SleepActor>(
        "SyncConferenceCallParticipantsActor", 1.0,
        PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id,
                                blockchain_participant_ids = group_call->blockchain_participant_ids](Unit) mutable {
          send_closure(actor_id, &GroupCallManager::sync_conference_call_participants, input_group_call_id,
                       std::move(blockchain_participant_ids));
        }))
        .release();
  }

  int32 next_timeout = result.is_ok() ? CHECK_GROUP_CALL_IS_JOINED_TIMEOUT : 1;
  check_group_call_is_joined_timeout_.set_timeout_in(group_call->group_call_id.get(), next_timeout);
}

void GroupCallManager::sync_conference_call_participants(InputGroupCallId input_group_call_id,
                                                         vector<int64> &&blockchain_participant_ids) {
  if (G()->close_flag()) {
    return;
  }

  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id,
                                         blockchain_participant_ids = std::move(blockchain_participant_ids)](
                                            Result<vector<int64>> r_participants) mutable {
    if (r_participants.is_ok()) {
      send_closure(actor_id, &GroupCallManager::on_sync_conference_call_participants, input_group_call_id,
                   std::move(blockchain_participant_ids), r_participants.move_as_ok());
    }
  });
  td_->create_handler<GetGroupCallParticipantsToCheckQuery>(std::move(promise))->send(input_group_call_id);
}

void GroupCallManager::on_sync_conference_call_participants(InputGroupCallId input_group_call_id,
                                                            vector<int64> &&blockchain_participant_ids,
                                                            vector<int64> &&server_participant_ids) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (!group_call->is_joined || group_call->is_being_joined) {
    return;
  }

  vector<int64> removed_user_ids;
  for (auto participant_id : blockchain_participant_ids) {
    if (!td::contains(server_participant_ids, participant_id)) {
      removed_user_ids.push_back(participant_id);
    }
  }
  do_delete_group_call_participants(input_group_call_id, std::move(removed_user_ids), false, Promise<Unit>());
}

const string &GroupCallManager::get_group_call_title(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->pending_title.empty() ? group_call->title : group_call->pending_title;
}

bool GroupCallManager::get_group_call_is_joined(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return (group_call->is_joined || group_call->is_being_joined) && !group_call->is_being_left;
}

bool GroupCallManager::get_group_call_start_subscribed(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_start_subscribed ? group_call->pending_start_subscribed
                                                   : group_call->start_subscribed;
}

bool GroupCallManager::get_group_call_is_my_video_paused(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_is_my_video_paused ? group_call->pending_is_my_video_paused
                                                     : group_call->is_my_video_paused;
}

bool GroupCallManager::get_group_call_is_my_video_enabled(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_is_my_video_enabled ? group_call->pending_is_my_video_enabled
                                                      : group_call->is_my_video_enabled;
}

bool GroupCallManager::get_group_call_is_my_presentation_paused(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_is_my_presentation_paused ? group_call->pending_is_my_presentation_paused
                                                            : group_call->is_my_presentation_paused;
}

bool GroupCallManager::get_group_call_mute_new_participants(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_mute_new_participants ? group_call->pending_mute_new_participants
                                                        : group_call->mute_new_participants;
}

bool GroupCallManager::get_group_call_are_messages_enabled(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_are_messages_enabled ? group_call->pending_are_messages_enabled
                                                       : group_call->are_messages_enabled;
}

int64 GroupCallManager::get_group_call_paid_message_star_count(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_paid_message_star_count ? group_call->pending_paid_message_star_count
                                                          : group_call->paid_message_star_count;
}

int32 GroupCallManager::get_group_call_record_start_date(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_record_start_date ? group_call->pending_record_start_date
                                                    : group_call->record_start_date;
}

bool GroupCallManager::get_group_call_is_video_recorded(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return group_call->have_pending_record_start_date ? group_call->pending_record_record_video
                                                    : group_call->is_video_recorded;
}

bool GroupCallManager::get_group_call_has_recording(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  return get_group_call_record_start_date(group_call) != 0;
}

bool GroupCallManager::get_group_call_can_enable_video(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  if (group_call->unmuted_video_limit <= 0) {
    return true;
  }
  return group_call->unmuted_video_count < group_call->unmuted_video_limit;
}

bool GroupCallManager::get_group_call_can_delete_messages(const GroupCall *group_call) {
  CHECK(group_call != nullptr);
  if (!group_call->is_live_story) {
    return false;
  }
  return group_call->can_be_managed;
}

bool GroupCallManager::is_group_call_active(const GroupCall *group_call) {
  return group_call != nullptr && group_call->is_inited && group_call->is_active;
}

bool GroupCallManager::need_group_call_participants(InputGroupCallId input_group_call_id) const {
  return need_group_call_participants(input_group_call_id, get_group_call(input_group_call_id));
}

bool GroupCallManager::need_group_call_participants(InputGroupCallId input_group_call_id,
                                                    const GroupCall *group_call) const {
  if (!is_group_call_active(group_call) || group_call->is_being_left) {
    return false;
  }
  if (group_call->is_joined || group_call->need_rejoin || group_call->is_being_joined ||
      (group_call->is_conference && pending_join_requests_.count(input_group_call_id) != 0)) {
    return true;
  }
  return false;
}

void GroupCallManager::on_get_group_call_participants(
    InputGroupCallId input_group_call_id, tl_object_ptr<telegram_api::phone_groupParticipants> &&participants,
    bool is_load, const string &offset) {
  LOG(INFO) << "Receive group call participants: " << to_string(participants);

  CHECK(participants != nullptr);
  td_->user_manager_->on_get_users(std::move(participants->users_), "on_get_group_call_participants");
  td_->chat_manager_->on_get_chats(std::move(participants->chats_), "on_get_group_call_participants");

  if (!need_group_call_participants(input_group_call_id)) {
    return;
  }

  bool is_sync = is_load && offset.empty();
  if (is_sync) {
    auto *group_call = get_group_call(input_group_call_id);
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
  process_group_call_participants(input_group_call_id, std::move(participants->participants_), participants->version_,
                                  offset, is_load, is_sync);

  if (!is_sync) {
    on_receive_group_call_version(input_group_call_id, participants->version_);
  }

  if (is_load) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "on_get_group_call_participants");
    if (group_call_participants->next_offset == offset) {
      if (!offset.empty() && participants->next_offset_.empty() && group_call_participants->joined_date_asc) {
        LOG(INFO) << "Ignore empty next_offset";
      } else {
        group_call_participants->next_offset = std::move(participants->next_offset_);
      }
    }

    if (is_empty || is_sync) {
      bool need_update = false;
      auto *group_call = get_group_call(input_group_call_id);
      CHECK(group_call != nullptr && group_call->is_inited);
      if (is_empty && !group_call->loaded_all_participants) {
        group_call->loaded_all_participants = true;
        need_update = true;
      }

      auto real_participant_count = participants->count_;
      if (!group_call->is_joined) {
        real_participant_count++;
      }
      if (is_empty) {
        auto known_participant_count = static_cast<int32>(group_call_participants->participants.size());
        if (real_participant_count != known_participant_count) {
          LOG(ERROR) << "Receive participant count " << real_participant_count << ", but know "
                     << known_participant_count << " participants in " << input_group_call_id << " from "
                     << group_call->dialog_id;
          real_participant_count = known_participant_count;
        }
      }
      if (!is_empty && is_sync && group_call->loaded_all_participants && real_participant_count > 50) {
        group_call->loaded_all_participants = false;
        need_update = true;
      }
      if (real_participant_count != group_call->participant_count) {
        if (!is_sync) {
          LOG(ERROR) << "Have participant count " << group_call->participant_count << " instead of "
                     << real_participant_count << " in " << input_group_call_id << " from " << group_call->dialog_id;
        }
        need_update |=
            set_group_call_participant_count(group_call, real_participant_count, "on_get_group_call_participants 1");
      }
      if (process_pending_group_call_participant_updates(input_group_call_id)) {
        need_update = false;
      }
      if (group_call->loaded_all_participants || !group_call_participants->min_order.has_video()) {
        set_group_call_unmuted_video_count(group_call, group_call_participants->local_unmuted_video_count,
                                           "on_get_group_call_participants 2");
      }
      if (need_update) {
        send_update_group_call(group_call, "on_get_group_call_participants 3");
      }

      if (is_sync && group_call->need_syncing_participants) {
        group_call->need_syncing_participants = false;
        sync_group_call_participants(input_group_call_id);
      }
    }
  }
}

GroupCallManager::GroupCallParticipants *GroupCallManager::add_group_call_participants(
    InputGroupCallId input_group_call_id, const char *source) {
  LOG_CHECK(need_group_call_participants(input_group_call_id)) << source;

  auto &participants = group_call_participants_[input_group_call_id];
  if (participants == nullptr) {
    participants = make_unique<GroupCallParticipants>();
    participants->joined_date_asc = get_group_call_joined_date_asc(input_group_call_id);
  }
  return participants.get();
}

GroupCallParticipant *GroupCallManager::get_group_call_participant(InputGroupCallId input_group_call_id,
                                                                   DialogId dialog_id, const char *source) {
  return get_group_call_participant(add_group_call_participants(input_group_call_id, source), dialog_id);
}

GroupCallParticipant *GroupCallManager::get_group_call_participant(GroupCallParticipants *group_call_participants,
                                                                   DialogId dialog_id) const {
  if (!dialog_id.is_valid()) {
    return nullptr;
  }
  if (dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    for (auto &group_call_participant : group_call_participants->participants) {
      if (group_call_participant.is_self) {
        return &group_call_participant;
      }
    }
  } else {
    for (auto &group_call_participant : group_call_participants->participants) {
      if (group_call_participant.dialog_id == dialog_id) {
        return &group_call_participant;
      }
    }
  }
  return nullptr;
}

void GroupCallManager::on_update_group_call_participants(
    InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::groupCallParticipant>> &&participants,
    int32 version, bool is_recursive) {
  if (G()->close_flag()) {
    return;
  }

  if (!need_group_call_participants(input_group_call_id)) {
    int32 diff = 0;
    int32 video_diff = 0;
    bool need_update = false;
    auto *group_call = get_group_call(input_group_call_id);
    for (auto &group_call_participant : participants) {
      GroupCallParticipant participant(group_call_participant, version);
      if (!participant.is_valid()) {
        LOG(ERROR) << "Receive invalid " << to_string(group_call_participant);
        continue;
      }
      if (participant.is_self && group_call != nullptr && group_call->is_being_left) {
        continue;
      }
      if (participant.joined_date == 0) {
        if (group_call == nullptr || version > group_call->leave_version) {
          diff--;
          video_diff += participant.video_diff;
        }
        remove_recent_group_call_speaker(input_group_call_id, participant.dialog_id);
      } else {
        if (group_call == nullptr || version >= group_call->leave_version) {
          if (participant.is_just_joined) {
            diff++;
          }
          video_diff += participant.video_diff;
        }
        on_participant_speaking_in_group_call(input_group_call_id, participant);
      }
    }

    if (is_group_call_active(group_call) && group_call->version == -1) {
      need_update |= set_group_call_participant_count(group_call, group_call->participant_count + diff,
                                                      "on_update_group_call_participants 1");
      need_update |= set_group_call_unmuted_video_count(group_call, group_call->unmuted_video_count + video_diff,
                                                        "on_update_group_call_participants 2");
    }
    if (need_update) {
      send_update_group_call(group_call, "on_update_group_call_participants 3");
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

  auto *group_call_participants = add_group_call_participants(input_group_call_id, "on_update_group_call_participants");
  if (!is_recursive) {
    vector<DialogId> missing_participants;
    for (auto &group_call_participant : participants) {
      GroupCallParticipant participant(group_call_participant, version);
      if (!participant.is_valid()) {
        continue;
      }
      if (participant.joined_date == 0) {
        if (!participant.is_self) {
          do_delete_group_call_participants(input_group_call_id, {participant.dialog_id.get()}, false, Promise<Unit>());
        }
      } else if (participant.is_min &&
                 get_group_call_participant(group_call_participants, participant.dialog_id) == nullptr) {
        missing_participants.push_back(participant.dialog_id);
      }
    }
    if (!missing_participants.empty()) {
      LOG(INFO) << "Can't apply min updates about " << missing_participants << " in " << input_group_call_id;
      auto input_peers = transform(missing_participants, &DialogManager::get_input_peer_force);
      auto query_promise =
          PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id,
                                  participants = std::move(participants), version](Result<Unit> &&result) mutable {
            send_closure(actor_id, &GroupCallManager::on_update_group_call_participants, input_group_call_id,
                         std::move(participants), version, true);
          });
      td_->create_handler<GetGroupCallParticipantQuery>(std::move(query_promise))
          ->send(input_group_call_id, std::move(input_peers), {});
      return;
    }
  }

  auto &pending_version_updates = group_call_participants->pending_version_updates_[version].updates;
  auto &pending_mute_updates = group_call_participants->pending_mute_updates_[version].updates;
  LOG(INFO) << "Have " << pending_version_updates.size() << " versioned and " << pending_mute_updates.size()
            << " mute pending updates for " << input_group_call_id;
  for (auto &group_call_participant : participants) {
    GroupCallParticipant participant(group_call_participant, version);
    if (!participant.is_valid()) {
      LOG(ERROR) << "Receive invalid " << to_string(group_call_participant);
      continue;
    }
    if (participant.is_min && participant.joined_date != 0) {
      auto old_participant = get_group_call_participant(group_call_participants, participant.dialog_id);
      if (old_participant == nullptr) {
        LOG(ERROR) << "Can't apply min update about " << participant.dialog_id << " in " << input_group_call_id;
        on_receive_group_call_version(input_group_call_id, version, true);
        return;
      }

      participant.update_from(*old_participant);
      CHECK(!participant.is_min);
    }
    auto dialog_id = participant.dialog_id;
    if (dialog_id.get_type() != DialogType::User && participant.joined_date != 0) {
      td_->dialog_manager_->force_create_dialog(dialog_id, "on_update_group_call_participants 4", true);
    }

    bool is_versioned = GroupCallParticipant::is_versioned_update(group_call_participant);
    LOG(INFO) << "Add " << (is_versioned ? "versioned" : "muted") << " update for " << participant;
    if (is_versioned) {
      pending_version_updates[dialog_id] = td::make_unique<GroupCallParticipant>(std::move(participant));
    } else {
      pending_mute_updates[dialog_id] = td::make_unique<GroupCallParticipant>(std::move(participant));
    }
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
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->version == -1 || !group_call->is_active) {
    return false;
  }

  std::pair<int32, int32> diff{0, 0};
  bool is_left = false;
  bool need_rejoin = true;
  auto &pending_version_updates = participants_it->second->pending_version_updates_;
  auto &pending_mute_updates = participants_it->second->pending_mute_updates_;

  auto process_mute_updates = [&] {
    while (!pending_mute_updates.empty()) {
      auto it = pending_mute_updates.begin();
      auto version = it->first;
      if (version > group_call->version) {
        return;
      }

      auto &participants = it->second.updates;
      LOG(INFO) << "Process " << participants.size() << " mute updates for " << input_group_call_id;
      for (auto &participant_it : participants) {
        auto &participant = *participant_it.second;
        on_participant_speaking_in_group_call(input_group_call_id, participant);
        auto mute_diff = process_group_call_participant(input_group_call_id, std::move(participant));
        CHECK(mute_diff.first == 0);
        diff.second += mute_diff.second;
      }
      pending_mute_updates.erase(it);
    }
  };

  bool need_update = false;
  while (!pending_version_updates.empty()) {
    process_mute_updates();

    auto it = pending_version_updates.begin();
    auto version = it->first;
    auto &participants = it->second.updates;
    if (version <= group_call->version) {
      for (auto &participant_it : participants) {
        auto &participant = *participant_it.second;
        on_participant_speaking_in_group_call(input_group_call_id, participant);
        if (participant.is_self || participant.joined_date != 0) {
          auto new_diff = process_group_call_participant(input_group_call_id, std::move(participant));
          diff.first += new_diff.first;
          diff.second += new_diff.second;
        }
      }
      LOG(INFO) << "Ignore already applied updateGroupCallParticipants with version " << version << " in "
                << input_group_call_id << " from " << group_call->dialog_id;
      pending_version_updates.erase(it);
      continue;
    }

    if (version == group_call->version + 1) {
      LOG(INFO) << "Process " << participants.size() << " versioned updates for " << input_group_call_id;
      group_call->version = version;
      for (auto &participant_it : participants) {
        auto &participant = *participant_it.second;
        if (participant.is_self && group_call->is_joined &&
            (participant.joined_date == 0) ==
                is_my_audio_source(input_group_call_id, group_call, participant.audio_source)) {
          LOG(INFO) << "Leaving " << input_group_call_id << " after processing update with joined date "
                    << participant.joined_date;
          is_left = true;
          if (participant.joined_date != 0) {
            need_rejoin = false;
          } else {
            continue;
          }
        }
        auto new_diff = process_group_call_participant(input_group_call_id, std::move(participant));
        diff.first += new_diff.first;
        diff.second += new_diff.second;
      }
      pending_version_updates.erase(it);
    } else {
      // found a gap
      if (!group_call->syncing_participants) {
        LOG(INFO) << "Receive " << participants.size() << " group call participant updates with version " << version
                  << ", but current version is " << group_call->version;
        sync_participants_timeout_.add_timeout_in(group_call->group_call_id.get(), 1.0);
      }
      break;
    }
  }

  process_mute_updates();

  if (!pending_mute_updates.empty()) {
    on_receive_group_call_version(input_group_call_id, pending_mute_updates.begin()->first);
  }

  if (pending_version_updates.empty() && pending_mute_updates.empty()) {
    sync_participants_timeout_.cancel_timeout(group_call->group_call_id.get());
  }

  need_update |= set_group_call_participant_count(group_call, group_call->participant_count + diff.first,
                                                  "process_pending_group_call_participant_updates 1");
  need_update |= set_group_call_unmuted_video_count(group_call, group_call->unmuted_video_count + diff.second,
                                                    "process_pending_group_call_participant_updates 2");
  if (is_left && group_call->is_joined) {
    on_group_call_left_impl(group_call, need_rejoin, "process_pending_group_call_participant_updates 3");
    need_update = true;
  }
  need_update |= try_clear_group_call_participants(input_group_call_id);
  if (need_update) {
    send_update_group_call(group_call, "process_pending_group_call_participant_updates 4");
  }

  return need_update;
}

void GroupCallManager::schedule_group_call_message_deletion(const GroupCall *group_call) {
  auto next_delete_time = group_call->messages.get_next_delete_time();
  if (next_delete_time > 0) {
    delete_group_call_messages_timeout_.set_timeout_at(group_call->group_call_id.get(), next_delete_time);
  } else {
    delete_group_call_messages_timeout_.cancel_timeout(group_call->group_call_id.get());
  }
}

bool GroupCallManager::can_delete_group_call_message(const GroupCall *group_call, DialogId sender_dialog_id) const {
  CHECK(group_call != nullptr);
  if (!group_call->is_inited) {
    LOG(ERROR) << "Have a non-inited group call";
    return false;
  }
  if (!group_call->is_active || !group_call->is_live_story) {
    return false;
  }
  if (sender_dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    return true;
  }
  if (get_group_call_can_delete_messages(group_call)) {
    return true;
  }
  if (group_call->dialog_id.get_type() == DialogType::Channel &&
      td_->chat_manager_->get_channel_status(group_call->dialog_id.get_channel_id()).is_administrator()) {
    return true;
  }
  const auto &created_public_broadcasts = td_->chat_manager_->get_created_public_broadcasts();
  for (auto channel_id : created_public_broadcasts) {
    if (sender_dialog_id == DialogId(channel_id)) {
      return true;
    }
  }
  return false;
}

int32 GroupCallManager::get_group_call_message_delete_in(const GroupCall *group_call,
                                                         const GroupCallMessage &group_call_message,
                                                         bool is_old) const {
  if (group_call_message.is_local()) {
    return 0;
  }
  if (group_call->is_live_story) {
    if (is_old) {
      return clamp(group_call_message.get_date() + 86400 - G()->unix_time(), 1, 86400);
    } else {
      return 86400;
    }
  }
  return static_cast<int32>(clamp(td_->option_manager_->get_option_integer("group_call_message_show_time_max", 30),
                                  static_cast<int64>(1), static_cast<int64>(1000000000)));
}

static void add_top_donors_spent_stars(int64 &total_star_count, vector<MessageReactor> &top_donors,
                                       DialogId sender_dialog_id, bool is_outgoing, int64 star_count) {
  vector<MessageReactor> new_top_donors;
  bool is_found = false;
  for (const auto &donor : top_donors) {
    new_top_donors.push_back(donor);
    if ((donor.is_me() && is_outgoing) || donor.is_same(sender_dialog_id)) {
      is_found = true;
      new_top_donors.back().add_count(static_cast<int32>(star_count), sender_dialog_id, DialogId());
    }
  }
  if (!is_found) {
    new_top_donors.emplace_back(sender_dialog_id, static_cast<int32>(star_count), is_outgoing, false);
  }
  MessageReactor::fix_message_reactors(new_top_donors, false, true);

  total_star_count += star_count;
  top_donors = std::move(new_top_donors);
}

void GroupCallManager::add_group_call_spent_stars(InputGroupCallId input_group_call_id, GroupCall *group_call,
                                                  DialogId sender_dialog_id, bool is_outgoing, bool is_reaction,
                                                  int64 star_count) {
  if (need_group_call_participants(input_group_call_id, group_call)) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "add_group_call_spent_stars");
    if (group_call_participants->are_top_donors_loaded) {
      add_top_donors_spent_stars(group_call_participants->total_star_count, group_call_participants->top_donors,
                                 sender_dialog_id, is_outgoing, star_count);
      send_update_live_story_top_donors(group_call->group_call_id, group_call_participants);
    }
  }
  if (is_reaction) {
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateNewGroupCallPaidReaction>(
                     group_call->group_call_id.get(),
                     get_message_sender_object(td_, sender_dialog_id, "updateNewGroupCallPaidReaction"), star_count));
  }
}

void GroupCallManager::remove_group_call_spent_stars(InputGroupCallId input_group_call_id, GroupCall *group_call,
                                                     int64 star_count) {
  if (need_group_call_participants(input_group_call_id, group_call)) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "remove_group_call_spent_stars");
    if (group_call_participants->are_top_donors_loaded) {
      for (auto &donor : group_call_participants->top_donors) {
        if (donor.is_me()) {
          donor.remove_count(static_cast<int32>(star_count));
          break;
        }
      }
      MessageReactor::fix_message_reactors(group_call_participants->top_donors, false, true);

      group_call_participants->total_star_count -= star_count;
      send_update_live_story_top_donors(group_call->group_call_id, group_call_participants);
    }
  }
  // don't neet to undo updateNewGroupCallPaidReaction
}

int32 GroupCallManager::add_group_call_message(InputGroupCallId input_group_call_id, GroupCall *group_call,
                                               const GroupCallMessage &group_call_message, bool is_old) {
  if (!group_call_message.is_valid()) {
    LOG(INFO) << "Skip invalid " << group_call_message;
    return 0;
  }
  if (group_call_message.is_reaction() && !group_call->is_live_story) {
    LOG(INFO) << "Ignore reaction in " << input_group_call_id;
    return 0;
  }
  LOG(INFO) << "Receive " << (is_old ? "old " : "new ") << group_call_message;
  int32 message_id = 0;
  auto paid_message_star_count = group_call_message.get_paid_message_star_count();
  if (paid_message_star_count >= group_call->paid_message_star_count ||
      (group_call_message.is_from_admin() && !group_call_message.is_reaction())) {
    message_id = group_call->messages.add_message(
        group_call_message, get_group_call_message_delete_in(group_call, group_call_message, is_old));
    if (message_id == 0) {
      LOG(INFO) << "Skip duplicate " << group_call_message;
    } else {
      send_closure(G()->td(), &Td::send_update,
                   td_api::make_object<td_api::updateNewGroupCallMessage>(
                       group_call->group_call_id.get(),
                       group_call_message.get_group_call_message_object(
                           td_, message_id,
                           can_delete_group_call_message(group_call, group_call_message.get_sender_dialog_id()))));
      on_group_call_messages_deleted(group_call, group_call->messages.delete_old_group_call_messages(message_limits_));
    }
  }
  if (!is_old && paid_message_star_count > 0 && group_call->is_live_story) {
    add_group_call_spent_stars(input_group_call_id, group_call, group_call_message.get_sender_dialog_id(),
                               group_call_message.is_local(), group_call_message.is_reaction(),
                               paid_message_star_count);
  }
  return message_id;
}

void GroupCallManager::apply_old_server_messages(InputGroupCallId input_group_call_id, GroupCall *group_call) {
  auto server_message_ids = group_call->messages.get_server_message_ids();
  for (const auto &message : group_call->old_messages) {
    add_group_call_message(input_group_call_id, group_call, message, true);
    server_message_ids.erase(message.get_server_id());
  }
  group_call->old_messages.clear();

  vector<int32> server_ids;
  for (auto &server_id : server_message_ids) {
    server_ids.push_back(server_id);
  }
  on_group_call_messages_deleted(group_call, group_call->messages.delete_server_messages(server_ids));
}

void GroupCallManager::on_group_call_messages_deleted(const GroupCall *group_call, vector<int32> &&message_ids) {
  if (!message_ids.empty()) {
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateGroupCallMessagesDeleted>(group_call->group_call_id.get(),
                                                                             std::move(message_ids)));
  }
  schedule_group_call_message_deletion(group_call);
}

void GroupCallManager::on_group_call_message_sent(InputGroupCallId input_group_call_id, int32 message_id,
                                                  telegram_api::object_ptr<telegram_api::groupCallMessage> &&message) {
  GroupCallMessage group_call_message(td_, std::move(message));
  if (!group_call_message.is_valid()) {
    LOG(ERROR) << "Receive invalid " << group_call_message;
    return;
  }
  auto group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return;
  }
  group_call->messages.on_message_sent(message_id, group_call_message,
                                       get_group_call_message_delete_in(group_call, group_call_message, false));
  schedule_group_call_message_deletion(group_call);
}

void GroupCallManager::on_group_call_message_sending_failed(InputGroupCallId input_group_call_id, int32 message_id,
                                                            int64 paid_message_star_count, const Status &status) {
  auto group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return;
  }
  if (paid_message_star_count > 0 && group_call->is_live_story) {
    remove_group_call_spent_stars(input_group_call_id, group_call, paid_message_star_count);
  }
  if (group_call->messages.has_message(message_id)) {
    send_closure(G()->td(), &Td::send_update,
                 td_api::make_object<td_api::updateGroupCallMessageSendFailed>(
                     group_call->group_call_id.get(), message_id,
                     td_api::make_object<td_api::error>(status.code(), status.message().str())));
  }
  if (group_call->is_live_story && status.code() == 400 && status.message() == CSlice("SEND_AS_PEER_INVALID")) {
    reload_group_call(input_group_call_id, Auto());
  }
}

void GroupCallManager::on_new_group_call_message(InputGroupCallId input_group_call_id,
                                                 telegram_api::object_ptr<telegram_api::groupCallMessage> &&message) {
  if (G()->close_flag()) {
    return;
  }
  auto group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || group_call->is_conference ||
      group_call->call_id != tde2e_api::CallId()) {
    return;
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id,
                                                               message =
                                                                   std::move(message)](Result<Unit> &&result) mutable {
        if (result.is_ok()) {
          send_closure(actor_id, &GroupCallManager::on_new_group_call_message, input_group_call_id, std::move(message));
        }
      }));
    }
    return;
  }

  add_group_call_message(input_group_call_id, group_call, GroupCallMessage(td_, std::move(message)));
}

void GroupCallManager::on_new_encrypted_group_call_message(InputGroupCallId input_group_call_id,
                                                           DialogId sender_dialog_id, string &&encrypted_message) {
  if (G()->close_flag()) {
    return;
  }
  auto group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_conference ||
      group_call->call_id == tde2e_api::CallId() || !sender_dialog_id.is_valid()) {
    return;
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, sender_dialog_id,
                                  encrypted_message = std::move(encrypted_message)](Result<Unit> &&result) mutable {
            if (result.is_ok()) {
              send_closure(actor_id, &GroupCallManager::on_new_encrypted_group_call_message, input_group_call_id,
                           sender_dialog_id, std::move(encrypted_message));
            }
          }));
    }
    return;
  }

  auto r_message = tde2e_api::call_decrypt(group_call->call_id, sender_dialog_id.get(), tde2e_api::CallChannelId(),
                                           encrypted_message);
  if (r_message.is_error()) {
    LOG(INFO) << "Failed to decrypt a message from " << sender_dialog_id;
    return;
  }

  add_group_call_message(input_group_call_id, group_call,
                         GroupCallMessage(td_, sender_dialog_id, std::move(r_message.value())));
}

void GroupCallManager::on_update_group_call_messages_deleted(InputGroupCallId input_group_call_id,
                                                             vector<int32> &&server_ids) {
  if (G()->close_flag()) {
    return;
  }
  auto group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active) {
    return;
  }
  if (!group_call->is_live_story) {
    LOG(ERROR) << "Receive updateDeleteGroupCallMessages in " << input_group_call_id;
    return;
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id,
                                  server_ids = std::move(server_ids)](Result<Unit> &&result) mutable {
            if (result.is_ok()) {
              send_closure(actor_id, &GroupCallManager::on_update_group_call_messages_deleted, input_group_call_id,
                           std::move(server_ids));
            }
          }));
    }
    return;
  }

  on_group_call_messages_deleted(group_call, group_call->messages.delete_server_messages(server_ids));
}

bool GroupCallManager::is_my_audio_source(InputGroupCallId input_group_call_id, const GroupCall *group_call,
                                          int32 audio_source) const {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end()) {
    return audio_source == group_call->audio_source;
  }
  CHECK(it->second != nullptr);

  return audio_source == it->second->audio_source;
}

void GroupCallManager::sync_group_call_participants(InputGroupCallId input_group_call_id) {
  auto *group_call = get_group_call(input_group_call_id);
  if (!need_group_call_participants(input_group_call_id, group_call) || group_call->is_live_story) {
    return;
  }
  CHECK(group_call != nullptr && group_call->is_inited);

  sync_participants_timeout_.cancel_timeout(group_call->group_call_id.get());

  if (group_call->syncing_participants || (group_call->is_conference && !group_call->is_joined)) {
    group_call->need_syncing_participants = true;
    return;
  }
  group_call->syncing_participants = true;
  group_call->need_syncing_participants = false;

  LOG(INFO) << "Force participants synchronization in " << input_group_call_id << " from " << group_call->dialog_id;
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](
                                            Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
    send_closure(actor_id, &GroupCallManager::on_sync_group_call_participants, input_group_call_id, std::move(result));
  });

  td_->create_handler<GetGroupCallQuery>(std::move(promise))->send(input_group_call_id, 100);
}

void GroupCallManager::on_sync_group_call_participants(InputGroupCallId input_group_call_id,
                                                       Result<tl_object_ptr<telegram_api::phone_groupCall>> &&result) {
  if (G()->close_flag() || !need_group_call_participants(input_group_call_id)) {
    return;
  }

  if (result.is_error()) {
    auto *group_call = get_group_call(input_group_call_id);
    CHECK(group_call != nullptr && group_call->is_inited);
    CHECK(group_call->syncing_participants);
    group_call->syncing_participants = false;

    if (!group_call->is_joined) {
      group_call->need_syncing_participants = true;
      return;
    }

    sync_participants_timeout_.add_timeout_in(group_call->group_call_id.get(),
                                              group_call->need_syncing_participants ? 0.0 : 1.0);
    return;
  }

  auto call = result.move_as_ok();
  if (call->call_->get_id() == telegram_api::groupCall::ID) {
    auto *group_call = static_cast<const telegram_api::groupCall *>(call->call_.get());
    auto participants = make_tl_object<telegram_api::phone_groupParticipants>(
        group_call->participants_count_, std::move(call->participants_), std::move(call->participants_next_offset_),
        std::move(call->chats_), std::move(call->users_), group_call->version_);
    on_get_group_call_participants(input_group_call_id, std::move(participants), true, string());
  }

  if (update_group_call(call->call_, DialogId(), false) != input_group_call_id) {
    LOG(ERROR) << "Expected " << input_group_call_id << ", but received " << to_string(result.ok());
  }
}

GroupCallParticipantOrder GroupCallManager::get_real_participant_order(bool my_can_self_unmute,
                                                                       const GroupCallParticipant &participant,
                                                                       const GroupCallParticipants *participants) {
  auto real_order = participant.get_real_order(my_can_self_unmute, participants->joined_date_asc);
  if (real_order >= participants->min_order) {
    return real_order;
  }
  if (participant.is_self) {
    return participants->min_order;
  }
  if (real_order.is_valid()) {
    LOG(DEBUG) << "Order " << real_order << " of " << participant.dialog_id << " is less than last known order "
               << participants->min_order;
  }
  return GroupCallParticipantOrder();
}

void GroupCallManager::process_group_call_participants(
    InputGroupCallId input_group_call_id, vector<tl_object_ptr<telegram_api::groupCallParticipant>> &&participants,
    int32 version, const string &offset, bool is_load, bool is_sync) {
  // if receive exactly one participant, then the current user is the only participant
  // there are no reasons to process it independently
  if (offset.empty() && is_load && participants.size() >= 2 && participants[0]->self_) {
    GroupCallParticipant participant(participants[0], version);
    if (participant.is_valid()) {
      process_my_group_call_participant(input_group_call_id, std::move(participant));
    }
    participants.erase(participants.begin());
  }
  if (!need_group_call_participants(input_group_call_id)) {
    for (auto &group_call_participant : participants) {
      GroupCallParticipant participant(group_call_participant, version);
      if (!participant.is_valid()) {
        LOG(ERROR) << "Receive invalid " << to_string(group_call_participant);
        continue;
      }
      if (participant.dialog_id.get_type() != DialogType::User) {
        td_->dialog_manager_->force_create_dialog(participant.dialog_id, "process_group_call_participants", true);
      }

      on_participant_speaking_in_group_call(input_group_call_id, participant);
    }
    return;
  }

  FlatHashSet<DialogId, DialogIdHash> old_participant_dialog_ids;
  if (is_sync) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "process_group_call_participants");
    for (auto &participant : group_call_participants->participants) {
      CHECK(participant.dialog_id.is_valid());
      old_participant_dialog_ids.insert(participant.dialog_id);
    }
  }

  auto min_order = GroupCallParticipantOrder::max();
  DialogId debug_min_order_dialog_id;
  bool my_can_self_unmute = get_group_call_can_self_unmute(input_group_call_id);
  bool joined_date_asc = get_group_call_joined_date_asc(input_group_call_id);
  for (auto &group_call_participant : participants) {
    GroupCallParticipant participant(group_call_participant, version);
    if (!participant.is_valid()) {
      LOG(ERROR) << "Receive invalid " << to_string(group_call_participant);
      continue;
    }
    if (participant.is_min) {
      LOG(ERROR) << "Receive unexpected min " << to_string(group_call_participant);
      continue;
    }
    if (participant.dialog_id.get_type() != DialogType::User) {
      td_->dialog_manager_->force_create_dialog(participant.dialog_id, "process_group_call_participants", true);
    }

    if (is_load) {
      auto real_order = participant.get_server_order(my_can_self_unmute, joined_date_asc);
      if (real_order > min_order) {
        LOG(ERROR) << "Receive group call participant " << participant.dialog_id << " with order " << real_order
                   << " after group call participant " << debug_min_order_dialog_id << " with order " << min_order;
      } else {
        min_order = real_order;
        debug_min_order_dialog_id = participant.dialog_id;
      }
    }
    if (is_sync) {
      old_participant_dialog_ids.erase(participant.dialog_id);
    }
    process_group_call_participant(input_group_call_id, std::move(participant));
  }
  if (is_load && participants.empty() && !joined_date_asc) {
    // If loaded 0 participants and new participants are added to the beginning of the list,
    // then the end of the list was reached.
    // Set min_order to the minimum possible value to send updates about all participants with order less than
    // the current min_order. There can be such participants if the last loaded participant had a fake active_date.
    min_order = GroupCallParticipantOrder::min();
  }
  if (is_sync) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "process_group_call_participants");
    auto &group_participants = group_call_participants->participants;
    for (auto participant_it = group_participants.begin(); participant_it != group_participants.end();) {
      auto &participant = *participant_it;
      if (old_participant_dialog_ids.count(participant.dialog_id) == 0) {
        // successfully synced old user
        ++participant_it;
        continue;
      }

      if (participant.is_self) {
        if (participant.order != min_order) {
          participant.order = min_order;
          send_update_group_call_participant(input_group_call_id, participant, "process_group_call_participants self");
        }
        ++participant_it;
        continue;
      }

      // not synced user and not self, needs to be deleted
      if (participant.order.is_valid()) {
        CHECK(participant.order >= group_call_participants->min_order);
        participant.order = GroupCallParticipantOrder();
        send_update_group_call_participant(input_group_call_id, participant, "process_group_call_participants sync");
      }
      on_remove_group_call_participant(input_group_call_id, participant.dialog_id);
      group_call_participants->local_unmuted_video_count -= participant.get_has_video();
      participant_it = group_participants.erase(participant_it);
    }
    if (group_call_participants->min_order < min_order) {
      // if previously known more users, adjust min_order
      LOG(INFO) << "Decrease min_order from " << group_call_participants->min_order << " to " << min_order << " in "
                << input_group_call_id;
      group_call_participants->min_order = min_order;
      update_group_call_participants_order(input_group_call_id, my_can_self_unmute, group_call_participants,
                                           "decrease min_order");
    }
  }
  if (is_load) {
    auto *group_call_participants = add_group_call_participants(input_group_call_id, "process_group_call_participants");
    if (group_call_participants->min_order > min_order) {
      LOG(INFO) << "Increase min_order from " << group_call_participants->min_order << " to " << min_order << " in "
                << input_group_call_id;
      group_call_participants->min_order = min_order;
      update_group_call_participants_order(input_group_call_id, my_can_self_unmute, group_call_participants,
                                           "increase min_order");
    }
  }
}

bool GroupCallManager::update_group_call_participant_can_be_muted(bool can_manage,
                                                                  const GroupCallParticipants *participants,
                                                                  GroupCallParticipant &participant,
                                                                  bool force_is_admin) {
  bool is_admin = force_is_admin || td::contains(participants->administrator_dialog_ids, participant.dialog_id);
  return participant.update_can_be_muted(can_manage, is_admin);
}

void GroupCallManager::update_group_call_participants_can_be_muted(InputGroupCallId input_group_call_id,
                                                                   bool can_manage, GroupCallParticipants *participants,
                                                                   bool force_is_admin) {
  CHECK(participants != nullptr);
  LOG(INFO) << "Update group call participants can_be_muted in " << input_group_call_id;
  for (auto &participant : participants->participants) {
    if (update_group_call_participant_can_be_muted(can_manage, participants, participant, force_is_admin) &&
        participant.order.is_valid()) {
      send_update_group_call_participant(input_group_call_id, participant,
                                         "update_group_call_participants_can_be_muted");
    }
  }
}

void GroupCallManager::update_group_call_participants_order(InputGroupCallId input_group_call_id,
                                                            bool my_can_self_unmute,
                                                            GroupCallParticipants *participants, const char *source) {
  for (auto &participant : participants->participants) {
    auto new_order = get_real_participant_order(my_can_self_unmute, participant, participants);
    if (new_order != participant.order) {
      participant.order = new_order;
      send_update_group_call_participant(input_group_call_id, participant, "process_group_call_participants load");
    }
  }

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  update_group_call_participant_order_timeout_.set_timeout_in(group_call->group_call_id.get(),
                                                              UPDATE_GROUP_CALL_PARTICIPANT_ORDER_TIMEOUT);
}

void GroupCallManager::process_my_group_call_participant(InputGroupCallId input_group_call_id,
                                                         GroupCallParticipant &&participant) {
  CHECK(participant.is_valid());
  CHECK(participant.is_self);
  if (!need_group_call_participants(input_group_call_id)) {
    return;
  }
  auto my_participant = get_group_call_participant(input_group_call_id, td_->dialog_manager_->get_my_dialog_id(),
                                                   "process_my_group_call_participant");
  if (my_participant == nullptr || my_participant->is_fake || my_participant->joined_date < participant.joined_date ||
      (my_participant->joined_date <= participant.joined_date &&
       my_participant->audio_source != participant.audio_source)) {
    process_group_call_participant(input_group_call_id, std::move(participant));
  }
}

std::pair<int32, int32> GroupCallManager::process_group_call_participant(InputGroupCallId input_group_call_id,
                                                                         GroupCallParticipant &&participant) {
  if (!participant.is_valid()) {
    LOG(ERROR) << "Receive invalid " << participant;
    return {0, 0};
  }
  if (!need_group_call_participants(input_group_call_id)) {
    return {0, 0};
  }

  LOG(INFO) << "Process " << participant << " in " << input_group_call_id;

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (participant.is_self) {
    auto can_self_unmute = group_call->is_active && !participant.get_is_muted_by_admin();
    if (can_self_unmute != group_call->can_self_unmute) {
      group_call->can_self_unmute = can_self_unmute;
      send_update_group_call(group_call, "process_group_call_participant 1");
      sync_group_call_participants(input_group_call_id);  // participant order is different for administrators
    }
  }

  bool my_can_self_unmute = get_group_call_can_self_unmute(input_group_call_id);
  bool can_manage = can_manage_group_call(input_group_call_id);
  auto *participants = add_group_call_participants(input_group_call_id, "process_group_call_participant");
  for (size_t i = 0; i < participants->participants.size(); i++) {
    auto &old_participant = participants->participants[i];
    if (old_participant.dialog_id == participant.dialog_id || (old_participant.is_self && participant.is_self)) {
      if (participant.joined_date == 0) {
        LOG(INFO) << "Remove " << old_participant;
        if (old_participant.order.is_valid()) {
          send_update_group_call_participant(input_group_call_id, participant, "process_group_call_participant remove");
        }
        on_remove_group_call_participant(input_group_call_id, old_participant.dialog_id);
        remove_recent_group_call_speaker(input_group_call_id, old_participant.dialog_id);
        int32 unmuted_video_diff = -old_participant.get_has_video();
        participants->local_unmuted_video_count += unmuted_video_diff;
        participants->participants.erase(participants->participants.begin() + i);
        return {-1, unmuted_video_diff};
      }

      if (old_participant.version > participant.version) {
        LOG(INFO) << "Ignore outdated update of " << old_participant.dialog_id;
        return {0, 0};
      }

      if (old_participant.dialog_id != participant.dialog_id) {
        on_remove_group_call_participant(input_group_call_id, old_participant.dialog_id);
        on_add_group_call_participant(input_group_call_id, participant.dialog_id);
      }

      participant.update_from(old_participant);

      participant.is_just_joined = false;
      participant.order = get_real_participant_order(my_can_self_unmute, participant, participants);
      update_group_call_participant_can_be_muted(can_manage, participants, participant,
                                                 get_group_call_is_creator(group_call));

      LOG(INFO) << "Edit " << old_participant << " to " << participant;
      if (old_participant != participant && (old_participant.order.is_valid() || participant.order.is_valid())) {
        send_update_group_call_participant(input_group_call_id, participant, "process_group_call_participant edit");
        if (old_participant.dialog_id != participant.dialog_id) {
          // delete old self-participant; shouldn't affect correct apps
          old_participant.order = GroupCallParticipantOrder();
          send_update_group_call_participant(input_group_call_id, old_participant,
                                             "process_group_call_participant edit self");
        }
      }
      on_participant_speaking_in_group_call(input_group_call_id, participant);
      int32 unmuted_video_diff = participant.get_has_video() - old_participant.get_has_video();
      participants->local_unmuted_video_count += unmuted_video_diff;
      old_participant = std::move(participant);
      return {0, unmuted_video_diff};
    }
  }

  if (participant.joined_date == 0) {
    LOG(INFO) << "Remove unknown " << participant;
    remove_recent_group_call_speaker(input_group_call_id, participant.dialog_id);
    return {-1, participant.video_diff};
  }

  CHECK(!participant.is_min);
  int diff = participant.is_just_joined ? 1 : 0;
  participant.order = get_real_participant_order(my_can_self_unmute, participant, participants);
  if (participant.is_just_joined) {
    LOG(INFO) << "Add new " << participant;
  } else {
    LOG(INFO) << "Receive new " << participant;
  }
  participant.is_just_joined = false;
  participants->local_unmuted_video_count += participant.get_has_video();
  update_group_call_participant_can_be_muted(can_manage, participants, participant,
                                             get_group_call_is_creator(group_call));
  participants->participants.push_back(std::move(participant));
  if (participants->participants.back().order.is_valid()) {
    send_update_group_call_participant(input_group_call_id, participants->participants.back(),
                                       "process_group_call_participant add");
  } else if (group_call->loaded_all_participants) {
    group_call->loaded_all_participants = false;
    send_update_group_call(group_call, "process_group_call_participant 2");
  }
  on_add_group_call_participant(input_group_call_id, participants->participants.back().dialog_id);
  on_participant_speaking_in_group_call(input_group_call_id, participants->participants.back());
  return {diff, participants->participants.back().video_diff};
}

void GroupCallManager::on_add_group_call_participant(InputGroupCallId input_group_call_id,
                                                     DialogId participant_dialog_id) {
  auto &participants = participant_id_to_group_call_id_[participant_dialog_id];
  CHECK(!td::contains(participants, input_group_call_id));
  participants.push_back(input_group_call_id);
}

void GroupCallManager::on_remove_group_call_participant(InputGroupCallId input_group_call_id,
                                                        DialogId participant_dialog_id) {
  auto it = participant_id_to_group_call_id_.find(participant_dialog_id);
  CHECK(it != participant_id_to_group_call_id_.end());
  bool is_removed = td::remove(it->second, input_group_call_id);
  CHECK(is_removed);
  if (it->second.empty()) {
    participant_id_to_group_call_id_.erase(it);
  }
}

void GroupCallManager::on_update_dialog_about(DialogId dialog_id, const string &about, bool from_server) {
  auto it = participant_id_to_group_call_id_.find(dialog_id);
  if (it == participant_id_to_group_call_id_.end()) {
    return;
  }
  CHECK(!it->second.empty());

  for (const auto &input_group_call_id : it->second) {
    auto participant = get_group_call_participant(input_group_call_id, dialog_id, "on_update_dialog_about");
    CHECK(participant != nullptr);
    if ((from_server || participant->is_fake) && participant->about != about) {
      participant->about = about;
      if (participant->order.is_valid()) {
        send_update_group_call_participant(input_group_call_id, *participant, "on_update_dialog_about");
      }
    }
  }
}

int32 GroupCallManager::cancel_join_group_call_request(InputGroupCallId input_group_call_id, GroupCall *group_call) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end()) {
    CHECK(group_call == nullptr || !group_call->is_being_joined || group_call->is_conference);
    return 0;
  }
  CHECK(group_call != nullptr);
  CHECK(group_call->is_being_joined || group_call->is_conference);
  group_call->is_being_joined = false;

  CHECK(it->second != nullptr);
  if (!it->second->query_ref.empty()) {
    cancel_query(it->second->query_ref);
  }
  tde2e_api::key_destroy(it->second->private_key_id);
  tde2e_api::key_destroy(it->second->public_key_id);
  it->second->promise.set_error(200, "Canceled");
  auto audio_source = it->second->audio_source;
  pending_join_requests_.erase(it);
  return audio_source;
}

int32 GroupCallManager::cancel_join_group_call_presentation_request(InputGroupCallId input_group_call_id) {
  auto it = pending_join_presentation_requests_.find(input_group_call_id);
  if (it == pending_join_presentation_requests_.end()) {
    return 0;
  }

  CHECK(it->second != nullptr);
  if (!it->second->query_ref.empty()) {
    cancel_query(it->second->query_ref);
  }
  it->second->promise.set_error(200, "Canceled");
  auto audio_source = it->second->audio_source;
  pending_join_presentation_requests_.erase(it);
  return audio_source;
}

void GroupCallManager::get_group_call_streams(GroupCallId group_call_id,
                                              Promise<td_api::object_ptr<td_api::groupCallStreams>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::get_group_call_streams, group_call_id,
                                       std::move(promise));
                        }
                      }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || !group_call->stream_dc_id.is_exact()) {
    return promise.set_error(400, "Group call can't be streamed");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(result.move_as_error());
            } else {
              send_closure(actor_id, &GroupCallManager::get_group_call_streams, group_call_id, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, audio_source = group_call->audio_source,
       promise = std::move(promise)](Result<td_api::object_ptr<td_api::groupCallStreams>> &&result) mutable {
        send_closure(actor_id, &GroupCallManager::finish_get_group_call_streams, input_group_call_id, audio_source,
                     std::move(result), std::move(promise));
      });
  td_->create_handler<GetGroupCallStreamChannelsQuery>(std::move(query_promise))
      ->send(input_group_call_id, group_call->stream_dc_id);
}

void GroupCallManager::finish_get_group_call_streams(InputGroupCallId input_group_call_id, int32 audio_source,
                                                     Result<td_api::object_ptr<td_api::groupCallStreams>> &&result,
                                                     Promise<td_api::object_ptr<td_api::groupCallStreams>> &&promise) {
  if (!G()->close_flag() && result.is_error()) {
    auto message = result.error().message();
    if (message == "GROUPCALL_JOIN_MISSING" || message == "GROUPCALL_FORBIDDEN" || message == "GROUPCALL_INVALID") {
      on_group_call_left(input_group_call_id, audio_source, message == "GROUPCALL_JOIN_MISSING");
    }
  }

  promise.set_result(std::move(result));
}

void GroupCallManager::get_group_call_stream_segment(GroupCallId group_call_id, int64 time_offset, int32 scale,
                                                     int32 channel_id,
                                                     td_api::object_ptr<td_api::GroupCallVideoQuality> quality,
                                                     Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, time_offset, scale, channel_id,
                                              quality = std::move(quality), promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::get_group_call_stream_segment, group_call_id,
                                       time_offset, scale, channel_id, std::move(quality), std::move(promise));
                        }
                      }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || !group_call->stream_dc_id.is_exact()) {
    return promise.set_error(400, "Group call can't be streamed");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, time_offset, scale, channel_id, quality = std::move(quality),
           promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(result.move_as_error());
            } else {
              send_closure(actor_id, &GroupCallManager::get_group_call_stream_segment, group_call_id, time_offset,
                           scale, channel_id, std::move(quality), std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  int32 video_quality = 0;
  if (quality != nullptr) {
    switch (quality->get_id()) {
      case td_api::groupCallVideoQualityThumbnail::ID:
        video_quality = 0;
        break;
      case td_api::groupCallVideoQualityMedium::ID:
        video_quality = 1;
        break;
      case td_api::groupCallVideoQualityFull::ID:
        video_quality = 2;
        break;
      default:
        UNREACHABLE();
    }
  }

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, audio_source = group_call->audio_source,
                              promise = std::move(promise)](Result<string> &&result) mutable {
        send_closure(actor_id, &GroupCallManager::finish_get_group_call_stream_segment, input_group_call_id,
                     audio_source, std::move(result), std::move(promise));
      });
  td_->create_handler<GetGroupCallStreamQuery>(std::move(query_promise))
      ->send(input_group_call_id, group_call->stream_dc_id, time_offset, scale, channel_id, video_quality);
}

void GroupCallManager::finish_get_group_call_stream_segment(InputGroupCallId input_group_call_id, int32 audio_source,
                                                            Result<string> &&result, Promise<string> &&promise) {
  if (!G()->close_flag()) {
    if (result.is_ok()) {
      auto *group_call = get_group_call(input_group_call_id);
      CHECK(group_call != nullptr);
      if (group_call->is_inited && check_group_call_is_joined_timeout_.has_timeout(group_call->group_call_id.get())) {
        check_group_call_is_joined_timeout_.set_timeout_in(group_call->group_call_id.get(),
                                                           CHECK_GROUP_CALL_IS_JOINED_TIMEOUT);
      }
    } else {
      auto message = result.error().message();
      if (message == "GROUPCALL_JOIN_MISSING" || message == "GROUPCALL_FORBIDDEN" || message == "GROUPCALL_INVALID") {
        on_group_call_left(input_group_call_id, audio_source, message == "GROUPCALL_JOIN_MISSING");
      }
    }
  }

  promise.set_result(std::move(result));
}

void GroupCallManager::start_scheduled_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::start_scheduled_group_call, group_call_id,
                                       std::move(promise));
                        }
                      }));
    return;
  }
  if (group_call->is_conference || group_call->is_live_story) {
    return promise.set_error(400, "The group call isn't scheduled");
  }
  if (!group_call->can_be_managed) {
    return promise.set_error(400, "Not enough rights to start the group call");
  }
  if (!group_call->is_active) {
    return promise.set_error(400, "Group call already ended");
  }
  if (group_call->scheduled_start_date == 0) {
    return promise.set_value(Unit());
  }

  td_->create_handler<StartScheduledGroupCallQuery>(std::move(promise))->send(input_group_call_id);
}

void GroupCallManager::join_group_call(td_api::object_ptr<td_api::InputGroupCall> &&api_input_group_call,
                                       td_api::object_ptr<td_api::groupCallJoinParameters> &&join_parameters,
                                       Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call,
                     InputGroupCall::get_input_group_call(td_, std::move(api_input_group_call)));
  TRY_RESULT_PROMISE(promise, parameters,
                     GroupCallJoinParameters::get_group_call_join_parameters(std::move(join_parameters), false));

  try_join_group_call(std::move(input_group_call), std::move(parameters), std::move(promise));
}

void GroupCallManager::try_join_group_call(InputGroupCall &&input_group_call, GroupCallJoinParameters &&join_parameters,
                                           Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call, join_parameters = std::move(join_parameters),
       promise = std::move(promise)](Result<telegram_api::object_ptr<telegram_api::Updates>> &&r_updates) mutable {
        if (r_updates.is_error()) {
          return promise.set_error(r_updates.move_as_error());
        }
        send_closure(actor_id, &GroupCallManager::do_join_group_call, std::move(input_group_call),
                     std::move(join_parameters), r_updates.move_as_ok(), std::move(promise));
      });
  td_->create_handler<GetGroupCallLastBlockQuery>(std::move(query_promise))->send(input_group_call);
}

void GroupCallManager::do_join_group_call(InputGroupCall &&input_group_call, GroupCallJoinParameters &&join_parameters,
                                          telegram_api::object_ptr<telegram_api::Updates> updates,
                                          Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  InputGroupCallId input_group_call_id;
  auto input_group_call_it = real_input_group_call_ids_.find(input_group_call);
  if (input_group_call_it != real_input_group_call_ids_.end()) {
    input_group_call_id = input_group_call_it->second;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call != nullptr) {
    if (group_call->is_inited && !group_call->is_active) {
      return promise.set_error(400, "Stream is finished");
    }
    if (group_call->is_inited && !group_call->is_conference) {
      // shouldn't happen
      return promise.set_error(400, "The group call must be joined using joinVideoChat");
    }
    if (group_call->is_joined) {
      return promise.set_error(400, "The group call is already joined");
    }
  }

  if (updates->get_id() != telegram_api::updates::ID) {
    return promise.set_error(500, "Receive invalid block");
  }
  auto &blocks = static_cast<telegram_api::updates *>(updates.get())->updates_;
  if (blocks.size() != 1u || blocks[0]->get_id() != telegram_api::updateGroupCallChainBlocks::ID) {
    return promise.set_error(500, "Receive invalid block updates");
  }
  auto update = telegram_api::move_object_as<telegram_api::updateGroupCallChainBlocks>(blocks[0]);
  if (update->blocks_.size() > 1u) {
    return promise.set_error(500, "Receive invalid blocks");
  }
  real_input_group_call_ids_[input_group_call] = InputGroupCallId(update->call_);

  auto r_private_key_id = tde2e_api::key_generate_temporary_private_key();
  if (r_private_key_id.is_error()) {
    return promise.set_error(400, "Failed to generate encryption key");
  }
  auto private_key_id = tde2e_move_as_ok(r_private_key_id);

  auto public_key_string = tde2e_move_as_ok(tde2e_api::key_to_public_key(private_key_id));
  auto public_key_id = tde2e_move_as_ok(tde2e_api::key_from_public_key(public_key_string));

  tde2e_api::CallParticipant participant;
  participant.user_id = td_->user_manager_->get_my_id().get();
  participant.public_key_id = public_key_id;
  participant.permissions = 3;

  string block;
  if (update->blocks_.empty()) {
    // create new blockchain
    tde2e_api::CallState state;
    state.participants.push_back(std::move(participant));

    block = tde2e_move_as_ok(tde2e_api::call_create_zero_block(private_key_id, state));
  } else {
    auto last_block = update->blocks_[0].as_slice();
    auto r_block =
        tde2e_api::call_create_self_add_block(private_key_id, {last_block.begin(), last_block.size()}, participant);
    if (r_block.is_error()) {
      tde2e_api::key_destroy(private_key_id);
      tde2e_api::key_destroy(public_key_id);
      return promise.set_error(500, "Receive invalid previous block");
    }
    block = tde2e_move_as_ok(std::move(r_block));
  }

  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call, join_parameters, private_key_id, public_key_id,
       promise = std::move(promise)](Result<telegram_api::object_ptr<telegram_api::Updates>> &&r_updates) mutable {
        send_closure(actor_id, &GroupCallManager::on_join_group_call, std::move(input_group_call),
                     std::move(join_parameters), private_key_id, public_key_id, std::move(r_updates),
                     std::move(promise));
      });
  td_->create_handler<JoinGroupCallQuery>(std::move(query_promise))
      ->send(input_group_call, join_parameters, public_key_string, BufferSlice(block));
}

void GroupCallManager::on_join_group_call(InputGroupCall &&input_group_call, GroupCallJoinParameters &&join_parameters,
                                          const tde2e_api::PrivateKeyId &private_key_id,
                                          const tde2e_api::PublicKeyId &public_key_id,
                                          Result<telegram_api::object_ptr<telegram_api::Updates>> &&r_updates,
                                          Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  InputGroupCallId input_group_call_id;
  if (r_updates.is_ok()) {
    input_group_call_id = td_->updates_manager_->get_update_new_group_call_id(r_updates.ok().get());
    if (!input_group_call_id.is_valid()) {
      r_updates = Status::Error(500, "Receive wrong response");
    } else {
      real_input_group_call_ids_[input_group_call] = input_group_call_id;
    }
  }
  if (pending_join_requests_.count(input_group_call_id) != 0) {
    r_updates = Status::Error(500, "Join conference call");
  }
  if (r_updates.is_error()) {
    auto r_ok = tde2e_api::key_destroy(private_key_id);
    CHECK(r_ok.is_ok());
    r_ok = tde2e_api::key_destroy(public_key_id);
    CHECK(r_ok.is_ok());
    if (begins_with(r_updates.error().message(), "CONF_WRITE_CHAIN_INVALID")) {
      LOG(INFO) << "Restart join of " << input_group_call << ", because group call state has changed";
      return try_join_group_call(std::move(input_group_call), std::move(join_parameters), std::move(promise));
    }
    return promise.set_error(r_updates.move_as_error());
  }

  process_join_group_call_response(input_group_call_id, true, join_parameters.audio_source_, private_key_id,
                                   public_key_id, r_updates.move_as_ok(), std::move(promise));
}

void GroupCallManager::process_join_group_call_response(InputGroupCallId input_group_call_id, bool is_join,
                                                        int32 audio_source,
                                                        const tde2e_api::PrivateKeyId &private_key_id,
                                                        const tde2e_api::PublicKeyId &public_key_id,
                                                        telegram_api::object_ptr<telegram_api::Updates> &&updates,
                                                        Promise<td_api::object_ptr<td_api::groupCallInfo>> &&promise) {
  if (is_join) {
    auto &request = pending_join_requests_[input_group_call_id];
    request = make_unique<PendingJoinRequest>();
    request->generation = 1;
    request->audio_source = audio_source;
    request->as_dialog_id = td_->dialog_manager_->get_my_dialog_id();
    request->private_key_id = private_key_id;
    request->public_key_id = public_key_id;
    request->promise =
        PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](Result<string> r_payload) mutable {
          if (r_payload.is_ok()) {
            send_closure(actor_id, &GroupCallManager::on_get_group_call_join_payload, input_group_call_id,
                         r_payload.move_as_ok());
          }
        });
  }

  td_->updates_manager_->on_get_updates(
      std::move(updates), PromiseCreator::lambda([actor_id = actor_id(this), is_join, promise = std::move(promise),
                                                  input_group_call_id](Unit) mutable {
        send_closure(actor_id, &GroupCallManager::on_create_group_call_finished, input_group_call_id, is_join,
                     std::move(promise));
      }));
}

void GroupCallManager::join_video_chat(GroupCallId group_call_id, DialogId as_dialog_id,
                                       td_api::object_ptr<td_api::groupCallJoinParameters> &&join_parameters,
                                       const string &invite_hash, Promise<string> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  TRY_RESULT_PROMISE(promise, parameters,
                     GroupCallJoinParameters::get_group_call_join_parameters(std::move(join_parameters), false));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (group_call->is_inited && !group_call->is_active) {
    return promise.set_error(400, "Video chat is finished");
  }
  if (group_call->is_inited && group_call->is_conference) {
    return promise.set_error(400, "The group call must be joined using joinGroupCall");
  }
  bool need_update = false;
  bool old_is_joined = get_group_call_is_joined(group_call);
  bool is_rejoin = group_call->need_rejoin;
  if (group_call->need_rejoin) {
    group_call->need_rejoin = false;
    need_update = true;
  }

  cancel_join_group_call_request(input_group_call_id, group_call);

  bool have_as_dialog_id = true;
  {
    auto my_dialog_id = td_->dialog_manager_->get_my_dialog_id();
    if (!as_dialog_id.is_valid()) {
      as_dialog_id = my_dialog_id;
    }
    auto dialog_type = as_dialog_id.get_type();
    if (dialog_type == DialogType::User) {
      if (as_dialog_id != my_dialog_id) {
        return promise.set_error(400, "Can't join video chat as another user");
      }
      if (!td_->user_manager_->have_user_force(as_dialog_id.get_user_id(), "join_video_chat 1")) {
        have_as_dialog_id = false;
      }
    } else {
      if (!td_->dialog_manager_->have_dialog_force(as_dialog_id, "join_video_chat 2")) {
        return promise.set_error(400, "Join as chat not found");
      }
    }
    if (!td_->dialog_manager_->have_input_peer(as_dialog_id, false, AccessRights::Read)) {
      return promise.set_error(400, "Can't access the join as participant");
    }
    if (as_dialog_id != my_dialog_id && group_call->is_live_story) {
      return promise.set_error(400, "Can't join live streams as another chat");
    }
  }

  group_call->is_being_left = false;
  group_call->is_being_joined = true;

  auto generation = ++join_group_request_generation_;
  auto &request = pending_join_requests_[input_group_call_id];
  request = make_unique<PendingJoinRequest>();
  request->generation = generation;
  request->audio_source = parameters.audio_source_;
  request->as_dialog_id = as_dialog_id;
  request->promise = std::move(promise);

  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), generation, input_group_call_id](Result<Unit> &&result) {
        CHECK(result.is_error());
        send_closure(actor_id, &GroupCallManager::finish_join_group_call, input_group_call_id, generation,
                     result.move_as_error());
      });
  request->query_ref = td_->create_handler<JoinVideoChatQuery>(std::move(query_promise))
                           ->send(input_group_call_id, as_dialog_id, parameters, invite_hash, generation);

  if (group_call->dialog_id.is_valid()) {
    td_->messages_manager_->on_update_dialog_default_join_group_call_as_dialog_id(group_call->dialog_id, as_dialog_id,
                                                                                  true);
  } else {
    if (as_dialog_id.get_type() != DialogType::User) {
      td_->dialog_manager_->force_create_dialog(as_dialog_id, "join_video_chat 3");
    }
  }
  if (group_call->is_inited && have_as_dialog_id) {
    GroupCallParticipant participant;
    participant.is_self = true;
    participant.dialog_id = as_dialog_id;
    participant.about = td_->dialog_manager_->get_dialog_about(participant.dialog_id);
    participant.audio_source = parameters.audio_source_;
    participant.joined_date = G()->unix_time();
    // if can_self_unmute has never been inited from self-participant,
    // it contains reasonable default "!call.mute_new_participants || call.can_be_managed || call.is_creator"
    participant.server_is_muted_by_admin = !group_call->can_self_unmute && !can_manage_group_call(group_call);
    participant.server_is_muted_by_themselves = parameters.is_muted_ && !participant.server_is_muted_by_admin;
    participant.is_just_joined = !is_rejoin;
    participant.video_diff = get_group_call_can_enable_video(group_call) && parameters.is_my_video_enabled_;
    participant.is_fake = true;
    auto diff = process_group_call_participant(input_group_call_id, std::move(participant));
    if (diff.first != 0) {
      CHECK(diff.first == 1);
      need_update |= set_group_call_participant_count(group_call, group_call->participant_count + diff.first,
                                                      "join_video_chat 4", true);
    }
    if (diff.second != 0) {
      CHECK(diff.second == 1);
      need_update |= set_group_call_unmuted_video_count(group_call, group_call->unmuted_video_count + diff.second,
                                                        "join_video_chat 5");
    }
  }
  if (group_call->is_my_video_enabled != parameters.is_my_video_enabled_) {
    group_call->is_my_video_enabled = parameters.is_my_video_enabled_;
    if (!group_call->is_my_video_enabled) {
      group_call->is_my_video_paused = false;
    }
    need_update = true;
  }
  if (old_is_joined != get_group_call_is_joined(group_call)) {
    need_update = true;
  }
  if (group_call->is_inited && need_update) {
    send_update_group_call(group_call, "join_video_chat 6");
  }

  try_load_group_call_administrators(input_group_call_id, group_call->dialog_id);
}

void GroupCallManager::join_live_story(GroupCallId group_call_id,
                                       td_api::object_ptr<td_api::groupCallJoinParameters> &&join_parameters,
                                       Promise<string> &&promise) {
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, join_parameters = std::move(join_parameters),
                              promise = std::move(promise)](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
        } else {
          send_closure_later(actor_id, &GroupCallManager::join_video_chat, group_call_id, DialogId(),
                             std::move(join_parameters), string(), std::move(promise));
        }
      });
  td_->chat_manager_->load_created_public_broadcasts(std::move(query_promise));
}

void GroupCallManager::encrypt_group_call_data(GroupCallId group_call_id,
                                               td_api::object_ptr<td_api::GroupCallDataChannel> &&data_channel,
                                               string &&data, int32 unencrypted_prefix_size,
                                               Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_conference || group_call->call_id == tde2e_api::CallId()) {
    return promise.set_error(400, "Group call doesn't support encryption");
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, data_channel = std::move(data_channel), data = std::move(data),
           unencrypted_prefix_size, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::encrypt_group_call_data, group_call_id, std::move(data_channel),
                           std::move(data), unencrypted_prefix_size, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  tde2e_api::CallChannelId channel_id{};
  if (data_channel != nullptr && data_channel->get_id() == td_api::groupCallDataChannelScreenSharing::ID) {
    channel_id = 1;
  }
  auto r_data = tde2e_api::call_encrypt(group_call->call_id, channel_id, data, unencrypted_prefix_size);
  if (r_data.is_error()) {
    return promise.set_error(400, r_data.error().message);
  }
  promise.set_value(std::move(r_data.value()));
}

void GroupCallManager::decrypt_group_call_data(GroupCallId group_call_id, DialogId participant_dialog_id,
                                               td_api::object_ptr<td_api::GroupCallDataChannel> &&data_channel,
                                               string &&data, Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_conference || group_call->call_id == tde2e_api::CallId()) {
    return promise.set_error(400, "Group call doesn't support decryption");
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, participant_dialog_id, data_channel = std::move(data_channel),
           data = std::move(data), promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::decrypt_group_call_data, group_call_id, participant_dialog_id,
                           std::move(data_channel), std::move(data), std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  tde2e_api::CallChannelId channel_id{};
  if (data_channel != nullptr && data_channel->get_id() == td_api::groupCallDataChannelScreenSharing::ID) {
    channel_id = 1;
  }
  auto r_data = tde2e_api::call_decrypt(group_call->call_id, participant_dialog_id.get(), channel_id, data);
  if (r_data.is_error()) {
    return promise.set_error(400, r_data.error().message);
  }
  promise.set_value(std::move(r_data.value()));
}

void GroupCallManager::start_group_call_screen_sharing(GroupCallId group_call_id, int32 audio_source, string &&payload,
                                                       Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, audio_source, payload = std::move(payload),
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::start_group_call_screen_sharing, group_call_id, audio_source,
                           std::move(payload), std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (group_call->is_live_story) {
    return promise.set_error(400, "Can't use screen sharing in live stories");
  }

  cancel_join_group_call_presentation_request(input_group_call_id);

  auto generation = ++join_group_request_generation_;
  auto &request = pending_join_presentation_requests_[input_group_call_id];
  request = make_unique<PendingJoinPresentationRequest>();
  request->generation = generation;
  request->audio_source = audio_source;
  request->promise = std::move(promise);

  request->query_ref =
      td_->create_handler<JoinGroupCallPresentationQuery>()->send(input_group_call_id, payload, generation);

  bool need_update = false;
  if (group_call->is_inited && need_update) {
    send_update_group_call(group_call, "start_group_call_screen_sharing");
  }
}

void GroupCallManager::end_group_call_screen_sharing(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_inited || !group_call->is_active) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined || group_call->is_being_left) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::end_group_call_screen_sharing, group_call_id,
                           std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (group_call->is_live_story) {
    return promise.set_error(400, "Can't use screen sharing in live stories");
  }

  cancel_join_group_call_presentation_request(input_group_call_id);

  group_call->have_pending_is_my_presentation_paused = false;
  group_call->pending_is_my_presentation_paused = false;

  td_->create_handler<LeaveGroupCallPresentationQuery>(std::move(promise))->send(input_group_call_id);
}

void GroupCallManager::try_load_group_call_administrators(InputGroupCallId input_group_call_id, DialogId dialog_id) {
  if (!dialog_id.is_valid()) {
    return;
  }
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call->is_conference || group_call->is_live_story ||
      !need_group_call_participants(input_group_call_id, group_call) || !can_manage_group_call(group_call)) {
    LOG(INFO) << "Don't need to load administrators in " << input_group_call_id << " from " << dialog_id;
    return;
  }
  if (dialog_id.get_type() == DialogType::User) {
    DialogParticipants participants;
    participants.total_count_ = 1;
    participants.participants_.push_back(
        DialogParticipant{dialog_id, UserId(), 0, DialogParticipantStatus::Creator(true, false, string())});
    return finish_load_group_call_administrators(input_group_call_id, std::move(participants));
  }

  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](Result<DialogParticipants> &&result) {
        send_closure(actor_id, &GroupCallManager::finish_load_group_call_administrators, input_group_call_id,
                     std::move(result));
      });
  td_->dialog_participant_manager_->search_dialog_participants(
      dialog_id, string(), 100,
      DialogParticipantFilter(td_, dialog_id, td_api::make_object<td_api::chatMembersFilterAdministrators>()),
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
  if (!group_call->dialog_id.is_valid() || group_call->is_conference || group_call->is_live_story ||
      !can_manage_group_call(group_call)) {
    return;
  }

  vector<DialogId> administrator_dialog_ids;
  auto participants = result.move_as_ok();
  for (auto &administrator : participants.participants_) {
    if (administrator.status_.can_manage_calls() &&
        administrator.dialog_id_ != td_->dialog_manager_->get_my_dialog_id()) {
      administrator_dialog_ids.push_back(administrator.dialog_id_);
    }
  }

  auto *group_call_participants =
      add_group_call_participants(input_group_call_id, "finish_load_group_call_administrators");
  if (group_call_participants->are_administrators_loaded &&
      group_call_participants->administrator_dialog_ids == administrator_dialog_ids) {
    return;
  }

  LOG(INFO) << "Set administrators of " << input_group_call_id << " to " << administrator_dialog_ids;
  group_call_participants->are_administrators_loaded = true;
  group_call_participants->administrator_dialog_ids = std::move(administrator_dialog_ids);

  update_group_call_participants_can_be_muted(input_group_call_id, true, group_call_participants,
                                              get_group_call_is_creator(group_call));
}

void GroupCallManager::process_join_video_chat_response(InputGroupCallId input_group_call_id, uint64 generation,
                                                        telegram_api::object_ptr<telegram_api::Updates> &&updates,
                                                        Promise<Unit> &&promise) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end() || it->second->generation != generation) {
    LOG(INFO) << "Ignore JoinVideoChatQuery response with " << input_group_call_id << " and generation " << generation;
    return;
  }
  CHECK(updates != nullptr);

  auto new_message_updates = UpdatesManager::extract_group_call_messages(updates.get());
  if (!new_message_updates.empty()) {
    td_->updates_manager_->process_updates_users_and_chats(updates.get());

    std::reverse(new_message_updates.begin(), new_message_updates.end());
    auto group_call = get_group_call(input_group_call_id);
    CHECK(group_call != nullptr);
    group_call->old_messages.clear();
    for (auto &update : new_message_updates) {
      if (input_group_call_id != InputGroupCallId(update->call_)) {
        LOG(ERROR) << "Receive message in " << InputGroupCallId(update->call_) << " instead of " << input_group_call_id;
        continue;
      }
      group_call->old_messages.push_back(GroupCallMessage(td_, std::move(update->message_)));
    }
    if (need_group_call_participants(input_group_call_id, group_call) &&
        add_group_call_participants(input_group_call_id, "process_join_video_chat_response")->are_top_donors_loaded) {
      apply_old_server_messages(input_group_call_id, group_call);
    }
  }
  td_->updates_manager_->on_get_updates(std::move(updates),
                                        PromiseCreator::lambda([promise = std::move(promise)](Unit) mutable {
                                          promise.set_error(500, "Wrong join response received");
                                        }));
}

void GroupCallManager::process_join_group_call_presentation_response(InputGroupCallId input_group_call_id,
                                                                     uint64 generation,
                                                                     tl_object_ptr<telegram_api::Updates> &&updates,
                                                                     Status status) {
  auto it = pending_join_presentation_requests_.find(input_group_call_id);
  if (it == pending_join_presentation_requests_.end() || it->second->generation != generation) {
    LOG(INFO) << "Ignore JoinGroupCallPresentationQuery response with " << input_group_call_id << " and generation "
              << generation;
    return;
  }
  auto promise = std::move(it->second->promise);
  pending_join_presentation_requests_.erase(it);

  if (status.is_error()) {
    return promise.set_error(std::move(status));
  }
  CHECK(updates != nullptr);

  string params = UpdatesManager::extract_join_group_call_presentation_params(updates.get());
  if (params.empty()) {
    return promise.set_error(500, "Wrong start group call screen sharing response received: parameters are missing");
  }
  td_->updates_manager_->on_get_updates(
      std::move(updates), PromiseCreator::lambda([params = std::move(params), promise = std::move(promise)](
                                                     Unit) mutable { promise.set_value(std::move(params)); }));
}

bool GroupCallManager::on_join_group_call_response(InputGroupCallId input_group_call_id, string json_response) {
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end()) {
    return false;
  }
  auto request = std::move(it->second);
  CHECK(request != nullptr);
  pending_join_requests_.erase(it);

  LOG(INFO) << "Successfully joined " << input_group_call_id;

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  group_call->is_joined = true;
  group_call->need_rejoin = false;
  group_call->is_being_joined = false;
  group_call->is_being_left = false;
  group_call->joined_date = G()->unix_time();
  group_call->audio_source = request->audio_source;
  group_call->as_dialog_id = request->as_dialog_id;
  if (group_call->is_conference) {
    if (request->private_key_id == tde2e_api::PrivateKeyId()) {
      LOG(ERROR) << "Have no private key in " << input_group_call_id;
    } else {
      group_call->private_key_id = request->private_key_id;
      group_call->public_key_id = request->public_key_id;

      auto block_it = being_joined_call_blocks_.find(input_group_call_id);
      if (block_it != being_joined_call_blocks_.end()) {
        auto &blocks = block_it->second;
        if (blocks.is_inited_[0] && blocks.is_inited_[1]) {
          CHECK(!blocks.blocks_[0].empty());
          auto my_user_id = td_->user_manager_->get_my_id();
          auto r_call_id = tde2e_api::call_create(my_user_id.get(), group_call->private_key_id, blocks.blocks_[0][0]);
          if (r_call_id.is_error()) {
            LOG(ERROR) << "Failed to create call";
          } else {
            group_call->call_id = r_call_id.value();
            for (size_t i = 1; i < blocks.blocks_[0].size(); i++) {
              tde2e_api::call_apply_block(group_call->call_id, blocks.blocks_[0][i]);
            }
            for (const auto &block : blocks.blocks_[1]) {
              tde2e_api::call_receive_inbound_message(group_call->call_id, block);
            }
            group_call->block_next_offset[0] = blocks.next_offset_[0];
            group_call->block_next_offset[1] = blocks.next_offset_[1];

            poll_group_call_blocks_timeout_.set_timeout_in(group_call->group_call_id.get() * 2,
                                                           GROUP_CALL_BLOCK_POLL_TIMEOUT);
            poll_group_call_blocks_timeout_.set_timeout_in(group_call->group_call_id.get() * 2 + 1,
                                                           GROUP_CALL_BLOCK_POLL_TIMEOUT);
            on_call_state_updated(group_call, "on_join_group_call_response");
            on_call_verification_state_updated(group_call);
          }
        } else {
          LOG(ERROR) << "Have no blocks for a subchain in " << input_group_call_id;
        }
        being_joined_call_blocks_.erase(block_it);
      } else {
        LOG(ERROR) << "Have no blocks in " << input_group_call_id;
      }
    }
  } else if (request->private_key_id != tde2e_api::PrivateKeyId()) {
    LOG(ERROR) << "Have private key in " << input_group_call_id;
  }
  request->promise.set_value(std::move(json_response));

  if (group_call->is_live_story) {
    poll_group_call_stars_timeout_.cancel_timeout(group_call->group_call_id.get());
    get_group_call_stars_from_server(input_group_call_id, Auto());
    if (!group_call->loaded_available_message_senders) {
      td_->create_handler<GetGroupCallSendAsQuery>(Promise<Unit>())->send(input_group_call_id, group_call->dialog_id);
    }
  }
  if (group_call->audio_source != 0) {
    check_group_call_is_joined_timeout_.set_timeout_in(group_call->group_call_id.get(),
                                                       CHECK_GROUP_CALL_IS_JOINED_TIMEOUT);
  }
  if (group_call->need_syncing_participants) {
    sync_participants_timeout_.add_timeout_in(group_call->group_call_id.get(), 0.0);
  }
  try_clear_group_call_participants(input_group_call_id);
  process_group_call_after_join_requests(input_group_call_id, "on_join_group_call_response");
  return true;
}

void GroupCallManager::finish_join_group_call(InputGroupCallId input_group_call_id, uint64 generation, Status error) {
  CHECK(error.is_error());
  auto it = pending_join_requests_.find(input_group_call_id);
  if (it == pending_join_requests_.end() || (generation != 0 && it->second->generation != generation)) {
    return;
  }
  tde2e_api::key_destroy(it->second->private_key_id);
  tde2e_api::key_destroy(it->second->public_key_id);
  it->second->promise.set_error(std::move(error));
  auto as_dialog_id = it->second->as_dialog_id;
  pending_join_requests_.erase(it);

  if (G()->close_flag()) {
    return;
  }

  GroupCall *group_call = get_group_call(input_group_call_id);
  bool need_update = false;
  if (group_call != nullptr && group_call->is_being_joined) {
    bool old_is_joined = get_group_call_is_joined(group_call);
    group_call->is_being_joined = false;
    need_update |= old_is_joined != get_group_call_is_joined(group_call);
  }
  group_call->old_messages.clear();
  remove_recent_group_call_speaker(input_group_call_id, as_dialog_id);
  if (try_clear_group_call_participants(input_group_call_id)) {
    CHECK(group_call != nullptr);
    need_update = true;
  }
  if (need_update && group_call->is_inited) {
    send_update_group_call(group_call, "finish_join_group_call");
  }
  process_group_call_after_join_requests(input_group_call_id, "finish_join_group_call");

  if (group_call != nullptr && group_call->dialog_id.is_valid()) {
    update_group_call_dialog(group_call, "finish_join_group_call", false);
    td_->dialog_manager_->reload_dialog_info_full(group_call->dialog_id, "finish_join_group_call");
  }
}

void GroupCallManager::process_group_call_after_join_requests(InputGroupCallId input_group_call_id,
                                                              const char *source) {
  GroupCall *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    return;
  }
  if (group_call->is_being_joined || group_call->need_rejoin) {
    LOG(ERROR) << "Failed to process after-join requests from " << source << ": " << group_call->is_being_joined << " "
               << group_call->need_rejoin;
    return;
  }
  if (group_call->after_join.empty()) {
    return;
  }

  if (!group_call->is_active || group_call->is_being_left || !group_call->is_joined) {
    fail_promises(group_call->after_join, Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  } else {
    set_promises(group_call->after_join);
  }
}

void GroupCallManager::set_group_call_title(GroupCallId group_call_id, string title, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(
        input_group_call_id,
        PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, title, promise = std::move(promise)](
                                   Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            send_closure(actor_id, &GroupCallManager::set_group_call_title, group_call_id, std::move(title),
                         std::move(promise));
          }
        }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || !group_call->can_be_managed || group_call->is_live_story) {
    return promise.set_error(400, "Can't change group call title");
  }

  title = clean_name(title, MAX_TITLE_LENGTH);
  if (title == get_group_call_title(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  if (group_call->pending_title.empty()) {
    send_edit_group_call_title_query(input_group_call_id, title);
  }
  group_call->pending_title = std::move(title);
  send_update_group_call(group_call, "set_group_call_title");
  promise.set_value(Unit());
}

void GroupCallManager::send_edit_group_call_title_query(InputGroupCallId input_group_call_id, const string &title) {
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, title](Result<Unit> result) {
    send_closure(actor_id, &GroupCallManager::on_edit_group_call_title, input_group_call_id, title, std::move(result));
  });
  td_->create_handler<EditGroupCallTitleQuery>(std::move(promise))->send(input_group_call_id, title);
}

void GroupCallManager::on_edit_group_call_title(InputGroupCallId input_group_call_id, const string &title,
                                                Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return;
  }

  if (group_call->pending_title != title && group_call->can_be_managed && !group_call->is_live_story) {
    // need to send another request
    send_edit_group_call_title_query(input_group_call_id, group_call->pending_title);
    return;
  }

  bool is_different = group_call->pending_title != group_call->title;
  if (is_different && group_call->can_be_managed && !group_call->is_live_story) {
    LOG(ERROR) << "Failed to set title to " << group_call->pending_title << " in " << input_group_call_id << ": "
               << result.error();
  }
  group_call->pending_title.clear();
  if (is_different) {
    send_update_group_call(group_call, "on_set_group_call_title failed");
  }
}

void GroupCallManager::toggle_group_call_is_my_video_paused(GroupCallId group_call_id, bool is_my_video_paused,
                                                            Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, is_my_video_paused,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::toggle_group_call_is_my_video_paused, group_call_id,
                           is_my_video_paused, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  if (is_my_video_paused == get_group_call_is_my_video_paused(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_is_my_video_paused = is_my_video_paused;
  if (!group_call->have_pending_is_my_video_paused) {
    group_call->have_pending_is_my_video_paused = true;
    send_toggle_group_call_is_my_video_paused_query(input_group_call_id, group_call->as_dialog_id, is_my_video_paused);
  }
  send_update_group_call(group_call, "toggle_group_call_is_my_video_paused");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_is_my_video_paused_query(InputGroupCallId input_group_call_id,
                                                                       DialogId as_dialog_id, bool is_my_video_paused) {
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, is_my_video_paused](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_is_my_video_paused, input_group_call_id,
                     is_my_video_paused, std::move(result));
      });
  td_->create_handler<EditGroupCallParticipantQuery>(std::move(promise))
      ->send(input_group_call_id, as_dialog_id, false, false, 0, false, false, false, false, true, is_my_video_paused,
             false, false);
}

void GroupCallManager::on_toggle_group_call_is_my_video_paused(InputGroupCallId input_group_call_id,
                                                               bool is_my_video_paused, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_is_my_video_paused) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_is_my_video_paused = false;
    LOG(ERROR) << "Failed to set is_my_video_paused to " << is_my_video_paused << " in " << input_group_call_id << ": "
               << result.error();
    if (group_call->pending_is_my_video_paused != group_call->is_my_video_paused) {
      send_update_group_call(group_call, "on_toggle_group_call_is_my_video_paused failed");
    }
  } else {
    group_call->is_my_video_paused = is_my_video_paused;
    if (group_call->pending_is_my_video_paused != is_my_video_paused) {
      // need to send another request
      send_toggle_group_call_is_my_video_paused_query(input_group_call_id, group_call->as_dialog_id,
                                                      group_call->pending_is_my_video_paused);
      return;
    }

    group_call->have_pending_is_my_video_paused = false;
  }
}

void GroupCallManager::toggle_group_call_is_my_video_enabled(GroupCallId group_call_id, bool is_my_video_enabled,
                                                             Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, is_my_video_enabled,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::toggle_group_call_is_my_video_enabled, group_call_id,
                           is_my_video_enabled, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  if (is_my_video_enabled == get_group_call_is_my_video_enabled(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_is_my_video_enabled = is_my_video_enabled;
  if (!group_call->have_pending_is_my_video_enabled) {
    group_call->have_pending_is_my_video_enabled = true;
    send_toggle_group_call_is_my_video_enabled_query(input_group_call_id, group_call->as_dialog_id,
                                                     is_my_video_enabled);
  }
  send_update_group_call(group_call, "toggle_group_call_is_my_video_enabled");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_is_my_video_enabled_query(InputGroupCallId input_group_call_id,
                                                                        DialogId as_dialog_id,
                                                                        bool is_my_video_enabled) {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, is_my_video_enabled](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_is_my_video_enabled, input_group_call_id,
                     is_my_video_enabled, std::move(result));
      });
  td_->create_handler<EditGroupCallParticipantQuery>(std::move(promise))
      ->send(input_group_call_id, as_dialog_id, false, false, 0, false, false, true, !is_my_video_enabled, false, false,
             false, false);
}

void GroupCallManager::on_toggle_group_call_is_my_video_enabled(InputGroupCallId input_group_call_id,
                                                                bool is_my_video_enabled, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_is_my_video_enabled) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_is_my_video_enabled = false;
    LOG(ERROR) << "Failed to set is_my_video_enabled to " << is_my_video_enabled << " in " << input_group_call_id
               << ": " << result.error();
    if (group_call->pending_is_my_video_enabled != group_call->is_my_video_enabled) {
      send_update_group_call(group_call, "on_toggle_group_call_is_my_video_enabled failed");
    }
  } else {
    group_call->is_my_video_enabled = is_my_video_enabled;
    if (group_call->pending_is_my_video_enabled != is_my_video_enabled) {
      // need to send another request
      send_toggle_group_call_is_my_video_enabled_query(input_group_call_id, group_call->as_dialog_id,
                                                       group_call->pending_is_my_video_enabled);
      return;
    }

    group_call->have_pending_is_my_video_enabled = false;
  }
}

void GroupCallManager::toggle_group_call_is_my_presentation_paused(GroupCallId group_call_id,
                                                                   bool is_my_presentation_paused,
                                                                   Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, is_my_presentation_paused,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::toggle_group_call_is_my_presentation_paused, group_call_id,
                           is_my_presentation_paused, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  if (is_my_presentation_paused == get_group_call_is_my_presentation_paused(group_call)) {
    return promise.set_value(Unit());
  }
  if (group_call->is_live_story) {
    return promise.set_error(400, "Can't use screen sharing in live stories");
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_is_my_presentation_paused = is_my_presentation_paused;
  if (!group_call->have_pending_is_my_presentation_paused) {
    group_call->have_pending_is_my_presentation_paused = true;
    send_toggle_group_call_is_my_presentation_paused_query(input_group_call_id, group_call->as_dialog_id,
                                                           is_my_presentation_paused);
  }
  send_update_group_call(group_call, "toggle_group_call_is_my_presentation_paused");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_is_my_presentation_paused_query(InputGroupCallId input_group_call_id,
                                                                              DialogId as_dialog_id,
                                                                              bool is_my_presentation_paused) {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, is_my_presentation_paused](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_is_my_presentation_paused, input_group_call_id,
                     is_my_presentation_paused, std::move(result));
      });
  td_->create_handler<EditGroupCallParticipantQuery>(std::move(promise))
      ->send(input_group_call_id, as_dialog_id, false, false, 0, false, false, false, false, false, false, true,
             is_my_presentation_paused);
}

void GroupCallManager::on_toggle_group_call_is_my_presentation_paused(InputGroupCallId input_group_call_id,
                                                                      bool is_my_presentation_paused,
                                                                      Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_is_my_presentation_paused) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_is_my_presentation_paused = false;
    LOG(ERROR) << "Failed to set is_my_presentation_paused to " << is_my_presentation_paused << " in "
               << input_group_call_id << ": " << result.error();
    if (group_call->pending_is_my_presentation_paused != group_call->is_my_presentation_paused) {
      send_update_group_call(group_call, "on_toggle_group_call_is_my_presentation_paused failed");
    }
  } else {
    group_call->is_my_presentation_paused = is_my_presentation_paused;
    if (group_call->pending_is_my_presentation_paused != is_my_presentation_paused) {
      // need to send another request
      send_toggle_group_call_is_my_presentation_paused_query(input_group_call_id, group_call->as_dialog_id,
                                                             group_call->pending_is_my_presentation_paused);
      return;
    }

    group_call->have_pending_is_my_presentation_paused = false;
  }
}

void GroupCallManager::toggle_group_call_start_subscribed(GroupCallId group_call_id, bool start_subscribed,
                                                          Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda(
                          [actor_id = actor_id(this), group_call_id, start_subscribed, promise = std::move(promise)](
                              Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                            if (result.is_error()) {
                              promise.set_error(result.move_as_error());
                            } else {
                              send_closure(actor_id, &GroupCallManager::toggle_group_call_start_subscribed,
                                           group_call_id, start_subscribed, std::move(promise));
                            }
                          }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || group_call->scheduled_start_date <= 0 ||
      group_call->is_live_story) {
    return promise.set_error(400, "The group call isn't scheduled");
  }

  if (start_subscribed == get_group_call_start_subscribed(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_start_subscribed = start_subscribed;
  if (!group_call->have_pending_start_subscribed) {
    group_call->have_pending_start_subscribed = true;
    send_toggle_group_call_start_subscription_query(input_group_call_id, start_subscribed);
  }
  send_update_group_call(group_call, "toggle_group_call_start_subscribed");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_start_subscription_query(InputGroupCallId input_group_call_id,
                                                                       bool start_subscribed) {
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, start_subscribed](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_start_subscription, input_group_call_id,
                     start_subscribed, std::move(result));
      });
  td_->create_handler<ToggleGroupCallStartSubscriptionQuery>(std::move(promise))
      ->send(input_group_call_id, start_subscribed);
}

void GroupCallManager::on_toggle_group_call_start_subscription(InputGroupCallId input_group_call_id,
                                                               bool start_subscribed, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_start_subscribed) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_start_subscribed = false;
    LOG(ERROR) << "Failed to set enabled_start_notification to " << start_subscribed << " in " << input_group_call_id
               << ": " << result.error();
    if (group_call->pending_start_subscribed != group_call->start_subscribed) {
      send_update_group_call(group_call, "on_toggle_group_call_start_subscription failed");
    }
  } else {
    if (group_call->pending_start_subscribed != start_subscribed) {
      // need to send another request
      send_toggle_group_call_start_subscription_query(input_group_call_id, group_call->pending_start_subscribed);
      return;
    }

    group_call->have_pending_start_subscribed = false;
    if (group_call->start_subscribed != start_subscribed) {
      LOG(ERROR) << "Failed to set enabled_start_notification to " << start_subscribed << " in " << input_group_call_id;
      send_update_group_call(group_call, "on_toggle_group_call_start_subscription failed 2");
    }
  }
}

void GroupCallManager::toggle_group_call_mute_new_participants(GroupCallId group_call_id, bool mute_new_participants,
                                                               Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, mute_new_participants,
                                              promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::toggle_group_call_mute_new_participants,
                                       group_call_id, mute_new_participants, std::move(promise));
                        }
                      }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || !group_call->can_be_managed ||
      !group_call->allowed_toggle_mute_new_participants || group_call->is_live_story) {
    return promise.set_error(400, "Can't change mute_new_participants setting");
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
  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))
      ->send(input_group_call_id, false, true, mute_new_participants, false, false, false, 0);
}

void GroupCallManager::on_toggle_group_call_mute_new_participants(InputGroupCallId input_group_call_id,
                                                                  bool mute_new_participants, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_mute_new_participants) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_mute_new_participants = false;
    if (group_call->can_be_managed && group_call->allowed_toggle_mute_new_participants && !group_call->is_live_story) {
      LOG(ERROR) << "Failed to set mute_new_participants to " << mute_new_participants << " in " << input_group_call_id
                 << ": " << result.error();
    }
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

void GroupCallManager::toggle_group_call_are_messages_enabled(GroupCallId group_call_id, bool are_messages_enabled,
                                                              Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, are_messages_enabled,
                                              promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::toggle_group_call_are_messages_enabled,
                                       group_call_id, are_messages_enabled, std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_active || !group_call->can_be_managed || !group_call->allowed_toggle_are_messages_enabled) {
    return promise.set_error(400, "Can't change are_messages_enabled setting");
  }

  if (are_messages_enabled == get_group_call_are_messages_enabled(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_are_messages_enabled = are_messages_enabled;
  if (!group_call->have_pending_are_messages_enabled) {
    group_call->have_pending_are_messages_enabled = true;
    send_toggle_group_call_are_messages_enabled_query(input_group_call_id, are_messages_enabled);
  }
  send_update_group_call(group_call, "toggle_group_call_are_messages_enabled");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_are_messages_enabled_query(InputGroupCallId input_group_call_id,
                                                                         bool are_messages_enabled) {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, are_messages_enabled](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_are_messages_enabled, input_group_call_id,
                     are_messages_enabled, std::move(result));
      });
  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))
      ->send(input_group_call_id, false, false, false, true, are_messages_enabled, false, 0);
}

void GroupCallManager::on_toggle_group_call_are_messages_enabled(InputGroupCallId input_group_call_id,
                                                                 bool are_messages_enabled, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_are_messages_enabled) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_are_messages_enabled = false;
    if (group_call->can_be_managed && group_call->allowed_toggle_are_messages_enabled) {
      LOG(ERROR) << "Failed to set are_messages_enabled to " << are_messages_enabled << " in " << input_group_call_id
                 << ": " << result.error();
    }
    if (group_call->pending_are_messages_enabled != group_call->are_messages_enabled) {
      send_update_group_call(group_call, "on_toggle_group_call_are_messages_enabled failed");
    }
  } else {
    if (group_call->pending_are_messages_enabled != are_messages_enabled) {
      // need to send another request
      send_toggle_group_call_are_messages_enabled_query(input_group_call_id, group_call->pending_are_messages_enabled);
      return;
    }

    group_call->have_pending_are_messages_enabled = false;
    if (group_call->are_messages_enabled != are_messages_enabled) {
      LOG(ERROR) << "Failed to set are_messages_enabled to " << are_messages_enabled << " in " << input_group_call_id;
      send_update_group_call(group_call, "on_toggle_group_call_are_messages_enabled failed 2");
    }
  }
}

void GroupCallManager::set_group_call_paid_message_star_count(GroupCallId group_call_id, int64 paid_message_star_count,
                                                              Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, paid_message_star_count,
                                              promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::set_group_call_paid_message_star_count,
                                       group_call_id, paid_message_star_count, std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_active || !group_call->can_be_managed || !group_call->is_live_story) {
    return promise.set_error(400, "Can't change paid_message_star_count setting");
  }

  if (paid_message_star_count == get_group_call_paid_message_star_count(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  group_call->pending_paid_message_star_count = paid_message_star_count;
  if (!group_call->have_pending_paid_message_star_count) {
    group_call->have_pending_paid_message_star_count = true;
    send_set_group_call_paid_message_star_count_query(input_group_call_id, paid_message_star_count);
  }
  send_update_group_call(group_call, "set_group_call_paid_message_star_count");
  promise.set_value(Unit());
}

void GroupCallManager::send_set_group_call_paid_message_star_count_query(InputGroupCallId input_group_call_id,
                                                                         int64 paid_message_star_count) {
  auto promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), input_group_call_id, paid_message_star_count](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_set_group_call_paid_message_star_count, input_group_call_id,
                     paid_message_star_count, std::move(result));
      });
  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))
      ->send(input_group_call_id, false, false, false, false, false, true, paid_message_star_count);
}

void GroupCallManager::on_set_group_call_paid_message_star_count(InputGroupCallId input_group_call_id,
                                                                 int64 paid_message_star_count, Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || !group_call->have_pending_paid_message_star_count) {
    return;
  }

  if (result.is_error()) {
    group_call->have_pending_paid_message_star_count = false;
    if (group_call->can_be_managed) {
      LOG(ERROR) << "Failed to set paid_message_star_count to " << paid_message_star_count << " in "
                 << input_group_call_id << ": " << result.error();
    }
    if (group_call->pending_paid_message_star_count != group_call->paid_message_star_count) {
      send_update_group_call(group_call, "on_set_group_call_paid_message_star_count failed");
    }
  } else {
    if (group_call->pending_paid_message_star_count != paid_message_star_count) {
      // need to send another request
      send_set_group_call_paid_message_star_count_query(input_group_call_id,
                                                        group_call->pending_paid_message_star_count);
      return;
    }

    group_call->have_pending_paid_message_star_count = false;
    if (group_call->paid_message_star_count != paid_message_star_count) {
      LOG(ERROR) << "Failed to set paid_message_star_count to " << paid_message_star_count << " in "
                 << input_group_call_id;
      send_update_group_call(group_call, "on_set_group_call_paid_message_star_count failed 2");
    }
  }
}

bool GroupCallManager::get_group_call_message_is_from_admin(const GroupCall *group_call, DialogId sender_dialog_id) {
  if (!group_call->is_live_story) {
    return false;
  }
  return sender_dialog_id == group_call->dialog_id ||
         (group_call->can_be_managed && sender_dialog_id.get_type() == DialogType::User);
}

void GroupCallManager::send_group_call_message(GroupCallId group_call_id,
                                               td_api::object_ptr<td_api::formattedText> &&text,
                                               int64 paid_message_star_count, bool is_reaction,
                                               Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  if (paid_message_star_count < 0 || (is_reaction && paid_message_star_count == 0)) {
    return promise.set_error(400, "Invalid number of Telegram Stars specified");
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, text = std::move(text),
                                              paid_message_star_count, is_reaction, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::send_group_call_message, group_call_id,
                                       std::move(text), paid_message_star_count, is_reaction, std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, text = std::move(text), paid_message_star_count, is_reaction,
           promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::send_group_call_message, group_call_id, std::move(text),
                           paid_message_star_count, is_reaction, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  TRY_RESULT_PROMISE(promise, message,
                     get_formatted_text(td_, group_call->dialog_id, std::move(text), td_->auth_manager_->is_bot(),
                                        is_reaction, true, false));
  if (group_call->is_live_story) {
    if (paid_message_star_count > 0 && group_call->dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
      return promise.set_error(400, "Can't send paid messages to self");
    }
    if (!is_reaction && !td_->star_manager_->has_owned_star_count(paid_message_star_count)) {
      return promise.set_error(400, "Have not enough Telegram Stars");
    }
    for (auto &c : message.text) {
      if (c == '\n') {
        c = ' ';
      }
    }
  } else {
    if (paid_message_star_count != 0) {
      if (is_reaction) {
        return promise.set_error(400, "Reactions can't be sent to the call");
      }
      return promise.set_error(400, "Paid messages can't be sent to the call");
    }
    if (static_cast<int64>(utf8_length(message.text)) > G()->get_option_integer("group_call_message_text_length_max")) {
      return promise.set_error(400, "Message is too long");
    }
  }

  auto as_dialog_id =
      group_call->is_live_story
          ? group_call->message_sender_dialog_id
          : (group_call->as_dialog_id.is_valid() ? group_call->as_dialog_id : td_->dialog_manager_->get_my_dialog_id());
  CHECK(as_dialog_id.is_valid());
  auto group_call_message = GroupCallMessage(as_dialog_id, message, paid_message_star_count,
                                             get_group_call_message_is_from_admin(group_call, as_dialog_id));
  auto message_id = add_group_call_message(input_group_call_id, group_call, group_call_message);
  if (group_call->is_conference || group_call->call_id != tde2e_api::CallId()) {
    auto json_message = group_call_message.encode_to_json();
    auto r_data = tde2e_api::call_encrypt(group_call->call_id, tde2e_api::CallChannelId(), json_message, 0);
    if (r_data.is_error()) {
      return promise.set_error(400, r_data.error().message);
    }
    td_->create_handler<SendGroupCallEncryptedMessageQuery>(std::move(promise))
        ->send(input_group_call_id, r_data.value());
  } else {
    CHECK(is_reaction == message.text.empty());
    td_->create_handler<SendGroupCallMessageQuery>(std::move(promise))
        ->send(input_group_call_id, message_id, message, group_call->is_live_story ? as_dialog_id : DialogId(),
               paid_message_star_count, group_call->is_live_story);
  }
}

void GroupCallManager::send_group_call_reaction(GroupCallId group_call_id, int64 star_count, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  if (star_count <= 0 ||
      star_count > td_->option_manager_->get_option_integer("paid_group_call_message_star_count_max")) {
    return promise.set_error(400, "Invalid number of Telegram Stars specified");
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(
        input_group_call_id,
        PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, star_count, promise = std::move(promise)](
                                   Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
          if (result.is_error()) {
            promise.set_error(result.move_as_error());
          } else {
            send_closure(actor_id, &GroupCallManager::send_group_call_reaction, group_call_id, star_count,
                         std::move(promise));
          }
        }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, star_count,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::send_group_call_reaction, group_call_id, star_count,
                           std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_live_story) {
    return promise.set_error(400, "Reactions can't be sent to the call");
  }
  if (group_call->dialog_id == td_->dialog_manager_->get_my_dialog_id()) {
    return promise.set_error(400, "Can't send paid reactions to self");
  }
  if (!td_->star_manager_->has_owned_star_count(star_count)) {
    return promise.set_error(400, "Have not enough Telegram Stars");
  }

  if (group_call->pending_reaction_star_count > 1000000000 || star_count > 1000000000) {
    LOG(ERROR) << "Pending paid reactions overflown";
    return promise.set_error(400, "Too many Stars added");
  }
  td_->star_manager_->add_pending_owned_star_count(-star_count, false);
  group_call->pending_reaction_star_count += star_count;

  add_group_call_spent_stars(input_group_call_id, group_call, group_call->message_sender_dialog_id, true, true,
                             star_count);
  promise.set_value(Unit());
}

void GroupCallManager::commit_pending_group_call_reactions(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::commit_pending_group_call_reactions, group_call_id,
                                       std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::commit_pending_group_call_reactions, group_call_id,
                           std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_live_story) {
    return promise.set_error(400, "Reactions can't be sent to the call");
  }
  if (group_call->pending_reaction_star_count == 0) {
    return promise.set_value(Unit());
  }

  auto star_count = group_call->pending_reaction_star_count;
  group_call->pending_reaction_star_count = 0;

  auto as_dialog_id = group_call->message_sender_dialog_id;
  CHECK(as_dialog_id.is_valid());
  auto group_call_message =
      GroupCallMessage(as_dialog_id, {}, star_count, get_group_call_message_is_from_admin(group_call, as_dialog_id));
  auto message_id = add_group_call_message(input_group_call_id, group_call, group_call_message, true);
  td_->create_handler<SendGroupCallMessageQuery>(std::move(promise))
      ->send(input_group_call_id, message_id, FormattedText(), as_dialog_id, star_count, group_call->is_live_story);
}

void GroupCallManager::remove_pending_group_call_reactions(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::commit_pending_group_call_reactions, group_call_id,
                                       std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::commit_pending_group_call_reactions, group_call_id,
                           std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_live_story) {
    return promise.set_error(400, "Reactions can't be sent to the call");
  }

  if (group_call->pending_reaction_star_count > 0) {
    td_->star_manager_->add_pending_owned_star_count(group_call->pending_reaction_star_count, false);
    remove_group_call_spent_stars(input_group_call_id, group_call, group_call->pending_reaction_star_count);
    group_call->pending_reaction_star_count = 0;
  }
  promise.set_value(Unit());
}

void GroupCallManager::delete_group_call_messages(GroupCallId group_call_id, const vector<int32> &message_ids,
                                                  bool report_spam, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, message_ids, report_spam,
                                              promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::delete_group_call_messages, group_call_id,
                                       std::move(message_ids), report_spam, std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, message_ids, report_spam,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::delete_group_call_messages, group_call_id,
                           std::move(message_ids), report_spam, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  for (auto message_id : message_ids) {
    auto sender_dialog_id = group_call->messages.get_message_sender_dialog_id(message_id);
    if (sender_dialog_id != DialogId() && !can_delete_group_call_message(group_call, sender_dialog_id)) {
      return promise.set_error(400, "Can't delete the message");
    }
  }

  vector<int32> server_ids;
  vector<int32> deleted_message_ids;
  for (auto message_id : message_ids) {
    auto result = group_call->messages.delete_message(message_id);
    if (result.second) {
      if (result.first != 0) {
        server_ids.push_back(result.first);
      }
      deleted_message_ids.push_back(message_id);
    }
  }
  on_group_call_messages_deleted(group_call, std::move(deleted_message_ids));
  if (!server_ids.empty()) {
    td_->create_handler<DeleteGroupCallMessagesQuery>(std::move(promise))
        ->send(input_group_call_id, std::move(server_ids), report_spam);
  } else {
    promise.set_value(Unit());
  }
}

void GroupCallManager::delete_group_call_messages_by_sender(GroupCallId group_call_id, DialogId sender_dialog_id,
                                                            bool report_spam, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, sender_dialog_id, report_spam,
                                              promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::delete_group_call_messages_by_sender, group_call_id,
                                       sender_dialog_id, report_spam, std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, sender_dialog_id, report_spam,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::delete_group_call_messages_by_sender, group_call_id,
                           sender_dialog_id, report_spam, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!get_group_call_can_delete_messages(group_call)) {
    return promise.set_error(400, "Can't delete messages in the group call");
  }
  if (!td_->dialog_manager_->have_input_peer(sender_dialog_id, false, AccessRights::Know)) {
    return promise.set_error(400, "Message sender not found");
  }
  if (sender_dialog_id.get_type() == DialogType::SecretChat) {
    return promise.set_value(Unit());
  }

  vector<int32> server_ids;
  vector<int32> deleted_message_ids;
  group_call->messages.delete_messages_by_sender(sender_dialog_id, server_ids, deleted_message_ids);
  on_group_call_messages_deleted(group_call, std::move(deleted_message_ids));
  if (!server_ids.empty()) {
    td_->create_handler<DeleteGroupCallParticipantMessagesQuery>(std::move(promise))
        ->send(input_group_call_id, sender_dialog_id, report_spam);
  } else {
    promise.set_value(Unit());
  }
}

td_api::object_ptr<td_api::liveStoryDonors> GroupCallManager::get_live_story_donors_object(
    const GroupCallParticipants *group_call_participants) const {
  CHECK(group_call_participants->are_top_donors_loaded);
  vector<td_api::object_ptr<td_api::paidReactor>> reactors;
  for (const auto &donor : group_call_participants->top_donors) {
    if (reactors.size() < 3u || donor.is_me()) {
      reactors.push_back(donor.get_paid_reactor_object(td_));
    }
  }
  return td_api::make_object<td_api::liveStoryDonors>(group_call_participants->total_star_count, std::move(reactors));
}

void GroupCallManager::send_update_live_story_top_donors(GroupCallId group_call_id,
                                                         const GroupCallParticipants *group_call_participants) {
  if (td_->auth_manager_->is_bot()) {
    return;
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateLiveStoryTopDonors>(
                   group_call_id.get(), get_live_story_donors_object(group_call_participants)));
}

void GroupCallManager::get_group_call_stars(GroupCallId group_call_id,
                                            Promise<td_api::object_ptr<td_api::liveStoryDonors>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::get_group_call_stars, group_call_id,
                                       std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::get_group_call_stars, group_call_id, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_live_story) {
    return promise.set_error(400, "The group call isn't a live story");
  }
  if (!need_group_call_participants(input_group_call_id, group_call)) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  auto *group_call_participants = add_group_call_participants(input_group_call_id, "get_group_call_stars");
  if (group_call_participants->are_top_donors_loaded) {
    return promise.set_value(get_live_story_donors_object(group_call_participants));
  }

  get_group_call_stars_from_server(input_group_call_id, std::move(promise));
}

void GroupCallManager::get_group_call_stars_from_server(
    InputGroupCallId input_group_call_id, Promise<td_api::object_ptr<td_api::liveStoryDonors>> &&promise) {
  auto &queries = get_stars_queries_[input_group_call_id];
  queries.push_back(std::move(promise));
  if (queries.size() != 1u) {
    return;
  }
  auto query_promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id](
                                 Result<telegram_api::object_ptr<telegram_api::phone_groupCallStars>> r_stars) {
        send_closure(actor_id, &GroupCallManager::on_get_group_call_stars, input_group_call_id, std::move(r_stars));
      });
  td_->create_handler<GetGroupCallStarsQuery>(std::move(query_promise))->send(input_group_call_id);
}

void GroupCallManager::on_get_group_call_stars(
    InputGroupCallId input_group_call_id,
    Result<telegram_api::object_ptr<telegram_api::phone_groupCallStars>> r_stars) {
  if (G()->close_flag()) {
    return;
  }
  auto it = get_stars_queries_.find(input_group_call_id);
  CHECK(it != get_stars_queries_.end());
  auto promises = std::move(it->second);
  CHECK(!promises.empty());
  get_stars_queries_.erase(it);

  auto *group_call = get_group_call(input_group_call_id);
  auto need_participants = need_group_call_participants(input_group_call_id, group_call);
  if (!need_participants) {
    if (r_stars.is_ok()) {
      r_stars = Status::Error(400, "GROUPCALL_JOIN_MISSING");
    }
  } else if (group_call->is_joined) {
    poll_group_call_stars_timeout_.add_timeout_in(group_call->group_call_id.get(), 30.0);
  }

  if (r_stars.is_error()) {
    if (group_call != nullptr) {
      auto error_message = r_stars.error().message();
      if (error_message == "GROUPCALL_FORBIDDEN" || error_message == "GROUPCALL_INVALID") {
        on_group_call_left(input_group_call_id, group_call->audio_source, false);
      } else if (need_participants) {
        apply_old_server_messages(input_group_call_id, group_call);
      }
      group_call->old_messages.clear();
    }
    return fail_promises(promises, r_stars.move_as_error());
  }
  auto stars = r_stars.move_as_ok();

  td_->user_manager_->on_get_users(std::move(stars->users_), "on_get_group_call_stars");
  td_->chat_manager_->on_get_chats(std::move(stars->chats_), "on_get_group_call_stars");

  auto total_star_count = StarManager::get_star_count(stars->total_stars_);
  int64 sum_star_count = 0;
  vector<MessageReactor> reactors;
  for (auto &donor : stars->top_donors_) {
    MessageReactor reactor(td_, std::move(donor));
    if (!reactor.is_valid()) {
      LOG(ERROR) << "Receive invalid " << reactor;
      continue;
    }
    sum_star_count += reactor.get_count();
    reactors.push_back(std::move(reactor));
  }
  MessageReactor::fix_message_reactors(reactors, true, true);
  if (total_star_count < sum_star_count) {
    LOG(ERROR) << "Receive " << total_star_count << " total donated Stars and " << sum_star_count
               << " Stars for top donors";
    total_star_count = sum_star_count;
  }
  if (group_call->pending_reaction_star_count > 0) {
    add_top_donors_spent_stars(total_star_count, reactors, group_call->message_sender_dialog_id, true,
                               group_call->pending_reaction_star_count);
  }

  CHECK(group_call != nullptr);
  auto *group_call_participants = add_group_call_participants(input_group_call_id, "on_get_group_call_stars");
  if (!group_call_participants->are_top_donors_loaded ||
      group_call_participants->total_star_count != total_star_count ||
      group_call_participants->top_donors != reactors) {
    group_call_participants->are_top_donors_loaded = true;
    group_call_participants->total_star_count = total_star_count;
    group_call_participants->top_donors = reactors;

    send_update_live_story_top_donors(group_call->group_call_id, group_call_participants);
  }

  for (auto &promise : promises) {
    if (promise) {
      promise.set_value(get_live_story_donors_object(group_call_participants));
    }
  }

  for (const auto &message : group_call->old_messages) {
    add_group_call_message(input_group_call_id, group_call, message, true);
  }
  group_call->old_messages.clear();
}

void GroupCallManager::revoke_group_call_invite_link(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, promise = std::move(promise)](
                                                 Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                        if (result.is_error()) {
                          promise.set_error(result.move_as_error());
                        } else {
                          send_closure(actor_id, &GroupCallManager::revoke_group_call_invite_link, group_call_id,
                                       std::move(promise));
                        }
                      }));
    return;
  }
  if (!group_call->is_active || !(group_call->is_conference ? group_call->is_creator : group_call->can_be_managed) ||
      group_call->is_live_story) {
    return promise.set_error(400, "Can't revoke invite link in the group call");
  }

  td_->create_handler<ToggleGroupCallSettingsQuery>(std::move(promise))
      ->send(input_group_call_id, true, false, false, false, false, false, 0);
}

void GroupCallManager::invite_group_call_participant(
    GroupCallId group_call_id, UserId user_id, bool is_video,
    Promise<td_api::object_ptr<td_api::InviteGroupCallParticipantResult>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_conference || group_call->is_live_story) {
    return promise.set_error(400, "Use inviteVideoChatParticipants for video chats");
  }
  if (!is_group_call_active(group_call) || group_call->is_being_left) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, user_id, is_video,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::invite_group_call_participant, group_call_id, user_id, is_video,
                           std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }

  td_->create_handler<InviteConferenceCallParticipantQuery>(std::move(promise))
      ->send(input_group_call_id, std::move(input_user), is_video);
}

void GroupCallManager::decline_group_call_invitation(MessageFullId message_full_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, server_message_id, td_->messages_manager_->get_group_call_message_id(message_full_id));

  td_->create_handler<DeclineConferenceCallInviteQuery>(std::move(promise))->send(server_message_id);
}

void GroupCallManager::delete_group_call_participants(GroupCallId group_call_id, const vector<int64> &user_ids,
                                                      bool is_ban, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto my_user_id = td_->user_manager_->get_my_id();
  for (auto &user_id : user_ids) {
    if (user_id == my_user_id.get()) {
      return promise.set_error(400, "Use leaveGroupCall to leave the group call");
    }
  }

  do_delete_group_call_participants(input_group_call_id, user_ids, is_ban, std::move(promise));
}

void GroupCallManager::do_delete_group_call_participants(InputGroupCallId input_group_call_id, vector<int64> user_ids,
                                                         bool is_ban, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_conference || group_call->is_live_story) {
    return promise.set_error(
        400,
        "Use setChatMemberStatus or setMessageSenderBlockList to ban participants from video chats or live stories");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, user_ids = std::move(user_ids),
                                  is_ban, promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_value(Unit());
            } else {
              send_closure(actor_id, &GroupCallManager::do_delete_group_call_participants, input_group_call_id,
                           std::move(user_ids), is_ban, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  auto state = tde2e_move_as_ok(tde2e_api::call_get_state(group_call->call_id));
  if (!td::remove_if(state.participants,
                     [&user_ids](const auto &participant) { return td::contains(user_ids, participant.user_id); }) &&
      !is_ban) {
    return promise.set_value(Unit());
  }
  auto block = tde2e_move_as_ok(tde2e_api::call_create_change_state_block(group_call->call_id, state));

  td_->create_handler<DeleteConferenceCallParticipantsQuery>(std::move(promise))
      ->send(input_group_call_id, std::move(user_ids), is_ban, BufferSlice(block));
}

void GroupCallManager::invite_group_call_participants(GroupCallId group_call_id, vector<UserId> &&user_ids,
                                                      Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return promise.set_error(400, "Group call is not active");
  }
  if (group_call->is_conference || group_call->is_live_story) {
    return promise.set_error(400, "The call is not a video chat");
  }

  vector<telegram_api::object_ptr<telegram_api::InputUser>> input_users;
  auto my_user_id = td_->user_manager_->get_my_id();
  for (auto user_id : user_ids) {
    TRY_RESULT_PROMISE(promise, input_user, td_->user_manager_->get_input_user(user_id));

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

void GroupCallManager::get_group_call_invite_link(GroupCallId group_call_id, bool can_self_unmute,
                                                  Promise<string> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(input_group_call_id,
                      PromiseCreator::lambda(
                          [actor_id = actor_id(this), group_call_id, can_self_unmute, promise = std::move(promise)](
                              Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
                            if (result.is_error()) {
                              promise.set_error(result.move_as_error());
                            } else {
                              send_closure(actor_id, &GroupCallManager::get_group_call_invite_link, group_call_id,
                                           can_self_unmute, std::move(promise));
                            }
                          }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || group_call->is_live_story) {
    return promise.set_error(400, "Can't get group call invite link");
  }

  if (can_self_unmute && !group_call->can_be_managed) {
    return promise.set_error(400, "Not enough rights in the group call");
  }

  td_->create_handler<ExportGroupCallInviteQuery>(std::move(promise))->send(input_group_call_id, can_self_unmute);
}

void GroupCallManager::toggle_group_call_recording(GroupCallId group_call_id, bool is_enabled, string title,
                                                   bool record_video, bool use_portrait_orientation,
                                                   Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited) {
    reload_group_call(
        input_group_call_id,
        PromiseCreator::lambda(
            [actor_id = actor_id(this), group_call_id, is_enabled, title, record_video, use_portrait_orientation,
             promise = std::move(promise)](Result<td_api::object_ptr<td_api::groupCall>> &&result) mutable {
              if (result.is_error()) {
                promise.set_error(result.move_as_error());
              } else {
                send_closure(actor_id, &GroupCallManager::toggle_group_call_recording, group_call_id, is_enabled,
                             std::move(title), record_video, use_portrait_orientation, std::move(promise));
              }
            }));
    return;
  }
  if (group_call->is_conference || !group_call->is_active || !group_call->can_be_managed || group_call->is_live_story) {
    return promise.set_error(400, "Can't manage group call recording");
  }

  title = clean_name(title, MAX_TITLE_LENGTH);

  if (is_enabled == get_group_call_has_recording(group_call)) {
    return promise.set_value(Unit());
  }

  // there is no reason to save promise; we will send an update with actual value anyway

  if (!group_call->have_pending_record_start_date) {
    send_toggle_group_call_recording_query(input_group_call_id, is_enabled, title, record_video,
                                           use_portrait_orientation, toggle_recording_generation_ + 1);
  }
  group_call->have_pending_record_start_date = true;
  group_call->pending_record_start_date = is_enabled ? G()->unix_time() : 0;
  group_call->pending_record_title = std::move(title);
  group_call->pending_record_record_video = record_video;
  group_call->pending_record_use_portrait_orientation = use_portrait_orientation;
  group_call->toggle_recording_generation = ++toggle_recording_generation_;
  send_update_group_call(group_call, "toggle_group_call_recording");
  promise.set_value(Unit());
}

void GroupCallManager::send_toggle_group_call_recording_query(InputGroupCallId input_group_call_id, bool is_enabled,
                                                              const string &title, bool record_video,
                                                              bool use_portrait_orientation, uint64 generation) {
  auto promise =
      PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, generation](Result<Unit> result) {
        send_closure(actor_id, &GroupCallManager::on_toggle_group_call_recording, input_group_call_id, generation,
                     std::move(result));
      });
  td_->create_handler<ToggleGroupCallRecordQuery>(std::move(promise))
      ->send(input_group_call_id, is_enabled, title, record_video, use_portrait_orientation);
}

void GroupCallManager::on_toggle_group_call_recording(InputGroupCallId input_group_call_id, uint64 generation,
                                                      Result<Unit> &&result) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return;
  }

  CHECK(group_call->have_pending_record_start_date);

  if (group_call->toggle_recording_generation != generation && group_call->can_be_managed &&
      !group_call->is_live_story) {
    // need to send another request
    send_toggle_group_call_recording_query(input_group_call_id, group_call->pending_record_start_date != 0,
                                           group_call->pending_record_title, group_call->pending_record_record_video,
                                           group_call->pending_record_use_portrait_orientation,
                                           group_call->toggle_recording_generation);
    return;
  }

  auto current_record_start_date = get_group_call_record_start_date(group_call);
  auto current_is_video_recorded = get_group_call_is_video_recorded(group_call);
  group_call->have_pending_record_start_date = false;
  if (current_record_start_date != get_group_call_record_start_date(group_call) ||
      current_is_video_recorded != get_group_call_is_video_recorded(group_call)) {
    send_update_group_call(group_call, "on_toggle_group_call_recording");
  }
}

void GroupCallManager::set_group_call_participant_is_speaking(
    GroupCallId group_call_id, int32 audio_source, bool is_speaking,
    Promise<td_api::object_ptr<td_api::MessageSender>> &&promise, int32 date) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call)) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, audio_source, is_speaking,
                                  promise = std::move(promise), date](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(result.move_as_error());
            } else {
              send_closure(actor_id, &GroupCallManager::set_group_call_participant_is_speaking, group_call_id,
                           audio_source, is_speaking, std::move(promise), date);
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (audio_source == 0) {
    audio_source = group_call->audio_source;
    if (audio_source == 0) {
      return promise.set_error(400, "Can't speak without joining the group call");
    }
  }

  bool is_recursive = false;
  if (date == 0) {
    date = G()->unix_time();
  } else {
    is_recursive = true;
  }
  if (group_call->audio_source != 0 && audio_source != group_call->audio_source && !is_recursive && is_speaking &&
      check_group_call_is_joined_timeout_.has_timeout(group_call_id.get())) {
    check_group_call_is_joined_timeout_.set_timeout_in(group_call_id.get(), CHECK_GROUP_CALL_IS_JOINED_TIMEOUT);
  }
  DialogId dialog_id =
      set_group_call_participant_is_speaking_by_source(input_group_call_id, audio_source, is_speaking, date);
  if (!dialog_id.is_valid()) {
    if (!is_recursive) {
      auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, audio_source, is_speaking,
                                                   promise = std::move(promise), date](Result<Unit> &&result) mutable {
        if (result.is_error()) {
          promise.set_error(result.move_as_error());
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
      promise.set_value(nullptr);
    }
    return;
  }

  if (is_speaking) {
    on_user_speaking_in_group_call(group_call_id, dialog_id, date, false, is_recursive);
  }

  if (group_call->audio_source == audio_source && group_call->is_speaking != is_speaking) {
    group_call->is_speaking = is_speaking;
    if (is_speaking && group_call->dialog_id.is_valid() && !group_call->is_live_story) {
      pending_send_speaking_action_timeout_.add_timeout_in(group_call_id.get(), 0.0);
    }
  }

  promise.set_value(get_message_sender_object(td_, dialog_id, "set_group_call_participant_is_speaking"));
}

void GroupCallManager::toggle_group_call_participant_is_muted(GroupCallId group_call_id, DialogId dialog_id,
                                                              bool is_muted, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, dialog_id, is_muted,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::toggle_group_call_participant_is_muted, group_call_id,
                           dialog_id, is_muted, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (group_call->is_live_story) {
    return promise.set_error(400, "Can't manage participants in live stories");
  }

  auto participants = add_group_call_participants(input_group_call_id, "toggle_group_call_participant_is_muted");
  auto participant = get_group_call_participant(participants, dialog_id);
  if (participant == nullptr) {
    return promise.set_error(400, "Can't find group call participant");
  }
  dialog_id = participant->dialog_id;

  bool can_manage = can_manage_group_call(group_call);
  bool is_admin = group_call->is_conference ? group_call->is_creator
                                            : td::contains(participants->administrator_dialog_ids, dialog_id);

  auto participant_copy = *participant;
  if (!participant_copy.set_pending_is_muted(is_muted, can_manage, is_admin)) {
    return promise.set_error(400, PSLICE() << "Can't " << (is_muted ? "" : "un") << "mute user");
  }
  if (participant_copy == *participant) {
    return promise.set_value(Unit());
  }
  *participant = std::move(participant_copy);

  participant->pending_is_muted_generation = ++toggle_is_muted_generation_;
  if (participant->order.is_valid()) {
    send_update_group_call_participant(input_group_call_id, *participant, "toggle_group_call_participant_is_muted");
  }

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, dialog_id,
                                               generation = participant->pending_is_muted_generation,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
      promise = Promise<Unit>();
    }
    send_closure(actor_id, &GroupCallManager::on_toggle_group_call_participant_is_muted, input_group_call_id, dialog_id,
                 generation, std::move(promise));
  });
  td_->create_handler<EditGroupCallParticipantQuery>(std::move(query_promise))
      ->send(input_group_call_id, dialog_id, true, is_muted, 0, false, false, false, false, false, false, false, false);
}

void GroupCallManager::on_toggle_group_call_participant_is_muted(InputGroupCallId input_group_call_id,
                                                                 DialogId dialog_id, uint64 generation,
                                                                 Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(Unit());
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left || !group_call->is_joined) {
    return promise.set_value(Unit());
  }

  auto participants = add_group_call_participants(input_group_call_id, "on_toggle_group_call_participant_is_muted");
  auto participant = get_group_call_participant(participants, dialog_id);
  if (participant == nullptr || participant->pending_is_muted_generation != generation) {
    return promise.set_value(Unit());
  }

  CHECK(participant->have_pending_is_muted);
  participant->have_pending_is_muted = false;
  bool can_manage = can_manage_group_call(group_call);
  if (update_group_call_participant_can_be_muted(can_manage, participants, *participant,
                                                 get_group_call_is_creator(group_call)) ||
      participant->server_is_muted_by_themselves != participant->pending_is_muted_by_themselves ||
      participant->server_is_muted_by_admin != participant->pending_is_muted_by_admin ||
      participant->server_is_muted_locally != participant->pending_is_muted_locally) {
    LOG(ERROR) << "Failed to mute/unmute " << dialog_id << " in " << input_group_call_id
               << ", can_manage = " << can_manage << ", expected " << participant->pending_is_muted_by_themselves << '/'
               << participant->pending_is_muted_by_admin << '/' << participant->pending_is_muted_locally
               << ", but received " << participant->server_is_muted_by_themselves << '/'
               << participant->server_is_muted_by_admin << '/' << participant->server_is_muted_locally;
    if (participant->order.is_valid()) {
      send_update_group_call_participant(input_group_call_id, *participant,
                                         "on_toggle_group_call_participant_is_muted");
    }
  }
  promise.set_value(Unit());
}

void GroupCallManager::set_group_call_participant_volume_level(GroupCallId group_call_id, DialogId dialog_id,
                                                               int32 volume_level, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  if (volume_level < GroupCallParticipant::MIN_VOLUME_LEVEL || volume_level > GroupCallParticipant::MAX_VOLUME_LEVEL) {
    return promise.set_error(400, "Wrong volume level specified");
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, dialog_id, volume_level,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::set_group_call_participant_volume_level, group_call_id,
                           dialog_id, volume_level, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (group_call->is_live_story) {
    return promise.set_error(400, "Can't manage participants in live stories");
  }

  auto participant =
      get_group_call_participant(input_group_call_id, dialog_id, "set_group_call_participant_volume_level");
  if (participant == nullptr) {
    return promise.set_error(400, "Can't find group call participant");
  }
  dialog_id = participant->dialog_id;

  if (participant->is_self) {
    return promise.set_error(400, "Can't change self volume level");
  }

  if (participant->get_volume_level() == volume_level) {
    return promise.set_value(Unit());
  }

  participant->pending_volume_level = volume_level;
  participant->pending_volume_level_generation = ++set_volume_level_generation_;
  if (participant->order.is_valid()) {
    send_update_group_call_participant(input_group_call_id, *participant, "set_group_call_participant_volume_level");
  }

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, dialog_id,
                                               generation = participant->pending_volume_level_generation,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
      promise = Promise<Unit>();
    }
    send_closure(actor_id, &GroupCallManager::on_set_group_call_participant_volume_level, input_group_call_id,
                 dialog_id, generation, std::move(promise));
  });
  td_->create_handler<EditGroupCallParticipantQuery>(std::move(query_promise))
      ->send(input_group_call_id, dialog_id, false, false, volume_level, false, false, false, false, false, false,
             false, false);
}

void GroupCallManager::on_set_group_call_participant_volume_level(InputGroupCallId input_group_call_id,
                                                                  DialogId dialog_id, uint64 generation,
                                                                  Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(Unit());
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left || !group_call->is_joined) {
    return promise.set_value(Unit());
  }

  auto participant =
      get_group_call_participant(input_group_call_id, dialog_id, "on_set_group_call_participant_volume_level");
  if (participant == nullptr || participant->pending_volume_level_generation != generation) {
    return promise.set_value(Unit());
  }

  CHECK(participant->pending_volume_level != 0);
  if (participant->volume_level != participant->pending_volume_level) {
    LOG(ERROR) << "Failed to set volume level of " << dialog_id << " in " << input_group_call_id;
    participant->pending_volume_level = 0;
    if (participant->order.is_valid()) {
      send_update_group_call_participant(input_group_call_id, *participant,
                                         "on_set_group_call_participant_volume_level");
    }
  } else {
    participant->pending_volume_level = 0;
  }
  promise.set_value(Unit());
}

void GroupCallManager::toggle_group_call_participant_is_hand_raised(GroupCallId group_call_id, DialogId dialog_id,
                                                                    bool is_hand_raised, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left) {
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (!group_call->is_joined) {
    if (group_call->is_being_joined || group_call->need_rejoin) {
      group_call->after_join.push_back(
          PromiseCreator::lambda([actor_id = actor_id(this), group_call_id, dialog_id, is_hand_raised,
                                  promise = std::move(promise)](Result<Unit> &&result) mutable {
            if (result.is_error()) {
              promise.set_error(400, "GROUPCALL_JOIN_MISSING");
            } else {
              send_closure(actor_id, &GroupCallManager::toggle_group_call_participant_is_hand_raised, group_call_id,
                           dialog_id, is_hand_raised, std::move(promise));
            }
          }));
      return;
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  if (group_call->is_conference || group_call->is_live_story) {
    return promise.set_error(400, "The method can be used only in video chats");
  }

  auto participants = add_group_call_participants(input_group_call_id, "toggle_group_call_participant_is_hand_raised");
  auto participant = get_group_call_participant(participants, dialog_id);
  if (participant == nullptr) {
    return promise.set_error(400, "Can't find group call participant");
  }
  dialog_id = participant->dialog_id;

  if (is_hand_raised == participant->get_is_hand_raised()) {
    return promise.set_value(Unit());
  }

  if (!participant->is_self) {
    if (is_hand_raised) {
      return promise.set_error(400, "Can't raise others hand");
    } else {
      if (!can_manage_group_call(group_call)) {
        return promise.set_error(400, "Have not enough rights in the group call");
      }
    }
  }

  participant->have_pending_is_hand_raised = true;
  participant->pending_is_hand_raised = is_hand_raised;
  participant->pending_is_hand_raised_generation = ++toggle_is_hand_raised_generation_;
  if (participant->order.is_valid()) {
    send_update_group_call_participant(input_group_call_id, *participant,
                                       "toggle_group_call_participant_is_hand_raised");
  }

  auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, dialog_id,
                                               generation = participant->pending_is_hand_raised_generation,
                                               promise = std::move(promise)](Result<Unit> &&result) mutable {
    if (result.is_error()) {
      promise.set_error(result.move_as_error());
      promise = Promise<Unit>();
    }
    send_closure(actor_id, &GroupCallManager::on_toggle_group_call_participant_is_hand_raised, input_group_call_id,
                 dialog_id, generation, std::move(promise));
  });
  td_->create_handler<EditGroupCallParticipantQuery>(std::move(query_promise))
      ->send(input_group_call_id, dialog_id, false, false, 0, true, is_hand_raised, false, false, false, false, false,
             false);
}

void GroupCallManager::on_toggle_group_call_participant_is_hand_raised(InputGroupCallId input_group_call_id,
                                                                       DialogId dialog_id, uint64 generation,
                                                                       Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(Unit());
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left || !group_call->is_joined) {
    return promise.set_value(Unit());
  }

  auto participant =
      get_group_call_participant(input_group_call_id, dialog_id, "on_toggle_group_call_participant_is_hand_raised");
  if (participant == nullptr || participant->pending_is_hand_raised_generation != generation) {
    return promise.set_value(Unit());
  }

  CHECK(participant->have_pending_is_hand_raised);
  participant->have_pending_is_hand_raised = false;
  if (participant->get_is_hand_raised() != participant->pending_is_hand_raised) {
    LOG(ERROR) << "Failed to change raised hand state for " << dialog_id << " in " << input_group_call_id;
    if (participant->order.is_valid()) {
      send_update_group_call_participant(input_group_call_id, *participant,
                                         "on_toggle_group_call_participant_is_hand_raised");
    }
  }
  promise.set_value(Unit());
}

void GroupCallManager::get_group_call_participants(
    td_api::object_ptr<td_api::InputGroupCall> input_group_call, int32 limit,
    Promise<td_api::object_ptr<td_api::groupCallParticipants>> &&promise) {
  TRY_RESULT_PROMISE(promise, group_call, InputGroupCall::get_input_group_call(td_, std::move(input_group_call)));
  if (limit <= 0) {
    return promise.set_error(400, "Parameter limit must be positive");
  }
  td_->create_handler<GetInputGroupCallParticipantsQuery>(std::move(promise))->send(group_call, limit);
}

void GroupCallManager::load_group_call_participants(GroupCallId group_call_id, int32 limit, Promise<Unit> &&promise) {
  if (limit <= 0) {
    return promise.set_error(400, "Parameter limit must be positive");
  }

  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!need_group_call_participants(input_group_call_id, group_call) || group_call->is_live_story) {
    return promise.set_error(400, "Can't load group call participants");
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
  if (limit == 1 && next_offset.empty()) {
    // prevent removing self as the first user and deducing that there are no more participants
    limit = 2;
  }
  td_->create_handler<GetGroupCallParticipantsQuery>(std::move(promise))
      ->send(input_group_call_id, std::move(next_offset), limit);
}

void GroupCallManager::leave_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));

  auto *group_call = get_group_call(input_group_call_id);
  if (!is_group_call_active(group_call) || group_call->is_being_left || !group_call->is_joined) {
    if (group_call != nullptr) {
      bool old_is_joined = get_group_call_is_joined(group_call);
      if (cancel_join_group_call_request(input_group_call_id, group_call) != 0) {
        if (try_clear_group_call_participants(input_group_call_id) ||
            old_is_joined != get_group_call_is_joined(group_call)) {
          send_update_group_call(group_call, "leave_group_call 1");
        }
        process_group_call_after_join_requests(input_group_call_id, "leave_group_call 1");
        return promise.set_value(Unit());
      }
    }
    if (group_call != nullptr && group_call->need_rejoin) {
      group_call->need_rejoin = false;
      send_update_group_call(group_call, "leave_group_call");
      if (try_clear_group_call_participants(input_group_call_id)) {
        send_update_group_call(group_call, "leave_group_call 2");
      }
      process_group_call_after_join_requests(input_group_call_id, "leave_group_call 2");
      return promise.set_value(Unit());
    }
    return promise.set_error(400, "GROUPCALL_JOIN_MISSING");
  }
  auto audio_source = cancel_join_group_call_request(input_group_call_id, group_call);
  if (audio_source == 0) {
    audio_source = group_call->audio_source;
  }
  group_call->is_being_left = true;
  group_call->need_rejoin = false;
  group_call->pending_is_my_video_enabled = false;
  group_call->have_pending_is_my_video_enabled = true;
  group_call->is_my_video_paused = false;
  group_call->have_pending_is_my_video_paused = true;
  try_clear_group_call_participants(input_group_call_id);
  send_update_group_call(group_call, "leave_group_call");

  process_group_call_after_join_requests(input_group_call_id, "leave_group_call 3");

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

void GroupCallManager::clear_group_call(GroupCall *group_call) {
  if (group_call->is_conference) {
    tde2e_api::key_destroy(group_call->private_key_id);
    tde2e_api::key_destroy(group_call->public_key_id);
    tde2e_api::call_destroy(group_call->call_id);
    set_blockchain_participant_ids(group_call, {});
    if (!get_emojis_fingerprint(group_call).empty()) {
      send_closure(G()->td(), &Td::send_update,
                   td_api::make_object<td_api::updateGroupCallVerificationState>(
                       group_call->group_call_id.get(), group_call->call_verification_state.height, vector<string>()));
    }

    group_call->private_key_id = {};
    group_call->public_key_id = {};
    group_call->call_id = {};
    group_call->block_next_offset[0] = -1;
    group_call->block_next_offset[1] = -1;
    group_call->call_verification_state = {};

    poll_group_call_blocks_timeout_.cancel_timeout(group_call->group_call_id.get() * 2);
    poll_group_call_blocks_timeout_.cancel_timeout(group_call->group_call_id.get() * 2 + 1);
  }
  fail_promises(group_call->after_join, Status::Error(400, "GROUPCALL_JOIN_MISSING"));
  check_group_call_is_joined_timeout_.cancel_timeout(group_call->group_call_id.get());
  auto input_group_call_id = get_input_group_call_id(group_call->group_call_id).ok();
  try_clear_group_call_participants(input_group_call_id);
  group_call->old_messages.clear();
}

void GroupCallManager::on_group_call_left(InputGroupCallId input_group_call_id, int32 audio_source, bool need_rejoin) {
  if (G()->close_flag()) {
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  if (group_call->is_joined && group_call->audio_source == audio_source) {
    on_group_call_left_impl(group_call, need_rejoin, "on_group_call_left");
    send_update_group_call(group_call, "on_group_call_left");
  }
}

void GroupCallManager::on_group_call_left_impl(GroupCall *group_call, bool need_rejoin, const char *source) {
  CHECK(group_call != nullptr && group_call->is_inited && group_call->is_joined);
  LOG(INFO) << "Leave " << group_call->group_call_id << " in " << group_call->dialog_id
            << " with need_rejoin = " << need_rejoin << " from " << source;
  group_call->is_joined = false;
  group_call->need_rejoin = need_rejoin && !group_call->is_being_left;
  if (group_call->need_rejoin && group_call->dialog_id.is_valid()) {
    auto dialog_id = group_call->dialog_id;
    if (!td_->dialog_manager_->have_input_peer(dialog_id, false, AccessRights::Read) ||
        (dialog_id.get_type() == DialogType::Chat &&
         !td_->chat_manager_->get_chat_status(dialog_id.get_chat_id()).is_member())) {
      group_call->need_rejoin = false;
    }
  }
  group_call->is_being_left = false;
  group_call->is_speaking = false;
  group_call->is_my_video_paused = false;
  group_call->is_my_video_enabled = false;
  group_call->is_my_presentation_paused = false;
  group_call->have_pending_is_my_video_enabled = false;
  group_call->have_pending_is_my_video_paused = false;
  if (!group_call->is_active) {
    group_call->can_be_managed = false;
  }
  group_call->joined_date = 0;
  group_call->audio_source = 0;

  clear_group_call(group_call);
}

void GroupCallManager::discard_group_call(GroupCallId group_call_id, Promise<Unit> &&promise) {
  TRY_RESULT_PROMISE(promise, input_group_call_id, get_input_group_call_id(group_call_id));
  td_->create_handler<DiscardGroupCallQuery>(std::move(promise))->send(input_group_call_id);
}

void GroupCallManager::on_update_group_call_connection(string &&connection_params) {
  if (!pending_group_call_join_params_.empty()) {
    LOG(ERROR) << "Receive duplicate connection params";
  }
  if (connection_params.empty()) {
    LOG(ERROR) << "Receive empty connection params";
  }
  pending_group_call_join_params_ = std::move(connection_params);
}

void GroupCallManager::on_update_group_call_chain_blocks(InputGroupCallId input_group_call_id, int32 sub_chain_id,
                                                         vector<string> &&blocks, int32 next_offset) {
  if (sub_chain_id != 0 && sub_chain_id != 1) {
    LOG(ERROR) << "Receive blocks in subchain " << sub_chain_id << " of " << input_group_call_id;
    return;
  }
  if (next_offset < 0) {
    LOG(ERROR) << "Receive next offset = " << next_offset;
    return;
  }
  if (pending_join_requests_.count(input_group_call_id) != 0 && !pending_group_call_join_params_.empty()) {
    if (sub_chain_id == 0 && blocks.empty()) {
      LOG(ERROR) << "Receive no join blocks for " << sub_chain_id << " of " << input_group_call_id;
      return;
    }
    auto &data = being_joined_call_blocks_[input_group_call_id];
    if (data.is_inited_[sub_chain_id]) {
      LOG(ERROR) << "Receive duplicate blocks for sub_chain_id = " << sub_chain_id << " of " << input_group_call_id;
    }
    data.is_inited_[sub_chain_id] = true;
    data.blocks_[sub_chain_id] = std::move(blocks);
    data.next_offset_[sub_chain_id] = next_offset;
    return;
  }

  auto *group_call = get_group_call(input_group_call_id);
  if (group_call == nullptr || !group_call->is_inited || !group_call->is_active || !group_call->is_joined ||
      group_call->is_being_left || blocks.empty()) {
    return;
  }
  if (!group_call->is_conference || group_call->call_id == tde2e_api::CallId()) {
    LOG(ERROR) << "Receive a block in " << sub_chain_id << " of " << input_group_call_id;
    return;
  }
  auto added_blocks = next_offset - group_call->block_next_offset[sub_chain_id];
  if (added_blocks <= 0) {
    return;
  }
  if (added_blocks <= static_cast<int32>(blocks.size())) {
    if (sub_chain_id == 0) {
      for (size_t i = blocks.size() - added_blocks; i < blocks.size(); i++) {
        tde2e_api::call_apply_block(group_call->call_id, blocks[i]);
      }
      on_call_state_updated(group_call, "on_update_group_call_chain_blocks");
    } else {
      for (size_t i = blocks.size() - added_blocks; i < blocks.size(); i++) {
        tde2e_api::call_receive_inbound_message(group_call->call_id, blocks[i]);
      }
    }
    group_call->block_next_offset[sub_chain_id] = next_offset;
    poll_group_call_blocks_timeout_.set_timeout_in(group_call->group_call_id.get() * 2 + sub_chain_id,
                                                   GROUP_CALL_BLOCK_POLL_TIMEOUT);
    on_call_verification_state_updated(group_call);

    if (blocks.size() == BLOCK_POLL_COUNT) {
      poll_group_call_blocks(group_call, sub_chain_id);
    }
  } else {
    poll_group_call_blocks(group_call, sub_chain_id);
  }
}

void GroupCallManager::poll_group_call_blocks(GroupCall *group_call, int32 sub_chain_id) {
  CHECK(group_call != nullptr);
  if (group_call->is_blockchain_being_polled[sub_chain_id]) {
    return;
  }
  group_call->is_blockchain_being_polled[sub_chain_id] = true;

  auto group_call_id = group_call->group_call_id;
  poll_group_call_blocks_timeout_.cancel_timeout(group_call_id.get() * 2 + sub_chain_id);

  auto input_group_call_id = get_input_group_call_id(group_call_id).move_as_ok();
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), input_group_call_id, sub_chain_id](Unit) {
    send_closure(actor_id, &GroupCallManager::on_poll_group_call_blocks, input_group_call_id, sub_chain_id);
  });
  td_->create_handler<GetGroupCallChainBlocksQuery>(std::move(promise))
      ->send(input_group_call_id, sub_chain_id, group_call->block_next_offset[sub_chain_id],
             static_cast<int32>(BLOCK_POLL_COUNT));
}

void GroupCallManager::on_poll_group_call_blocks(InputGroupCallId input_group_call_id, int32 sub_chain_id) {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr);
  if (!group_call->is_active) {
    return;
  }
  CHECK(group_call->is_blockchain_being_polled[sub_chain_id]);
  group_call->is_blockchain_being_polled[sub_chain_id] = false;
  poll_group_call_blocks_timeout_.set_timeout_in(group_call->group_call_id.get() * 2 + sub_chain_id,
                                                 GROUP_CALL_BLOCK_POLL_TIMEOUT);
}

InputGroupCallId GroupCallManager::on_update_group_call(
    telegram_api::object_ptr<telegram_api::GroupCall> group_call_ptr, DialogId dialog_id, bool is_live_story) {
  if (td_->auth_manager_->is_bot()) {
    return {};
  }
  if (dialog_id != DialogId() && !dialog_id.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(group_call_ptr) << " in invalid " << dialog_id;
    dialog_id = DialogId();
  }
  auto input_group_call_id = update_group_call(group_call_ptr, dialog_id, is_live_story);
  if (input_group_call_id.is_valid()) {
    LOG(INFO) << "Update " << input_group_call_id << " from " << dialog_id;
  } else {
    LOG(ERROR) << "Receive invalid " << to_string(group_call_ptr);
  }
  return input_group_call_id;
}

void GroupCallManager::on_update_group_call_message_limits(telegram_api::object_ptr<telegram_api::JSONValue> limits) {
  GroupCallMessageLimits new_limits(std::move(limits));
  if (message_limits_ == new_limits) {
    return;
  }
  message_limits_ = new_limits;
  send_closure(G()->td(), &Td::send_update, message_limits_.get_update_group_call_message_levels_object());
  G()->td_db()->get_binlog_pmc()->set("group_call_message_limits", log_event_store(message_limits_).as_slice().str());
}

bool GroupCallManager::try_clear_group_call_participants(InputGroupCallId input_group_call_id) {
  auto *group_call = get_group_call(input_group_call_id);
  if (need_group_call_participants(input_group_call_id, group_call)) {
    return false;
  }
  if (group_call != nullptr) {
    update_group_call_participant_order_timeout_.cancel_timeout(group_call->group_call_id.get());
    remove_recent_group_call_speaker(input_group_call_id, group_call->as_dialog_id);

    LOG(INFO) << "Delete all group call messages";
    on_group_call_messages_deleted(group_call, group_call->messages.delete_all_messages());

    td_->star_manager_->add_pending_owned_star_count(group_call->pending_reaction_star_count, false);
  }

  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return false;
  }

  auto participants = std::move(participants_it->second);
  CHECK(participants != nullptr);
  group_call_participants_.erase(participants_it);

  CHECK(group_call != nullptr && group_call->is_inited);
  LOG(INFO) << "Clear participants in " << input_group_call_id << " from " << group_call->dialog_id;
  bool need_update = false;
  if (group_call->loaded_all_participants) {
    group_call->loaded_all_participants = false;
    need_update = true;
  }
  group_call->leave_version = group_call->version;
  group_call->need_syncing_participants = false;
  group_call->version = -1;

  for (auto &participant : participants->participants) {
    if (participant.order.is_valid()) {
      CHECK(participant.order >= participants->min_order);
      participant.order = GroupCallParticipantOrder();
      send_update_group_call_participant(input_group_call_id, participant, "try_clear_group_call_participants 1");

      if (participant.is_self) {
        need_update |= set_group_call_participant_count(group_call, group_call->participant_count - 1,
                                                        "try_clear_group_call_participants 2");
        if (participant.get_has_video()) {
          need_update |= set_group_call_unmuted_video_count(group_call, group_call->unmuted_video_count - 1,
                                                            "try_clear_group_call_participants 3");
        }
      }
    }
    on_remove_group_call_participant(input_group_call_id, participant.dialog_id);
  }
  participants->local_unmuted_video_count = 0;

  if (group_call_participants_.empty()) {
    CHECK(participant_id_to_group_call_id_.empty());
  }
  return need_update;
}

InputGroupCallId GroupCallManager::update_group_call(const tl_object_ptr<telegram_api::GroupCall> &group_call_ptr,
                                                     DialogId dialog_id, bool is_live_story) {
  CHECK(group_call_ptr != nullptr);

  InputGroupCallId input_group_call_id;
  GroupCall call;
  call.is_inited = true;

  bool is_min = false;
  switch (group_call_ptr->get_id()) {
    case telegram_api::groupCall::ID: {
      auto group_call = static_cast<const telegram_api::groupCall *>(group_call_ptr.get());
      input_group_call_id = InputGroupCallId(group_call->id_, group_call->access_hash_);
      if (group_call->min_) {
        auto old_group_call = get_group_call(input_group_call_id);
        if (old_group_call == nullptr || !old_group_call->is_inited) {
          return input_group_call_id;
        }
        is_min = true;
      }
      call.is_active = true;
      call.is_conference = group_call->conference_;
      call.is_rtmp_stream = group_call->rtmp_stream_;
      call.is_creator = group_call->creator_;
      call.has_hidden_listeners = group_call->listeners_hidden_;
      call.title = group_call->title_;
      call.invite_link = group_call->invite_link_;
      call.paid_message_star_count = StarManager::get_star_count(group_call->send_paid_messages_stars_);
      call.message_sender_dialog_id =
          group_call->default_send_as_ == nullptr ? DialogId() : DialogId(group_call->default_send_as_);
      call.start_subscribed = group_call->schedule_start_subscribed_;
      call.mute_new_participants = group_call->join_muted_;
      call.joined_date_asc = group_call->join_date_asc_;
      call.allowed_toggle_mute_new_participants = group_call->can_change_join_muted_;
      call.are_messages_enabled = group_call->messages_enabled_;
      call.allowed_toggle_are_messages_enabled = group_call->can_change_messages_enabled_;
      call.participant_count = group_call->participants_count_;
      call.unmuted_video_count = group_call->unmuted_video_count_;
      call.unmuted_video_limit = group_call->unmuted_video_limit_;
      if ((group_call->flags_ & telegram_api::groupCall::STREAM_DC_ID_MASK) != 0) {
        call.stream_dc_id = DcId::create(group_call->stream_dc_id_);
        if (!call.stream_dc_id.is_exact()) {
          LOG(ERROR) << "Receive invalid stream DC ID " << call.stream_dc_id << " in " << input_group_call_id;
          call.stream_dc_id = DcId();
        }
      } else {
        call.stream_dc_id = DcId();
      }
      if (group_call->record_start_date_ > 0) {
        call.record_start_date = group_call->record_start_date_;
        call.is_video_recorded = group_call->record_video_active_;
      } else {
        call.record_start_date = 0;
        call.is_video_recorded = false;
      }
      if (group_call->schedule_date_ > 0) {
        call.scheduled_start_date = group_call->schedule_date_;
      } else {
        call.scheduled_start_date = 0;
      }
      if (call.scheduled_start_date == 0) {
        call.start_subscribed = false;
      }

      call.version = group_call->version_;
      call.title_version = group_call->version_;
      call.can_enable_video_version = group_call->version_;
      call.start_subscribed_version = group_call->version_;
      call.mute_version = group_call->version_;
      call.are_messages_enabled_version = group_call->version_;
      call.paid_message_star_count_version = group_call->version_;
      call.stream_dc_id_version = group_call->version_;
      call.record_start_date_version = group_call->version_;
      call.scheduled_start_date_version = group_call->version_;
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

  string join_params = std::move(pending_group_call_join_params_);
  pending_group_call_join_params_.clear();

  bool need_update = false;
  auto *group_call = add_group_call(input_group_call_id, dialog_id, is_live_story);
  call.group_call_id = group_call->group_call_id;
  call.dialog_id = dialog_id.is_valid() ? dialog_id : group_call->dialog_id;
  call.is_live_story = group_call->is_live_story;
  call.can_be_managed = call.is_active && !call.is_conference && can_manage_group_call(&call);
  call.can_self_unmute = call.is_active && (!call.mute_new_participants || call.can_be_managed || call.is_creator);
  call.can_choose_message_sender = group_call->can_choose_message_sender;
  if (!group_call->dialog_id.is_valid() && group_call->dialog_id != dialog_id) {
    need_update = true;
    group_call->dialog_id = dialog_id;
  }
  if (!group_call->is_live_story && is_live_story) {
    need_update = true;
    group_call->is_live_story = true;
  }
  if (call.is_active && join_params.empty() && !group_call->is_joined &&
      (group_call->need_rejoin || group_call->is_being_joined)) {
    call.participant_count++;
  }
  if (call.message_sender_dialog_id == DialogId() && call.is_live_story) {
    call.message_sender_dialog_id = td_->dialog_manager_->get_my_dialog_id();
  }
  LOG(INFO) << "Update " << call.group_call_id << " with " << group_call->participant_count
            << " participants and version " << group_call->version;
  if (!group_call->is_inited) {
    call.is_joined = group_call->is_joined;
    call.need_rejoin = group_call->need_rejoin;
    call.is_being_left = group_call->is_being_left;
    call.is_speaking = group_call->is_speaking;
    call.is_my_video_paused = group_call->is_my_video_paused;
    call.is_my_video_enabled = group_call->is_my_video_enabled;
    call.is_my_presentation_paused = group_call->is_my_presentation_paused;
    call.syncing_participants = group_call->syncing_participants;
    call.need_syncing_participants = group_call->need_syncing_participants;
    call.loaded_all_participants = group_call->loaded_all_participants;
    call.audio_source = group_call->audio_source;
    call.as_dialog_id = group_call->as_dialog_id;
    call.messages = std::move(group_call->messages);
    call.old_messages = std::move(group_call->old_messages);
    *group_call = std::move(call);

    need_update = true;
    if (need_group_call_participants(input_group_call_id, group_call)) {
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
      if (!is_min) {
        // always update to an ended non-min call, dropping also is_joined, is_speaking and other local flags
        clear_group_call(group_call);
        *group_call = std::move(call);
        need_update = true;
      }
    } else {
      if (call.is_conference != group_call->is_conference) {
        group_call->is_conference = call.is_conference;
        need_update = true;
      }
      if (call.is_rtmp_stream != group_call->is_rtmp_stream) {
        group_call->is_rtmp_stream = call.is_rtmp_stream;
        need_update = true;
      }
      if (call.is_creator != group_call->is_creator && !is_min) {
        group_call->is_creator = call.is_creator;
        need_update = true;
      }
      if (call.has_hidden_listeners != group_call->has_hidden_listeners) {
        group_call->has_hidden_listeners = call.has_hidden_listeners;
        need_update = true;
      }
      if ((call.unmuted_video_count != group_call->unmuted_video_count ||
           call.unmuted_video_limit != group_call->unmuted_video_limit) &&
          call.can_enable_video_version >= group_call->can_enable_video_version && !is_min) {
        auto old_can_enable_video = get_group_call_can_enable_video(group_call);
        group_call->unmuted_video_count = call.unmuted_video_count;
        group_call->unmuted_video_limit = call.unmuted_video_limit;
        group_call->can_enable_video_version = call.can_enable_video_version;
        if (old_can_enable_video != get_group_call_can_enable_video(group_call)) {
          need_update = true;
        }
      }
      if (call.start_subscribed != group_call->start_subscribed &&
          call.start_subscribed_version >= group_call->start_subscribed_version && !is_min) {
        auto old_start_subscribed = get_group_call_start_subscribed(group_call);
        group_call->start_subscribed = call.start_subscribed;
        group_call->start_subscribed_version = call.start_subscribed_version;
        if (old_start_subscribed != get_group_call_start_subscribed(group_call)) {
          need_update = true;
        }
      }
      auto mute_flags_changed =
          call.mute_new_participants != group_call->mute_new_participants ||
          call.allowed_toggle_mute_new_participants != group_call->allowed_toggle_mute_new_participants;
      if (mute_flags_changed && call.mute_version >= group_call->mute_version && !is_min) {
        auto old_mute_new_participants = get_group_call_mute_new_participants(group_call);
        need_update |= (call.allowed_toggle_mute_new_participants && call.can_be_managed) !=
                       (group_call->allowed_toggle_mute_new_participants && group_call->can_be_managed);
        group_call->mute_new_participants = call.mute_new_participants;
        group_call->allowed_toggle_mute_new_participants = call.allowed_toggle_mute_new_participants;
        group_call->mute_version = call.mute_version;
        if (old_mute_new_participants != get_group_call_mute_new_participants(group_call)) {
          need_update = true;
        }
      }
      if (call.are_messages_enabled != group_call->are_messages_enabled &&
          call.are_messages_enabled_version >= group_call->are_messages_enabled_version) {
        auto old_are_messages_enabled = get_group_call_are_messages_enabled(group_call);
        group_call->are_messages_enabled = call.are_messages_enabled;
        group_call->are_messages_enabled_version = call.are_messages_enabled_version;
        if (old_are_messages_enabled != get_group_call_are_messages_enabled(group_call)) {
          need_update = true;
        }
      }
      if (call.allowed_toggle_are_messages_enabled != group_call->allowed_toggle_are_messages_enabled && !is_min) {
        need_update |= (call.allowed_toggle_are_messages_enabled && call.can_be_managed) !=
                       (group_call->allowed_toggle_are_messages_enabled && group_call->can_be_managed);
        group_call->allowed_toggle_are_messages_enabled = call.allowed_toggle_are_messages_enabled;
      }
      if (call.title != group_call->title && call.title_version >= group_call->title_version) {
        string old_group_call_title = get_group_call_title(group_call);
        group_call->title = std::move(call.title);
        group_call->title_version = call.title_version;
        if (old_group_call_title != get_group_call_title(group_call)) {
          need_update = true;
        }
      }
      if (call.invite_link != group_call->invite_link && !is_min) {
        group_call->invite_link = std::move(call.invite_link);
        need_update = true;
      }
      if (call.paid_message_star_count != group_call->paid_message_star_count &&
          call.paid_message_star_count_version >= group_call->paid_message_star_count_version) {
        auto old_paid_message_star_count = get_group_call_paid_message_star_count(group_call);
        group_call->paid_message_star_count = call.paid_message_star_count;
        group_call->paid_message_star_count_version = call.paid_message_star_count_version;
        if (old_paid_message_star_count != get_group_call_paid_message_star_count(group_call)) {
          need_update = true;
        }
      }
      if (call.message_sender_dialog_id != group_call->message_sender_dialog_id && !is_min) {
        group_call->message_sender_dialog_id = call.message_sender_dialog_id;
        need_update = true;
      }
      if (call.can_be_managed != group_call->can_be_managed && !is_min) {
        group_call->can_be_managed = call.can_be_managed;
        need_update = true;
      }
      if (call.stream_dc_id != group_call->stream_dc_id &&
          call.stream_dc_id_version >= group_call->stream_dc_id_version && !is_min) {
        group_call->stream_dc_id = call.stream_dc_id;
        group_call->stream_dc_id_version = call.stream_dc_id_version;
      }
      // flag call.joined_date_asc must not change
      if ((call.record_start_date != group_call->record_start_date ||
           call.is_video_recorded != group_call->is_video_recorded) &&
          call.record_start_date_version >= group_call->record_start_date_version) {
        int32 old_record_start_date = get_group_call_record_start_date(group_call);
        bool old_is_video_recorded = get_group_call_is_video_recorded(group_call);
        group_call->record_start_date = call.record_start_date;
        group_call->is_video_recorded = call.is_video_recorded;
        group_call->record_start_date_version = call.record_start_date_version;
        if (old_record_start_date != get_group_call_record_start_date(group_call) ||
            old_is_video_recorded != get_group_call_is_video_recorded(group_call)) {
          need_update = true;
        }
      }
      if (call.scheduled_start_date != group_call->scheduled_start_date &&
          call.scheduled_start_date_version >= group_call->scheduled_start_date_version) {
        LOG_IF(ERROR, group_call->scheduled_start_date == 0) << input_group_call_id << " became scheduled";
        group_call->scheduled_start_date = call.scheduled_start_date;
        group_call->scheduled_start_date_version = call.scheduled_start_date_version;
        need_update = true;
      }
      if (call.version > group_call->version) {
        if (group_call->version != -1) {
          // if we know group call version, then update participants only by corresponding updates
          on_receive_group_call_version(input_group_call_id, call.version);
        } else {
          need_update |= set_group_call_participant_count(group_call, call.participant_count, "update_group_call");
          if (need_group_call_participants(input_group_call_id, group_call) && !join_params.empty() &&
              group_call->version == -1) {
            LOG(INFO) << "Init " << call.group_call_id << " version to " << call.version;
            group_call->version = call.version;
            if (process_pending_group_call_participant_updates(input_group_call_id)) {
              need_update = false;
            }
          }
        }
      } else if (call.version == group_call->version) {
        set_group_call_participant_count(group_call, call.participant_count, "update_group_call fix");
        need_update = true;
      }
    }
  }
  if (!group_call->is_active && group_call_recent_speakers_.erase(group_call->group_call_id) != 0) {
    need_update = true;
  }
  if (!join_params.empty()) {
    need_update |= on_join_group_call_response(input_group_call_id, std::move(join_params));
  } else if (being_joined_call_blocks_.erase(input_group_call_id) != 0) {
    LOG(ERROR) << "Ignore blocks for " << input_group_call_id;
  }
  update_group_call_dialog(group_call, "update_group_call", false);  // must be after join response is processed
  need_update |= try_clear_group_call_participants(input_group_call_id);
  if (need_update) {
    send_update_group_call(group_call, "update_group_call");
  }
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
  auto *group_call_participants = add_group_call_participants(input_group_call_id, "on_receive_group_call_version");
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

  on_user_speaking_in_group_call(group_call->group_call_id, participant.dialog_id, participant.server_is_muted_by_admin,
                                 active_date, !participant.is_min);
}

void GroupCallManager::on_user_speaking_in_group_call(GroupCallId group_call_id, DialogId dialog_id,
                                                      bool is_muted_by_admin, int32 date, bool is_recursive) {
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
  if (group_call->has_hidden_listeners && is_muted_by_admin) {
    return;
  }
  if (group_call->is_live_story) {
    return;
  }

  if (!td_->dialog_manager_->have_dialog_info_force(dialog_id, "on_user_speaking_in_group_call") ||
      (!is_recursive && need_group_call_participants(input_group_call_id, group_call) &&
       get_group_call_participant(input_group_call_id, dialog_id, "on_user_speaking_in_group_call") == nullptr)) {
    if (is_recursive) {
      LOG(ERROR) << "Failed to find speaking " << dialog_id << " from " << input_group_call_id;
    } else {
      auto query_promise = PromiseCreator::lambda(
          [actor_id = actor_id(this), group_call_id, dialog_id, is_muted_by_admin, date](Result<Unit> &&result) {
            if (!G()->close_flag() && result.is_ok()) {
              send_closure(actor_id, &GroupCallManager::on_user_speaking_in_group_call, group_call_id, dialog_id,
                           is_muted_by_admin, date, true);
            }
          });
      vector<tl_object_ptr<telegram_api::InputPeer>> input_peers;
      input_peers.push_back(DialogManager::get_input_peer_force(dialog_id));
      td_->create_handler<GetGroupCallParticipantQuery>(std::move(query_promise))
          ->send(input_group_call_id, std::move(input_peers), {});
    }
    return;
  }

  LOG(INFO) << "Add " << dialog_id << " as recent speaker at " << date << " in " << input_group_call_id;
  auto &recent_speakers = group_call_recent_speakers_[group_call_id];
  if (recent_speakers == nullptr) {
    recent_speakers = make_unique<GroupCallRecentSpeakers>();
  }

  for (size_t i = 0; i < recent_speakers->users.size(); i++) {
    if (recent_speakers->users[i].first == dialog_id) {
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
      if (dialog_id.get_type() != DialogType::User) {
        td_->dialog_manager_->force_create_dialog(dialog_id, "on_user_speaking_in_group_call", true);
      }
      recent_speakers->users.insert(recent_speakers->users.begin() + i, {dialog_id, date});
      break;
    }
  }
  static constexpr size_t MAX_RECENT_SPEAKERS = 3;
  if (recent_speakers->users.size() > MAX_RECENT_SPEAKERS) {
    recent_speakers->users.pop_back();
  }

  on_group_call_recent_speakers_updated(group_call, recent_speakers.get());
}

void GroupCallManager::remove_recent_group_call_speaker(InputGroupCallId input_group_call_id, DialogId dialog_id) {
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
    if (recent_speakers->users[i].first == dialog_id) {
      LOG(INFO) << "Remove " << dialog_id << " from recent speakers in " << input_group_call_id << " from "
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
    if (group_call != nullptr) {
      LOG(INFO) << "Don't need to send update of recent speakers in " << group_call->group_call_id << " from "
                << group_call->dialog_id;
    }
    return;
  }

  recent_speakers->is_changed = true;

  LOG(INFO) << "Schedule update of recent speakers in " << group_call->group_call_id << " from "
            << group_call->dialog_id;
  const double MAX_RECENT_SPEAKER_UPDATE_DELAY = 0.5;
  recent_speaker_update_timeout_.set_timeout_in(group_call->group_call_id.get(), MAX_RECENT_SPEAKER_UPDATE_DELAY);
}

DialogId GroupCallManager::set_group_call_participant_is_speaking_by_source(InputGroupCallId input_group_call_id,
                                                                            int32 audio_source, bool is_speaking,
                                                                            int32 date) {
  CHECK(audio_source != 0);
  auto participants_it = group_call_participants_.find(input_group_call_id);
  if (participants_it == group_call_participants_.end()) {
    return DialogId();
  }

  for (auto &participant : participants_it->second->participants) {
    if (participant.audio_source == audio_source || participant.presentation_audio_source == audio_source) {
      if (is_speaking && participant.get_is_muted_by_admin()) {
        // don't allow to show as speaking muted by admin participants
        return DialogId();
      }
      if (participant.is_speaking != is_speaking) {
        participant.is_speaking = is_speaking;
        if (is_speaking) {
          participant.local_active_date = max(participant.local_active_date, date);
        }
        bool my_can_self_unmute = get_group_call_can_self_unmute(input_group_call_id);
        auto old_order = participant.order;
        participant.order = get_real_participant_order(my_can_self_unmute, participant, participants_it->second.get());
        if (participant.order.is_valid() || old_order.is_valid()) {
          send_update_group_call_participant(input_group_call_id, participant,
                                             "set_group_call_participant_is_speaking_by_source");
        }
      }

      return participant.dialog_id;
    }
  }
  return DialogId();
}

bool GroupCallManager::set_group_call_participant_count(GroupCall *group_call, int32 count, const char *source,
                                                        bool force_update) {
  CHECK(group_call != nullptr);
  CHECK(group_call->is_inited);
  if (group_call->participant_count == count) {
    return false;
  }

  LOG(DEBUG) << "Set " << group_call->group_call_id << " participant count to " << count << " from " << source;
  auto input_group_call_id = get_input_group_call_id(group_call->group_call_id).ok();
  if (count < 0) {
    LOG(ERROR) << "Participant count became negative in " << group_call->group_call_id << " in "
               << group_call->dialog_id << " from " << source;
    count = 0;
    reload_group_call(input_group_call_id, Auto());
  }

  bool result = false;
  if (need_group_call_participants(input_group_call_id, group_call)) {
    auto known_participant_count = static_cast<int32>(
        add_group_call_participants(input_group_call_id, "set_group_call_participant_count")->participants.size());
    if (count < known_participant_count) {
      if (group_call->is_joined) {
        LOG(ERROR) << "Participant count became " << count << " from " << source << ", which is less than known "
                   << known_participant_count << " number of participants in " << input_group_call_id << " from "
                   << group_call->dialog_id;
      }
      count = known_participant_count;
    } else if (group_call->loaded_all_participants && !group_call->has_hidden_listeners &&
               count > known_participant_count) {
      if (group_call->joined_date_asc) {
        group_call->loaded_all_participants = false;
        result = true;
      } else {
        count = known_participant_count;
      }
    }
  }

  if (group_call->participant_count == count) {
    return result;
  }

  group_call->participant_count = count;
  update_group_call_dialog(group_call, source, force_update);
  return true;
}

bool GroupCallManager::set_group_call_unmuted_video_count(GroupCall *group_call, int32 count, const char *source) {
  CHECK(group_call != nullptr);
  CHECK(group_call->is_inited);

  auto participants_it = group_call_participants_.find(get_input_group_call_id(group_call->group_call_id).ok());
  if (participants_it != group_call_participants_.end()) {
    auto group_call_participants = participants_it->second.get();
    CHECK(group_call_participants != nullptr);
    CHECK(group_call_participants->local_unmuted_video_count >= 0);
    CHECK(static_cast<size_t>(group_call_participants->local_unmuted_video_count) <=
          group_call_participants->participants.size());
    if (group_call->loaded_all_participants || !group_call_participants->min_order.has_video()) {
      if (group_call_participants->local_unmuted_video_count != count &&
          group_call->unmuted_video_count != group_call_participants->local_unmuted_video_count) {
        LOG(INFO) << "Use local count " << group_call_participants->local_unmuted_video_count
                  << " of unmuted videos instead of " << count;
      }
      count = group_call_participants->local_unmuted_video_count;
    }
  }

  if (count < 0) {
    LOG(ERROR) << "Video participant count became negative in " << group_call->group_call_id << " in "
               << group_call->dialog_id << " from " << source;
    count = 0;
    auto input_group_call_id = get_input_group_call_id(group_call->group_call_id).ok();
    reload_group_call(input_group_call_id, Auto());
  }

  if (group_call->unmuted_video_count == count) {
    return false;
  }

  LOG(DEBUG) << "Set " << group_call->group_call_id << " video participant count to " << count << " from " << source;

  auto old_can_enable_video = get_group_call_can_enable_video(group_call);
  group_call->unmuted_video_count = count;
  return old_can_enable_video != get_group_call_can_enable_video(group_call);
}

void GroupCallManager::update_group_call_dialog(const GroupCall *group_call, const char *source, bool force) {
  CHECK(group_call != nullptr);
  if (!group_call->dialog_id.is_valid()) {
    return;
  }
  if (group_call->is_live_story) {
    if (group_call->is_active) {
      dialog_live_stories_[group_call->dialog_id] = get_input_group_call_id(group_call->group_call_id).ok();
    }
    return;
  }

  td_->messages_manager_->on_update_dialog_group_call(group_call->dialog_id, group_call->is_active,
                                                      group_call->participant_count == 0, source, force);
}

void GroupCallManager::on_call_state_updated(GroupCall *group_call, const char *source) {
  CHECK(group_call != nullptr);
  CHECK(group_call->call_id != tde2e_api::CallId());
  auto r_state = tde2e_api::call_get_state(group_call->call_id);
  if (r_state.is_error()) {
    LOG(INFO) << "State of " << group_call->group_call_id << " has error " << static_cast<int>(r_state.error().code)
              << " : " << r_state.error().message << " from " << source;
    leave_group_call(group_call->group_call_id, Auto());
    return;
  }
  auto &state = r_state.value();
  auto participant_ids = transform(state.participants, [](const auto &participant) { return participant.user_id; });
  if (!td::contains(participant_ids, td_->user_manager_->get_my_id().get())) {
    LOG(INFO) << "State of " << group_call->group_call_id << " doesn't contain the current user";
    leave_group_call(group_call->group_call_id, Auto());
    return;
  }
  set_blockchain_participant_ids(group_call, std::move(participant_ids));
}

void GroupCallManager::set_blockchain_participant_ids(GroupCall *group_call, vector<int64> participant_ids) {
  std::sort(participant_ids.begin(), participant_ids.end());
  if (group_call->blockchain_participant_ids == participant_ids) {
    return;
  }
  group_call->blockchain_participant_ids = participant_ids;
  for (auto participant_id : participant_ids) {
    auto user_id = UserId(participant_id);
    if (user_id.is_valid()) {
      td_->user_manager_->have_user_force(user_id, "on_call_state_updated");
    }
  }
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateGroupCallParticipants>(group_call->group_call_id.get(),
                                                                        std::move(participant_ids)));
}

vector<string> GroupCallManager::get_emojis_fingerprint(const GroupCall *group_call) {
  const auto &o_emoji_hash = group_call->call_verification_state.emoji_hash;
  if (!o_emoji_hash || o_emoji_hash.value().size() < 32u) {
    return vector<string>();
  }
  return get_emoji_fingerprints(Slice(o_emoji_hash.value()).ubegin());
}

void GroupCallManager::on_call_verification_state_updated(GroupCall *group_call) {
  send_outbound_group_call_blockchain_messages(group_call);
  CHECK(group_call != nullptr);
  CHECK(group_call->call_id != tde2e_api::CallId());
  auto r_state = tde2e_api::call_get_verification_state(group_call->call_id);
  if (r_state.is_error()) {
    return;
  }
  auto &state = r_state.value();
  if (state.height == group_call->call_verification_state.height &&
      state.emoji_hash == group_call->call_verification_state.emoji_hash) {
    return;
  }
  group_call->call_verification_state = std::move(state);
  send_closure(G()->td(), &Td::send_update,
               td_api::make_object<td_api::updateGroupCallVerificationState>(
                   group_call->group_call_id.get(), state.height, get_emojis_fingerprint(group_call)));
}

void GroupCallManager::send_outbound_group_call_blockchain_messages(GroupCall *group_call) {
  CHECK(group_call != nullptr);
  CHECK(group_call->call_id != tde2e_api::CallId());
  auto r_queries = tde2e_api::call_pull_outbound_messages(group_call->call_id);
  if (r_queries.is_error()) {
    return;
  }

  for (auto &query : r_queries.value()) {
    auto input_group_call_id = get_input_group_call_id(group_call->group_call_id).move_as_ok();
    td_->create_handler<SendConferenceCallBroadcastQuery>()->send(input_group_call_id, query);
  }
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

  vector<std::pair<DialogId, bool>> recent_speaker_users;
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

  auto get_result = [recent_speaker_users, td = td_] {
    return transform(recent_speaker_users, [td](const std::pair<DialogId, bool> &recent_speaker_user) {
      return td_api::make_object<td_api::groupCallRecentSpeaker>(
          get_message_sender_object(td, recent_speaker_user.first, "get_recent_speakers"), recent_speaker_user.second);
    });
  };
  if (recent_speakers->last_sent_users != recent_speaker_users) {
    recent_speakers->last_sent_users = std::move(recent_speaker_users);

    if (!for_update) {
      // the change must be received through update first
      LOG(INFO) << "Send update about " << group_call->group_call_id << " from get_recent_speakers";
      send_closure(G()->td(), &Td::send_update, get_update_group_call_object(td_, group_call, get_result()));
    }
  }

  return get_result();
}

td_api::object_ptr<td_api::groupCall> GroupCallManager::get_group_call_object(
    Td *td, const GroupCall *group_call, vector<td_api::object_ptr<td_api::groupCallRecentSpeaker>> recent_speakers) {
  CHECK(group_call != nullptr);
  CHECK(group_call->is_inited);

  int32 scheduled_start_date = group_call->scheduled_start_date;
  bool is_active = scheduled_start_date == 0 ? group_call->is_active : false;
  bool is_joined = get_group_call_is_joined(group_call);
  bool start_subscribed = get_group_call_start_subscribed(group_call);
  bool is_my_video_enabled = get_group_call_is_my_video_enabled(group_call);
  bool is_my_video_paused = is_my_video_enabled && get_group_call_is_my_video_paused(group_call);
  bool mute_new_participants = get_group_call_mute_new_participants(group_call);
  bool can_toggle_mute_new_participants =
      group_call->is_active && group_call->can_be_managed && group_call->allowed_toggle_mute_new_participants;
  bool can_enable_video = get_group_call_can_enable_video(group_call);
  bool are_messages_enabled = get_group_call_are_messages_enabled(group_call);
  bool can_send_messages =
      are_messages_enabled || (group_call->is_active && group_call->is_live_story && group_call->can_be_managed);
  bool can_toggle_are_messages_enabled =
      group_call->is_active && group_call->can_be_managed && group_call->allowed_toggle_are_messages_enabled;
  bool can_delete_messages = get_group_call_can_delete_messages(group_call);
  auto paid_message_star_count = get_group_call_paid_message_star_count(group_call);
  int32 record_start_date = get_group_call_record_start_date(group_call);
  int32 record_duration = record_start_date == 0 ? 0 : max(G()->unix_time() - record_start_date + 1, 1);
  bool is_video_recorded = get_group_call_is_video_recorded(group_call);
  td_api::object_ptr<td_api::MessageSender> message_sender_id;
  if (group_call->is_live_story) {
    CHECK(group_call->message_sender_dialog_id.is_valid());
    message_sender_id = get_message_sender_object(td, group_call->message_sender_dialog_id, "groupCall");
  }
  return td_api::make_object<td_api::groupCall>(
      group_call->group_call_id.get(), group_call->input_group_call_id.get_group_call_id(),
      get_group_call_title(group_call), group_call->invite_link, paid_message_star_count, scheduled_start_date,
      start_subscribed, is_active, !group_call->is_conference && !group_call->is_live_story, group_call->is_live_story,
      !group_call->is_conference && group_call->is_rtmp_stream, is_joined, group_call->need_rejoin,
      group_call->is_creator && !group_call->is_live_story, group_call->can_be_managed, group_call->participant_count,
      group_call->has_hidden_listeners || group_call->is_live_story,
      group_call->loaded_all_participants || group_call->is_live_story, std::move(message_sender_id),
      std::move(recent_speakers), is_my_video_enabled, is_my_video_paused, can_enable_video, mute_new_participants,
      can_toggle_mute_new_participants, can_send_messages, are_messages_enabled, can_toggle_are_messages_enabled,
      can_delete_messages, record_duration, is_video_recorded, group_call->duration);
}

td_api::object_ptr<td_api::updateGroupCall> GroupCallManager::get_update_group_call_object(
    Td *td, const GroupCall *group_call, vector<td_api::object_ptr<td_api::groupCallRecentSpeaker>> recent_speakers) {
  return td_api::make_object<td_api::updateGroupCall>(
      get_group_call_object(td, group_call, std::move(recent_speakers)));
}

td_api::object_ptr<td_api::updateGroupCallParticipant> GroupCallManager::get_update_group_call_participant_object(
    GroupCallId group_call_id, const GroupCallParticipant &participant) {
  return td_api::make_object<td_api::updateGroupCallParticipant>(group_call_id.get(),
                                                                 participant.get_group_call_participant_object(td_));
}

void GroupCallManager::send_update_group_call(const GroupCall *group_call, const char *source) {
  LOG(INFO) << "Send update about " << group_call->group_call_id << " from " << source;
  send_closure(G()->td(), &Td::send_update,
               get_update_group_call_object(td_, group_call, get_recent_speakers(group_call, true)));
}

void GroupCallManager::send_update_group_call_participant(GroupCallId group_call_id,
                                                          const GroupCallParticipant &participant, const char *source) {
  LOG(INFO) << "Send update about " << participant << " in " << group_call_id << " from " << source;
  send_closure(G()->td(), &Td::send_update, get_update_group_call_participant_object(group_call_id, participant));
}

void GroupCallManager::send_update_group_call_participant(InputGroupCallId input_group_call_id,
                                                          const GroupCallParticipant &participant, const char *source) {
  auto *group_call = get_group_call(input_group_call_id);
  CHECK(group_call != nullptr && group_call->is_inited);
  send_update_group_call_participant(group_call->group_call_id, participant, source);
}

void GroupCallManager::register_group_call(MessageFullId message_full_id, const char *source) {
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(message_full_id.get_message_id().is_server());
  LOG(INFO) << "Register group call " << message_full_id << " from " << source;
  auto &call_id = group_call_messages_[message_full_id];
  if (call_id == 0) {
    call_id = ++current_call_id_;
    group_call_message_full_ids_[call_id] = message_full_id;
  }
  update_group_call_timeout_.add_timeout_in(call_id, 0);
}

void GroupCallManager::unregister_group_call(MessageFullId message_full_id, const char *source) {
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(message_full_id.get_message_id().is_server());
  LOG(INFO) << "Unregister group call " << message_full_id << " from " << source;
  auto it = group_call_messages_.find(message_full_id);
  CHECK(it != group_call_messages_.end());
  auto call_id = it->second;
  group_call_messages_.erase(it);
  auto is_deleted = group_call_message_full_ids_.erase(call_id) > 0;
  LOG_CHECK(is_deleted) << source << ' ' << message_full_id;
  update_group_call_timeout_.cancel_timeout(call_id, "unregister_group_call");
}

void GroupCallManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  updates.push_back(message_limits_.get_update_group_call_message_levels_object());
}

}  // namespace td
