//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/CallActor.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/telegram_api.hpp"

#include "td/mtproto/crypto.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DhCache.h"
#include "td/telegram/Global.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/Td.h"
#include "td/telegram/UpdatesManager.h"

#include "td/utils/buffer.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"

#include <tuple>

namespace td {
// CallProtocol
CallProtocol CallProtocol::from_telegram_api(const telegram_api::phoneCallProtocol &protocol) {
  CallProtocol res;
  res.udp_p2p = protocol.udp_p2p_;
  res.udp_reflector = protocol.udp_reflector_;
  res.min_layer = protocol.min_layer_;
  res.max_layer = protocol.max_layer_;
  return res;
}

tl_object_ptr<telegram_api::phoneCallProtocol> CallProtocol::as_telegram_api() const {
  int32 flags = 0;
  if (udp_p2p) {
    flags |= telegram_api::phoneCallProtocol::Flags::UDP_P2P_MASK;
  }
  if (udp_reflector) {
    flags |= telegram_api::phoneCallProtocol::Flags::UDP_REFLECTOR_MASK;
  }
  return make_tl_object<telegram_api::phoneCallProtocol>(flags, udp_p2p, udp_reflector, min_layer, max_layer);
}

CallProtocol CallProtocol::from_td_api(const td_api::callProtocol &protocol) {
  CallProtocol res;
  res.udp_p2p = protocol.udp_p2p_;
  res.udp_reflector = protocol.udp_reflector_;
  res.min_layer = protocol.min_layer_;
  res.max_layer = protocol.max_layer_;
  return res;
}
tl_object_ptr<td_api::callProtocol> CallProtocol::as_td_api() const {
  return make_tl_object<td_api::callProtocol>(udp_p2p, udp_reflector, min_layer, max_layer);
}

CallConnection CallConnection::from_telegram_api(const telegram_api::phoneConnection &connection) {
  CallConnection res;
  res.id = connection.id_;
  res.ip = connection.ip_;
  res.ipv6 = connection.ipv6_;
  res.port = connection.port_;
  res.peer_tag = connection.peer_tag_.as_slice().str();
  return res;
}
tl_object_ptr<telegram_api::phoneConnection> CallConnection::as_telegram_api() const {
  return make_tl_object<telegram_api::phoneConnection>(id, ip, ipv6, port, BufferSlice(peer_tag));
}
tl_object_ptr<td_api::callConnection> CallConnection::as_td_api() const {
  return make_tl_object<td_api::callConnection>(id, ip, ipv6, port, peer_tag);
}

// CallState
tl_object_ptr<td_api::CallState> CallState::as_td_api() const {
  switch (type) {
    case Type::Pending:
      return make_tl_object<td_api::callStatePending>(is_created, is_received);
    case Type::ExchangingKey:
      return make_tl_object<td_api::callStateExchangingKeys>();
    case Type::Ready: {
      std::vector<tl_object_ptr<td_api::callConnection>> v;
      for (auto &c : connections) {
        v.push_back(c.as_td_api());
      }
      return make_tl_object<td_api::callStateReady>(protocol.as_td_api(), std::move(v), config, key,
                                                    vector<string>(emojis_fingerprint));
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

// CallActor
CallActor::CallActor(CallId call_id, ActorShared<> parent, Promise<int64> promise)
    : parent_(std::move(parent)), call_id_promise_(std::move(promise)), local_call_id_(call_id) {
}

void CallActor::create_call(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user,
                            CallProtocol &&protocol, Promise<CallId> &&promise) {
  CHECK(state_ == State::Empty);
  state_ = State::SendRequestQuery;
  is_outgoing_ = true;
  user_id_ = user_id;
  input_user_ = std::move(input_user);
  call_state_.protocol = std::move(protocol);
  call_state_.type = CallState::Type::Pending;
  call_state_.is_received = false;
  call_state_need_flush_ = true;
  loop();
  promise.set_value(CallId(local_call_id_));
}

void CallActor::discard_call(bool is_disconnected, int32 duration, int64 connection_id, Promise<> promise) {
  promise.set_value(Unit());
  if (state_ == State::Discarded || state_ == State::WaitDiscardResult || state_ == State::SendDiscardQuery) {
    return;
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

void CallActor::accept_call(CallProtocol &&protocol, Promise<> promise) {
  if (state_ != State::SendAcceptQuery) {
    return promise.set_error(Status::Error(400, "Unexpected acceptCall"));
  }
  is_accepted_ = true;
  call_state_.protocol = std::move(protocol);
  promise.set_value(Unit());
  loop();
}

void CallActor::rate_call(int32 rating, string comment, Promise<> promise) {
  if (!call_state_.need_rating) {
    return promise.set_error(Status::Error(400, "Unexpected sendCallRating"));
  }
  promise.set_value(Unit());
  auto tl_query = telegram_api::phone_setCallRating(get_input_phone_call(), rating, std::move(comment));
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_set_rating_query_result, std::move(net_query));
                    }));
  loop();
}

void CallActor::on_set_rating_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_setCallRating>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  call_state_.need_rating = false;
  send_closure(G()->updates_manager(), &UpdatesManager::on_get_updates, res.move_as_ok());
}

void CallActor::send_call_debug_information(string data, Promise<> promise) {
  if (!call_state_.need_debug_information) {
    return promise.set_error(Status::Error(400, "Unexpected sendCallDebugInformation"));
  }
  promise.set_value(Unit());
  auto tl_query = telegram_api::phone_saveCallDebug(get_input_phone_call(),
                                                    make_tl_object<telegram_api::dataJSON>(std::move(data)));
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_set_debug_query_result, std::move(net_query));
                    }));
  loop();
}

