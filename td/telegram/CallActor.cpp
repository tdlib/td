//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CallActor.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DhCache.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/algorithm.h"
#include "td/utils/as.h"
#include "td/utils/bits.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

#include <tuple>
#include <unordered_set>

namespace td {

CallProtocol::CallProtocol(const telegram_api::phoneCallProtocol &protocol)
    : udp_p2p(protocol.udp_p2p_)
    , udp_reflector(protocol.udp_reflector_)
    , min_layer(protocol.min_layer_)
    , max_layer(protocol.max_layer_)
    , library_versions(protocol.library_versions_) {
}

tl_object_ptr<telegram_api::phoneCallProtocol> CallProtocol::get_input_phone_call_protocol() const {
  int32 flags = 0;
  if (udp_p2p) {
    flags |= telegram_api::phoneCallProtocol::UDP_P2P_MASK;
  }
  if (udp_reflector) {
    flags |= telegram_api::phoneCallProtocol::UDP_REFLECTOR_MASK;
  }
  return make_tl_object<telegram_api::phoneCallProtocol>(flags, udp_p2p, udp_reflector, min_layer, max_layer,
                                                         vector<string>(library_versions));
}

CallProtocol::CallProtocol(const td_api::callProtocol &protocol)
    : udp_p2p(protocol.udp_p2p_)
    , udp_reflector(protocol.udp_reflector_)
    , min_layer(protocol.min_layer_)
    , max_layer(protocol.max_layer_)
    , library_versions(protocol.library_versions_) {
}

CallConnection::CallConnection(const telegram_api::PhoneConnection &connection) {
  switch (connection.get_id()) {
    case telegram_api::phoneConnection::ID: {
      auto &conn = static_cast<const telegram_api::phoneConnection &>(connection);
      type = Type::Telegram;
      id = conn.id_;
      ip = conn.ip_;
      ipv6 = conn.ipv6_;
      port = conn.port_;
      peer_tag = conn.peer_tag_.as_slice().str();
      break;
    }
    case telegram_api::phoneConnectionWebrtc::ID: {
      auto &conn = static_cast<const telegram_api::phoneConnectionWebrtc &>(connection);
      type = Type::Webrtc;
      id = conn.id_;
      ip = conn.ip_;
      ipv6 = conn.ipv6_;
      port = conn.port_;
      username = conn.username_;
      password = conn.password_;
      supports_turn = conn.turn_;
      supports_stun = conn.stun_;
      break;
    }
    default:
      UNREACHABLE();
  }
}

tl_object_ptr<td_api::callProtocol> CallProtocol::get_call_protocol_object() const {
  return make_tl_object<td_api::callProtocol>(udp_p2p, udp_reflector, min_layer, max_layer,
                                              vector<string>(library_versions));
}

tl_object_ptr<td_api::callServer> CallConnection::get_call_server_object() const {
  auto server_type = [&]() -> tl_object_ptr<td_api::CallServerType> {
    switch (type) {
      case Type::Telegram:
        return make_tl_object<td_api::callServerTypeTelegramReflector>(peer_tag);
      case Type::Webrtc:
        return make_tl_object<td_api::callServerTypeWebrtc>(username, password, supports_turn, supports_stun);
      default:
        UNREACHABLE();
        return nullptr;
    }
  }();
  return make_tl_object<td_api::callServer>(id, ip, ipv6, port, std::move(server_type));
}

tl_object_ptr<td_api::CallState> CallState::get_call_state_object() const {
  switch (type) {
    case Type::Pending:
      return make_tl_object<td_api::callStatePending>(is_created, is_received);
    case Type::ExchangingKey:
      return make_tl_object<td_api::callStateExchangingKeys>();
    case Type::Ready: {
      auto call_connections = transform(connections, [](auto &c) { return c.get_call_server_object(); });
      return make_tl_object<td_api::callStateReady>(protocol.get_call_protocol_object(), std::move(call_connections),
                                                    config, key, vector<string>(emojis_fingerprint), allow_p2p);
    }
    case Type::HangingUp:
      return make_tl_object<td_api::callStateHangingUp>();
    case Type::Discarded:
      return make_tl_object<td_api::callStateDiscarded>(get_call_discard_reason_object(discard_reason), need_rating,
                                                        need_debug_information);
    case Type::Error:
      CHECK(error.is_error());
      return make_tl_object<td_api::callStateError>(make_tl_object<td_api::error>(error.code(), error.message().str()));
    case Type::Empty:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

CallActor::CallActor(CallId call_id, ActorShared<> parent, Promise<int64> promise)
    : parent_(std::move(parent)), call_id_promise_(std::move(promise)), local_call_id_(call_id) {
}

void CallActor::create_call(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
                            CallProtocol &&protocol, bool is_video, Promise<CallId> &&promise) {
  CHECK(state_ == State::Empty);
  state_ = State::SendRequestQuery;
  is_outgoing_ = true;
  is_video_ = is_video;
  user_id_ = user_id;
  input_user_ = std::move(input_user);
  call_state_.protocol = std::move(protocol);
  call_state_.type = CallState::Type::Pending;
  call_state_.is_received = false;
  call_state_need_flush_ = true;
  loop();
  promise.set_value(CallId(local_call_id_));
}

void CallActor::accept_call(CallProtocol &&protocol, Promise<> promise) {
  if (state_ != State::SendAcceptQuery) {
    return promise.set_error(Status::Error(400, "Unexpected acceptCall"));
  }
  is_accepted_ = true;
  call_state_.protocol = std::move(protocol);
  promise.set_value(Unit());
  loop();
}

void CallActor::update_call_signaling_data(string data) {
  if (call_state_.type != CallState::Type::Ready) {
    return;
  }

  auto update = td_api::make_object<td_api::updateNewCallSignalingData>();
  update->call_id_ = local_call_id_.get();
  update->data_ = std::move(data);
  send_closure(G()->td(), &Td::send_update, std::move(update));
}

void CallActor::send_call_signaling_data(string &&data, Promise<> promise) {
  if (call_state_.type != CallState::Type::Ready) {
    return promise.set_error(Status::Error(400, "Call is not active"));
  }

  auto query = G()->net_query_creator().create(
      telegram_api::phone_sendSignalingData(get_input_phone_call("send_call_signaling_data"), BufferSlice(data)));
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([promise = std::move(promise)](Result<NetQueryPtr> r_net_query) mutable {
                      auto res = fetch_result<telegram_api::phone_sendSignalingData>(std::move(r_net_query));
                      if (res.is_error()) {
                        promise.set_error(res.move_as_error());
                      } else {
                        promise.set_value(Unit());
                      }
                    }));
}

void CallActor::discard_call(bool is_disconnected, int32 duration, bool is_video, int64 connection_id,
                             Promise<> promise) {
  promise.set_value(Unit());
  if (state_ == State::Discarded || state_ == State::WaitDiscardResult || state_ == State::SendDiscardQuery) {
    return;
  }
  is_video_ |= is_video;

  if (state_ == State::WaitRequestResult && !request_query_ref_.empty()) {
    LOG(INFO) << "Cancel request call query";
    cancel_query(request_query_ref_);
  }

  switch (call_state_.type) {
    case CallState::Type::Empty:
    case CallState::Type::Pending:
      if (is_outgoing_) {
        call_state_.discard_reason = CallDiscardReason::Missed;
      } else {
        call_state_.discard_reason = CallDiscardReason::Declined;
      }
      break;
    case CallState::Type::ExchangingKey:
      call_state_.discard_reason = is_disconnected ? CallDiscardReason::Disconnected : CallDiscardReason::HungUp;
      break;
    case CallState::Type::Ready:
      call_state_.discard_reason = is_disconnected ? CallDiscardReason::Disconnected : CallDiscardReason::HungUp;
      duration_ = duration;
      connection_id_ = connection_id;
      break;
    case CallState::Type::HangingUp:
    case CallState::Type::Discarded:
    case CallState::Type::Error:
    default:
      UNREACHABLE();
      return;
  }

  call_state_.type = CallState::Type::HangingUp;
  call_state_need_flush_ = true;

  state_ = State::SendDiscardQuery;
  loop();
}

void CallActor::rate_call(int32 rating, string comment, vector<td_api::object_ptr<td_api::CallProblem>> &&problems,
                          Promise<> promise) {
  if (!call_state_.need_rating) {
    return promise.set_error(Status::Error(400, "Unexpected sendCallRating"));
  }
  promise.set_value(Unit());

  if (rating == 5) {
    comment.clear();
  }

  std::unordered_set<string> tags;
  for (auto &problem : problems) {
    if (problem == nullptr) {
      continue;
    }

    const char *tag = [problem_id = problem->get_id()] {
      switch (problem_id) {
        case td_api::callProblemEcho::ID:
          return "echo";
        case td_api::callProblemNoise::ID:
          return "noise";
        case td_api::callProblemInterruptions::ID:
          return "interruptions";
        case td_api::callProblemDistortedSpeech::ID:
          return "distorted_speech";
        case td_api::callProblemSilentLocal::ID:
          return "silent_local";
        case td_api::callProblemSilentRemote::ID:
          return "silent_remote";
        case td_api::callProblemDropped::ID:
          return "dropped";
        case td_api::callProblemDistortedVideo::ID:
          return "distorted_video";
        case td_api::callProblemPixelatedVideo::ID:
          return "pixelated_video";
        default:
          UNREACHABLE();
          return "";
      }
    }();
    if (tags.insert(tag).second) {
      if (!comment.empty()) {
        comment += ' ';
      }
      comment += '#';
      comment += tag;
    }
  }

  auto tl_query = telegram_api::phone_setCallRating(0, false /*ignored*/, get_input_phone_call("rate_call"), rating,
                                                    std::move(comment));
  auto query = G()->net_query_creator().create(tl_query);
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_set_rating_query_result, std::move(r_net_query));
                    }));
  loop();
}