void CallActor::on_set_debug_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_saveCallDebug>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  call_state_.need_debug_information = false;
}

//Updates
//phoneCallEmpty#5366c915 id:long = PhoneCall;
//phoneCallWaiting#1b8f4ad1 flags:# id:long access_hash:long date:int admin_id:int participant_id:int protocol:PhoneCallProtocol receive_date:flags.0?int = PhoneCall;

// Requests
//phone.discardCall#78d413a6 peer:InputPhoneCall duration:int reason:PhoneCallDiscardReason connection_id:long = Updates;
void CallActor::update_call(tl_object_ptr<telegram_api::PhoneCall> call) {
  Status status;
  downcast_call(*call, [&](auto &call) { status = this->do_update_call(call); });
  if (status.is_error()) {
    on_error(std::move(status));
  }
  loop();
}

void CallActor::update_call_inner(tl_object_ptr<telegram_api::phone_phoneCall> call) {
  send_closure(G()->contacts_manager(), &ContactsManager::on_get_users, std::move(call->users_));
  update_call(std::move(call->phone_call_));
}

Status CallActor::do_update_call(telegram_api::phoneCallEmpty &call) {
  return Status::Error(400, "Call is finished");
}

//phoneCallWaiting#1b8f4ad1 flags:# id:long access_hash:long date:int admin_id:int participant_id:int protocol:PhoneCallProtocol receive_date:flags.0?int = PhoneCall;
Status CallActor::do_update_call(telegram_api::phoneCallWaiting &call) {
  if (state_ != State::WaitRequestResult && state_ != State::WaitAcceptResult) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }

  if (state_ == State::WaitAcceptResult) {
    call_state_.type = CallState::Type::ExchangingKey;
    call_state_need_flush_ = true;
    cancel_timeout();
  } else {
    if ((call.flags_ & telegram_api::phoneCallWaiting::RECEIVE_DATE_MASK) != 0) {
      call_state_.is_received = true;
      call_state_need_flush_ = true;
      int32 call_ring_timeout_ms = G()->shared_config().get_option_integer("call_ring_timeout_ms", 90000);
      set_timeout_in(call_ring_timeout_ms * 0.001);
    }
  }

  call_id_ = call.id_;
  call_access_hash_ = call.access_hash_;
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

//phoneCallRequested#83761ce4 id:long access_hash:long date:int admin_id:int participant_id:int g_a_hash:bytes protocol:PhoneCallProtocol = PhoneCall;
Status CallActor::do_update_call(telegram_api::phoneCallRequested &call) {
  if (state_ != State::Empty) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }
  call_id_ = call.id_;
  call_access_hash_ = call.access_hash_;
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

tl_object_ptr<telegram_api::inputPhoneCall> CallActor::get_input_phone_call() {
  CHECK(call_id_ != 0);
  return make_tl_object<telegram_api::inputPhoneCall>(call_id_, call_access_hash_);
}

//phoneCallAccepted#6d003d3f id:long access_hash:long date:int admin_id:int participant_id:int g_b:bytes protocol:PhoneCallProtocol = PhoneCall;
Status CallActor::do_update_call(telegram_api::phoneCallAccepted &call) {
  if (state_ != State::WaitRequestResult) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }

  dh_handshake_.set_g_a(call.g_b_.as_slice());
  TRY_STATUS(dh_handshake_.run_checks(DhCache::instance()));
  std::tie(call_state_.key_fingerprint, call_state_.key) = dh_handshake_.gen_key();
  state_ = State::SendConfirmQuery;
  call_state_.type = CallState::Type::ExchangingKey;
  call_state_need_flush_ = true;
  cancel_timeout();
  return Status::OK();
}