void CallActor::on_set_rating_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_setCallRating>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  call_state_.need_rating = false;
  send_closure(G()->updates_manager(), &UpdatesManager::on_get_updates, res.move_as_ok(), Promise<Unit>());
}

void CallActor::send_call_debug_information(string data, Promise<> promise) {
  if (!call_state_.need_debug_information) {
    return promise.set_error(Status::Error(400, "Unexpected sendCallDebugInformation"));
  }
  promise.set_value(Unit());
  auto tl_query = telegram_api::phone_saveCallDebug(get_input_phone_call("send_call_debug_information"),
                                                    make_tl_object<telegram_api::dataJSON>(std::move(data)));
  auto query = G()->net_query_creator().create(tl_query);
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_set_debug_query_result, std::move(r_net_query));
                    }));
  loop();
}

void CallActor::on_set_debug_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_saveCallDebug>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  call_state_.need_debug_information = false;
}

// Requests
void CallActor::update_call(tl_object_ptr<telegram_api::PhoneCall> call) {
  LOG(INFO) << "Receive " << to_string(call);
  Status status;
  downcast_call(*call, [&](auto &call) { status = this->do_update_call(call); });
  if (status.is_error()) {
    LOG(INFO) << "Receive error " << status << ", while handling update " << to_string(call);
    on_error(std::move(status));
  }
  loop();
}

void CallActor::update_call_inner(tl_object_ptr<telegram_api::phone_phoneCall> call) {
  LOG(INFO) << "Update call with " << to_string(call);
  send_closure(G()->contacts_manager(), &ContactsManager::on_get_users, std::move(call->users_), "UpdatePhoneCall");
  update_call(std::move(call->phone_call_));
}

Status CallActor::do_update_call(telegram_api::phoneCallEmpty &call) {
  return Status::Error(400, "Call is finished");
}

Status CallActor::do_update_call(telegram_api::phoneCallWaiting &call) {
  if (state_ != State::WaitRequestResult && state_ != State::WaitAcceptResult) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }

  if (state_ == State::WaitAcceptResult) {
    LOG(DEBUG) << "Do update call to Waiting";
    on_begin_exchanging_key();
  } else {
    LOG(DEBUG) << "Do update call to Waiting";
    if ((call.flags_ & telegram_api::phoneCallWaiting::RECEIVE_DATE_MASK) != 0) {
      call_state_.is_received = true;
      call_state_need_flush_ = true;
      int64 call_ring_timeout_ms = G()->shared_config().get_option_integer("call_ring_timeout_ms", 90000);
      set_timeout_in(static_cast<double>(call_ring_timeout_ms) * 0.001);
    }
  }

  call_id_ = call.id_;
  call_access_hash_ = call.access_hash_;
  is_call_id_inited_ = true;
  is_video_ |= (call.flags_ & telegram_api::phoneCallWaiting::VIDEO_MASK) != 0;
  call_admin_id_ = call.admin_id_;
  call_participant_id_ = call.participant_id_;
  if (call_id_promise_) {
    call_id_promise_.set_value(std::move(call.id_));
  }

  if (!call_state_.is_created) {
    call_state_.is_created = true;
    call_state_need_flush_ = true;
  }

  return Status::OK();
}