//phoneCall#ffe6ab67 id:long access_hash:long date:int admin_id:int participant_id:int g_a_or_b:bytes key_fingerprint:long protocol:PhoneCallProtocol connection:PhoneConnection alternative_connections:Vector<PhoneConnection> start_date:int = PhoneCall;
Status CallActor::do_update_call(telegram_api::phoneCall &call) {
  if (state_ != State::WaitAcceptResult && state_ != State::WaitConfirmResult) {
    return Status::Error(500, PSLICE() << "Drop unexpected " << to_string(call));
  }

  if (state_ == State::WaitAcceptResult) {
    dh_handshake_.set_g_a(call.g_a_or_b_.as_slice());
    TRY_STATUS(dh_handshake_.run_checks(DhCache::instance()));
    std::tie(call_state_.key_fingerprint, call_state_.key) = dh_handshake_.gen_key();
  }
  if (call_state_.key_fingerprint != call.key_fingerprint_) {
    return Status::Error(400, "Key fingerprints mismatch");
  }

  call_state_.emojis_fingerprint =
      get_emojis_fingerprint(call_state_.key, is_outgoing_ ? dh_handshake_.get_g_b() : dh_handshake_.get_g_a());

  call_state_.connections.push_back(CallConnection::from_telegram_api(*call.connection_));
  for (auto &connection : call.alternative_connections_) {
    call_state_.connections.push_back(CallConnection::from_telegram_api(*connection));
  }
  call_state_.protocol = CallProtocol::from_telegram_api(*call.protocol_);
  call_state_.type = CallState::Type::Ready;
  call_state_need_flush_ = true;

  return Status::OK();
}

//phoneCallDiscarded#50ca4de1 flags:# need_rating:flags.2?true need_debug:flags.3?true id:long reason:flags.0?PhoneCallDiscardReason duration:flags.1?int = PhoneCall;
Status CallActor::do_update_call(telegram_api::phoneCallDiscarded &call) {
  state_ = State::Discarded;

  auto reason = get_call_discard_reason(call.reason_);
  if (call_state_.discard_reason == CallDiscardReason::Empty || reason != CallDiscardReason::Empty) {
    call_state_.discard_reason = reason;
  }
  if (call_state_.type != CallState::Type::Error) {
    call_state_.need_rating = call.need_rating_;
    call_state_.need_debug_information = call.need_debug_;
    call_state_.type = CallState::Type::Discarded;
    call_state_need_flush_ = true;
  }
  return Status::OK();
}

bool CallActor::load_dh_config() {
  if (dh_config_ready_) {
    return true;
  }
  if (!dh_config_query_sent_) {
    dh_config_query_sent_ = true;
    do_load_dh_config(PromiseCreator::lambda([actor_id = actor_id(this)](Result<std::shared_ptr<DhConfig>> dh_config) {
      send_closure(actor_id, &CallActor::on_dh_config, std::move(dh_config), false);
    }));
  }
  return false;
}

void CallActor::on_error(Status status) {
  CHECK(status.is_error());

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
  dh_config_ready_ = true;
  dh_config_ = r_dh_config.move_as_ok();
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
  auto query = G()->net_query_creator().create(create_storer(tl_query));
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
                          G()->set_dh_config(dh_config);
                          return std::move(dh_config);
                        }
                        if (old_dh_config) {
                          return std::move(old_dh_config);
                        }
                        return Status::Error(500, "Can't load DhConfig");
                      }());
                    }));
}

void CallActor::send_received_query() {
  auto tl_query = telegram_api::phone_receivedCall(get_input_phone_call());
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_received_query_result, std::move(net_query));
                    }));
}

void CallActor::on_received_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_receivedCall>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
}

//phone.requestCall#5b95b3d4 user_id:InputUser random_id:int g_a_hash:bytes protocol:PhoneCallProtocol = phone.PhoneCall;
void CallActor::try_send_request_query() {
  if (!load_dh_config()) {
    return;
  }
  dh_handshake_.set_config(dh_config_->g, dh_config_->prime);
  CHECK(input_user_ != nullptr);
  auto tl_query = telegram_api::phone_requestCall(std::move(input_user_), Random::secure_int32(),
                                                  BufferSlice(dh_handshake_.get_g_b_hash()),
                                                  call_state_.protocol.as_telegram_api());
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  state_ = State::WaitRequestResult;
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_request_query_result, std::move(net_query));
                    }));
  int32 call_receive_timeout_ms = G()->shared_config().get_option_integer("call_receive_timeout_ms", 20000);
  set_timeout_in(call_receive_timeout_ms * 0.001);
}

void CallActor::on_request_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_requestCall>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  update_call_inner(res.move_as_ok());
}