Status CallActor::do_update_call(telegram_api::phoneCallRequested &call) {
  if (state_ != State::Empty) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }
  LOG(DEBUG) << "Do update call to Requested";
  call_id_ = call.id_;
  call_access_hash_ = call.access_hash_;
  is_call_id_inited_ = true;
  is_video_ |= (call.flags_ & telegram_api::phoneCallRequested::VIDEO_MASK) != 0;
  call_admin_id_ = call.admin_id_;
  call_participant_id_ = call.participant_id_;
  if (call_id_promise_) {
    call_id_promise_.set_value(std::move(call.id_));
  }

  dh_handshake_.set_g_a_hash(call.g_a_hash_.as_slice());
  state_ = State::SendAcceptQuery;

  call_state_.type = CallState::Type::Pending;
  call_state_.is_created = true;
  call_state_.is_received = true;
  call_state_need_flush_ = true;

  send_received_query();
  return Status::OK();
}

tl_object_ptr<telegram_api::inputPhoneCall> CallActor::get_input_phone_call(const char *source) {
  LOG_CHECK(is_call_id_inited_) << source;
  return make_tl_object<telegram_api::inputPhoneCall>(call_id_, call_access_hash_);
}

Status CallActor::do_update_call(telegram_api::phoneCallAccepted &call) {
  if (state_ != State::WaitRequestResult) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }

  LOG(DEBUG) << "Do update call to Accepted";
  if (!is_call_id_inited_) {
    call_id_ = call.id_;
    call_access_hash_ = call.access_hash_;
    is_call_id_inited_ = true;
    call_admin_id_ = call.admin_id_;
    call_participant_id_ = call.participant_id_;
    if (call_id_promise_) {
      call_id_promise_.set_value(std::move(call.id_));
    }
  }
  is_video_ |= (call.flags_ & telegram_api::phoneCallAccepted::VIDEO_MASK) != 0;
  dh_handshake_.set_g_a(call.g_b_.as_slice());
  TRY_STATUS(dh_handshake_.run_checks(true, DhCache::instance()));
  std::tie(call_state_.key_fingerprint, call_state_.key) = dh_handshake_.gen_key();
  state_ = State::SendConfirmQuery;
  on_begin_exchanging_key();
  return Status::OK();
}

void CallActor::on_begin_exchanging_key() {
  call_state_.type = CallState::Type::ExchangingKey;
  call_state_need_flush_ = true;
  int64 call_receive_timeout_ms = G()->shared_config().get_option_integer("call_receive_timeout_ms", 20000);
  double timeout = static_cast<double>(call_receive_timeout_ms) * 0.001;
  LOG(INFO) << "Set call timeout to " << timeout;
  set_timeout_in(timeout);
}

Status CallActor::do_update_call(telegram_api::phoneCall &call) {
  if (state_ != State::WaitAcceptResult && state_ != State::WaitConfirmResult) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }
  cancel_timeout();

  is_video_ |= (call.flags_ & telegram_api::phoneCall::VIDEO_MASK) != 0;

  LOG(DEBUG) << "Do update call to Ready from state " << static_cast<int32>(state_);
  if (state_ == State::WaitAcceptResult) {
    dh_handshake_.set_g_a(call.g_a_or_b_.as_slice());
    TRY_STATUS(dh_handshake_.run_checks(true, DhCache::instance()));
    std::tie(call_state_.key_fingerprint, call_state_.key) = dh_handshake_.gen_key();
  }
  if (call_state_.key_fingerprint != call.key_fingerprint_) {
    return Status::Error(400, "Key fingerprints mismatch");
  }

  call_state_.emojis_fingerprint =
      get_emojis_fingerprint(call_state_.key, is_outgoing_ ? dh_handshake_.get_g_b() : dh_handshake_.get_g_a());

  for (auto &connection : call.connections_) {
    call_state_.connections.push_back(CallConnection(*connection));
  }
  call_state_.protocol = CallProtocol(*call.protocol_);
  call_state_.allow_p2p = (call.flags_ & telegram_api::phoneCall::P2P_ALLOWED_MASK) != 0;
  call_state_.type = CallState::Type::Ready;
  call_state_need_flush_ = true;

  return Status::OK();
}