//phone.acceptCall#3bd2b4a0 peer:InputPhoneCall g_b:bytes protocol:PhoneCallProtocol = phone.PhoneCall;
void CallActor::try_send_accept_query() {
  if (!load_dh_config()) {
    return;
  }
  if (!is_accepted_) {
    return;
  }
  dh_handshake_.set_config(dh_config_->g, dh_config_->prime);
  auto tl_query = telegram_api::phone_acceptCall(get_input_phone_call(), BufferSlice(dh_handshake_.get_g_b()),
                                                 call_state_.protocol.as_telegram_api());
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  state_ = State::WaitAcceptResult;
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_accept_query_result, std::move(net_query));
                    }));
}

void CallActor::on_accept_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_acceptCall>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  update_call_inner(res.move_as_ok());
}

//phone.confirmCall#2efe1722 peer:InputPhoneCall g_a:bytes key_fingerprint:long protocol:PhoneCallProtocol = phone.PhoneCall;
void CallActor::try_send_confirm_query() {
  if (!load_dh_config()) {
    return;
  }
  auto tl_query = telegram_api::phone_confirmCall(get_input_phone_call(), BufferSlice(dh_handshake_.get_g_b()),
                                                  call_state_.key_fingerprint, call_state_.protocol.as_telegram_api());
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  state_ = State::WaitConfirmResult;
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_confirm_query_result, std::move(net_query));
                    }));
}

void CallActor::on_confirm_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_confirmCall>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  update_call_inner(res.move_as_ok());
}

void CallActor::try_send_discard_query() {
  if (call_id_ == 0) {
    state_ = State::Discarded;
    yield();
    return;
  }
  auto tl_query =
      telegram_api::phone_discardCall(get_input_phone_call(), duration_,
                                      get_input_phone_call_discard_reason(call_state_.discard_reason), connection_id_);
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  state_ = State::WaitDiscardResult;
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_discard_query_result, std::move(net_query));
                    }));
}

void CallActor::on_discard_query_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_discardCall>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  send_closure(G()->updates_manager(), &UpdatesManager::on_get_updates, res.move_as_ok());
}

void CallActor::flush_call_state() {
  if (call_state_need_flush_) {
    if (call_state_.type == CallState::Type::Ready && !call_state_has_config_) {
      return;
    }
    call_state_need_flush_ = false;

    // can't call const function
    // send_closure(G()->contacts_manager(), &ContactsManager::get_user_id_object, user_id_);
    send_closure(G()->td(), &Td::send_update,
                 make_tl_object<td_api::updateCall>(
                     make_tl_object<td_api::call>(local_call_id_.get(), is_outgoing_ ? user_id_.get() : call_admin_id_,
                                                  is_outgoing_, call_state_.as_td_api())));
  }
}

void CallActor::start_up() {
  auto tl_query = telegram_api::phone_getCallConfig();
  auto query = G()->net_query_creator().create(create_storer(tl_query));
  send_with_promise(std::move(query), PromiseCreator::lambda([actor_id = actor_id(this)](NetQueryPtr net_query) {
                      send_closure(actor_id, &CallActor::on_get_call_config_result, std::move(net_query));
                    }));
}

void CallActor::on_get_call_config_result(NetQueryPtr net_query) {
  auto res = fetch_result<telegram_api::phone_getCallConfig>(std::move(net_query));
  if (res.is_error()) {
    return on_error(res.move_as_error());
  }
  call_state_.config = res.ok()->data_;
  call_state_has_config_ = true;
}

void CallActor::loop() {
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
      LOG(INFO) << "Close call " << local_call_id_;
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

vector<string> CallActor::get_emojis_fingerprint(const string &key, const string &g_a) {
  string str = key + g_a;
  unsigned char sha256_buf[32];
  sha256(str, {sha256_buf, 32});

  vector<string> result;
  result.reserve(4);
  for (int i = 0; i < 4; i++) {
    uint64 num =
        (static_cast<uint64>(sha256_buf[8 * i + 0]) << 56) | (static_cast<uint64>(sha256_buf[8 * i + 1]) << 48) |
        (static_cast<uint64>(sha256_buf[8 * i + 2]) << 40) | (static_cast<uint64>(sha256_buf[8 * i + 3]) << 32) |
        (static_cast<uint64>(sha256_buf[8 * i + 4]) << 24) | (static_cast<uint64>(sha256_buf[8 * i + 5]) << 16) |
        (static_cast<uint64>(sha256_buf[8 * i + 6]) << 8) | (static_cast<uint64>(sha256_buf[8 * i + 7]));
    result.push_back(get_emoji_fingerprint(num));
  }
  return result;
}

}  // namespace td