Status CallActor::do_update_call(telegram_api::phoneCallDiscarded &call) {
  LOG(DEBUG) << "Do update call to Discarded";
  on_call_discarded(get_call_discard_reason(call.reason_), call.need_rating_, call.need_debug_, call.video_);
  return Status::OK();
}

void CallActor::on_call_discarded(CallDiscardReason reason, bool need_rating, bool need_debug, bool is_video) {
  state_ = State::Discarded;
  is_video_ |= is_video;

  if (call_state_.discard_reason == CallDiscardReason::Empty || reason != CallDiscardReason::Empty) {
    call_state_.discard_reason = reason;
  }
  if (call_state_.type != CallState::Type::Error) {
    call_state_.need_rating = need_rating;
    call_state_.need_debug_information = need_debug;
    call_state_.type = CallState::Type::Discarded;
    call_state_need_flush_ = true;
  }
}

bool CallActor::load_dh_config() {
  if (dh_config_ready_) {
    LOG(DEBUG) << "Dh config is ready";
    return true;
  }
  if (!dh_config_query_sent_) {
    dh_config_query_sent_ = true;
    do_load_dh_config(PromiseCreator::lambda([actor_id = actor_id(this)](Result<std::shared_ptr<DhConfig>> dh_config) {
      send_closure(actor_id, &CallActor::on_dh_config, std::move(dh_config), false);
    }));
  }
  LOG(INFO) << "Dh config is not loaded";
  return false;
}

void CallActor::on_error(Status status) {
  CHECK(status.is_error());
  LOG(INFO) << "Receive error " << status;

  if (state_ == State::WaitRequestResult && !request_query_ref_.empty()) {
    LOG(INFO) << "Cancel request call query";
    cancel_query(request_query_ref_);
  }
  if (state_ == State::WaitDiscardResult || state_ == State::Discarded) {
    state_ = State::Discarded;
  } else {
    state_ = State::SendDiscardQuery;
    call_state_.discard_reason =
        call_state_.type == CallState::Type::Pending ? CallDiscardReason::Missed : CallDiscardReason::Disconnected;
  }

  call_state_.type = CallState::Type::Error;
  call_state_.error = std::move(status);
  call_state_need_flush_ = true;
}

void CallActor::on_dh_config(Result<std::shared_ptr<DhConfig>> r_dh_config, bool dummy) {
  if (r_dh_config.is_error()) {
    return on_error(r_dh_config.move_as_error());
  }

  dh_config_ = r_dh_config.move_as_ok();
  auto check_result = DhHandshake::check_config(dh_config_->g, dh_config_->prime, DhCache::instance());
  if (check_result.is_error()) {
    return on_error(std::move(check_result));
  }

  dh_config_ready_ = true;
  yield();
}

void CallActor::do_load_dh_config(Promise<std::shared_ptr<DhConfig>> promise) {
  //TODO: move to external actor
  auto dh_config = G()->get_dh_config();
  int32 version = 0;
  if (dh_config) {
    version = dh_config->version;
  }
  int random_length = 0;
  telegram_api::messages_getDhConfig tl_query(version, random_length);
  auto query = G()->net_query_creator().create(tl_query);
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this), old_dh_config = std::move(dh_config),
                                            promise = std::move(promise)](Result<NetQueryPtr> result_query) mutable {
                      promise.set_result([&]() -> Result<std::shared_ptr<DhConfig>> {
                        TRY_RESULT(query, std::move(result_query));
                        TRY_RESULT(new_dh_config, fetch_result<telegram_api::messages_getDhConfig>(std::move(query)));
                        if (new_dh_config->get_id() == telegram_api::messages_dhConfig::ID) {
                          auto dh = move_tl_object_as<telegram_api::messages_dhConfig>(new_dh_config);
                          auto dh_config = std::make_shared<DhConfig>();
                          dh_config->version = dh->version_;
                          dh_config->prime = dh->p_.as_slice().str();
                          dh_config->g = dh->g_;
                          Random::add_seed(dh->random_.as_slice());
                          G()->set_dh_config(dh_config);
                          return std::move(dh_config);
                        } else if (new_dh_config->get_id() == telegram_api::messages_dhConfigNotModified::ID) {
                          auto dh = move_tl_object_as<telegram_api::messages_dhConfigNotModified>(new_dh_config);
                          Random::add_seed(dh->random_.as_slice());
                        }
                        if (old_dh_config) {
                          return std::move(old_dh_config);
                        }
                        return Status::Error(500, "Can't load DhConfig");
                      }());
                    }));
}

void CallActor::send_received_query() {
  auto tl_query = telegram_api::phone_receivedCall(get_input_phone_call("send_received_query"));
  auto query = G()->net_query_creator().create(tl_query);
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_received_query_result, std::move(r_net_query));
                    }));
}

void CallActor::on_received_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_receivedCall>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
}

void CallActor::try_send_request_query() {
  LOG(INFO) << "Trying to send request query";
  if (!load_dh_config()) {
    return;
  }
  dh_handshake_.set_config(dh_config_->g, dh_config_->prime);
  CHECK(input_user_ != nullptr);
  int32 flags = 0;
  if (is_video_) {
    flags |= telegram_api::phone_requestCall::VIDEO_MASK;
  }
  auto tl_query = telegram_api::phone_requestCall(flags, false /*ignored*/, std::move(input_user_),
                                                  Random::secure_int32(), BufferSlice(dh_handshake_.get_g_b_hash()),
                                                  call_state_.protocol.get_input_phone_call_protocol());
  auto query = G()->net_query_creator().create(tl_query);
  state_ = State::WaitRequestResult;
  int64 call_receive_timeout_ms = G()->shared_config().get_option_integer("call_receive_timeout_ms", 20000);
  double timeout = static_cast<double>(call_receive_timeout_ms) * 0.001;
  LOG(INFO) << "Set call timeout to " << timeout;
  set_timeout_in(timeout);
  query->total_timeout_limit_ = max(timeout, 10.0);
  request_query_ref_ = query.get_weak();
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_request_query_result, std::move(r_net_query));
                    }));
}

void CallActor::on_request_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_requestCall>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  update_call_inner(res.move_as_ok());
}

//phone.acceptCall#3bd2b4a0 peer:InputPhoneCall g_b:bytes protocol:PhoneCallProtocol = phone.PhoneCall;
void CallActor::try_send_accept_query() {
  LOG(INFO) << "Trying to send accept query";
  if (!load_dh_config()) {
    return;
  }
  if (!is_accepted_) {
    LOG(DEBUG) << "Call is not accepted";
    return;
  }
  dh_handshake_.set_config(dh_config_->g, dh_config_->prime);
  auto tl_query = telegram_api::phone_acceptCall(get_input_phone_call("try_send_accept_query"),
                                                 BufferSlice(dh_handshake_.get_g_b()),
                                                 call_state_.protocol.get_input_phone_call_protocol());
  auto query = G()->net_query_creator().create(tl_query);
  state_ = State::WaitAcceptResult;
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_accept_query_result, std::move(r_net_query));
                    }));
}

void CallActor::on_accept_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_acceptCall>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  update_call_inner(res.move_as_ok());
}

//phone.confirmCall#2efe1722 peer:InputPhoneCall g_a:bytes key_fingerprint:long protocol:PhoneCallProtocol = phone.PhoneCall;
void CallActor::try_send_confirm_query() {
  LOG(INFO) << "Trying to send confirm query";
  if (!load_dh_config()) {
    return;
  }
  auto tl_query = telegram_api::phone_confirmCall(get_input_phone_call("try_send_confirm_query"),
                                                  BufferSlice(dh_handshake_.get_g_b()), call_state_.key_fingerprint,
                                                  call_state_.protocol.get_input_phone_call_protocol());
  auto query = G()->net_query_creator().create(tl_query);
  state_ = State::WaitConfirmResult;
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_confirm_query_result, std::move(r_net_query));
                    }));
}

void CallActor::on_confirm_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_confirmCall>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  update_call_inner(res.move_as_ok());
}

void CallActor::try_send_discard_query() {
  if (call_id_ == 0) {
    LOG(INFO) << "Failed to send discard query, because call_id_ is unknown";
    on_call_discarded(CallDiscardReason::Missed, false, false, is_video_);
    yield();
    return;
  }
  LOG(INFO) << "Trying to send discard query";
  int32 flags = 0;
  if (is_video_) {
    flags |= telegram_api::phone_discardCall::VIDEO_MASK;
  }
  auto tl_query = telegram_api::phone_discardCall(
      flags, false /*ignored*/, get_input_phone_call("try_send_discard_query"), duration_,
      get_input_phone_call_discard_reason(call_state_.discard_reason), connection_id_);
  auto query = G()->net_query_creator().create(tl_query);
  state_ = State::WaitDiscardResult;
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_discard_query_result, std::move(r_net_query));
                    }));
}

void CallActor::on_discard_query_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_discardCall>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  send_closure(G()->updates_manager(), &UpdatesManager::on_get_updates, res.move_as_ok(), Promise<Unit>());
}

void CallActor::flush_call_state() {
  if (call_state_need_flush_) {
    if (!is_outgoing_) {
      if (call_state_.type == CallState::Type::Pending) {
        if (!has_notification_) {
          has_notification_ = true;
          send_closure(G()->notification_manager(), &NotificationManager::add_call_notification,
                       DialogId(UserId(call_admin_id_)), local_call_id_);
        }
      } else {
        if (has_notification_) {
          has_notification_ = false;
          send_closure(G()->notification_manager(), &NotificationManager::remove_call_notification,
                       DialogId(UserId(call_admin_id_)), local_call_id_);
        }
      }
    }

    if (call_state_.type == CallState::Type::Ready && !call_state_has_config_) {
      return;
    }
    call_state_need_flush_ = false;

    // TODO can't call const function
    // send_closure(G()->contacts_manager(), &ContactsManager::get_user_id_object, user_id_, "flush_call_state");
    send_closure(G()->td(), &Td::send_update,
                 make_tl_object<td_api::updateCall>(
                     make_tl_object<td_api::call>(local_call_id_.get(), is_outgoing_ ? user_id_.get() : call_admin_id_,
                                                  is_outgoing_, is_video_, call_state_.get_call_state_object())));
  }
}

void CallActor::start_up() {
  auto tl_query = telegram_api::phone_getCallConfig();
  auto query = G()->net_query_creator().create(tl_query);
  send_with_promise(std::move(query),
                    PromiseCreator::lambda([actor_id = actor_id(this)](Result<NetQueryPtr> r_net_query) {
                      send_closure(actor_id, &CallActor::on_get_call_config_result, std::move(r_net_query));
                    }));
}

void CallActor::on_get_call_config_result(Result<NetQueryPtr> r_net_query) {
  auto res = fetch_result<telegram_api::phone_getCallConfig>(std::move(r_net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  call_state_.config = res.ok()->data_;
  call_state_has_config_ = true;
}

void CallActor::loop() {
  LOG(DEBUG) << "Enter loop for " << call_id_ << " in state " << static_cast<int32>(state_) << '/'
             << static_cast<int32>(call_state_.type);
  flush_call_state();
  switch (state_) {
    case State::SendRequestQuery:
      try_send_request_query();
      break;
    case State::SendAcceptQuery:
      try_send_accept_query();
      break;
    case State::SendConfirmQuery:
      try_send_confirm_query();
      break;
    case State::SendDiscardQuery:
      try_send_discard_query();
      break;
    case State::Discarded: {
      if (call_state_.type == CallState::Type::Discarded &&
          (call_state_.need_rating || call_state_.need_debug_information)) {
        break;
      }
      LOG(INFO) << "Close " << local_call_id_;
      stop();
      break;
    }
    default:
      break;
  }
}

void CallActor::timeout_expired() {
  on_error(Status::Error(4005000, "Call timeout expired"));
  yield();
}

void CallActor::on_result(NetQueryPtr query) {
  auto token = get_link_token();
  container_.extract(token).set_value(std::move(query));
  yield();  // Call loop AFTER all events from the promise are executed
}

void CallActor::send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise) {
  auto id = container_.create(std::move(promise));
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, id));
}

void CallActor::hangup() {
  container_.for_each(
      [](auto id, Promise<NetQueryPtr> &promise) { promise.set_error(Status::Error(500, "Request aborted")); });
  stop();
}

vector<string> CallActor::get_emojis_fingerprint(const string &key, const string &g_a) {
  string str = key + g_a;
  unsigned char sha256_buf[32];
  sha256(str, {sha256_buf, 32});

  vector<string> result;
  result.reserve(4);
  for (int i = 0; i < 4; i++) {
    uint64 num = big_endian_to_host64(as<uint64>(sha256_buf + 8 * i));
    result.push_back(get_emoji_fingerprint(num));
  }
  return result;
}

}  // namespace td
