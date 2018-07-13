//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/mtproto/crypto.h"

#include "td/telegram/CallDiscardReason.h"
#include "td/telegram/CallId.h"
#include "td/telegram/DhConfig.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/Container.h"
#include "td/utils/Status.h"

#include <memory>

namespace td {

struct CallProtocol {
  bool udp_p2p{true};
  bool udp_reflector{true};
  int32 min_layer{65};
  int32 max_layer{65};

  static CallProtocol from_telegram_api(const telegram_api::phoneCallProtocol &protocol);
  tl_object_ptr<telegram_api::phoneCallProtocol> as_telegram_api() const;
  static CallProtocol from_td_api(const td_api::callProtocol &protocol);
  tl_object_ptr<td_api::callProtocol> as_td_api() const;
};

struct CallConnection {
  int64 id;
  string ip;
  string ipv6;
  int32 port;
  string peer_tag;

  static CallConnection from_telegram_api(const telegram_api::phoneConnection &connection);
  tl_object_ptr<telegram_api::phoneConnection> as_telegram_api() const;
  tl_object_ptr<td_api::callConnection> as_td_api() const;
};

struct CallState {
  enum class Type : int32 { Empty, Pending, ExchangingKey, Ready, HangingUp, Discarded, Error } type{Type::Empty};

  CallProtocol protocol;
  std::vector<CallConnection> connections;
  CallDiscardReason discard_reason{CallDiscardReason::Empty};
  bool is_created{false};
  bool is_received{false};
  bool need_debug_information{false};
  bool need_rating{false};

  int64 key_fingerprint{0};
  string key;
  string config;
  vector<string> emojis_fingerprint;

  Status error;

  tl_object_ptr<td_api::CallState> as_td_api() const;
};

class CallActor : public NetQueryCallback {
 public:
  CallActor(CallId call_id, ActorShared<> parent, Promise<int64> promise);

  void create_call(UserId user_id, tl_object_ptr<telegram_api::InputUser> &&input_user, CallProtocol &&protocol,
                   Promise<CallId> &&promise);
  void discard_call(bool is_disconnected, int32 duration, int64 connection_id, Promise<> promise);
  void accept_call(CallProtocol &&protocol, Promise<> promise);
  void rate_call(int32 rating, string comment, Promise<> promise);
  void send_call_debug_information(string data, Promise<> promise);

  void update_call(tl_object_ptr<telegram_api::PhoneCall> call);

 private:
  void update_call_inner(tl_object_ptr<telegram_api::phone_phoneCall> call);
  ActorShared<> parent_;
  Promise<int64> call_id_promise_;

  DhHandshake dh_handshake_;
  std::shared_ptr<DhConfig> dh_config_;
  bool dh_config_query_sent_{false};
  bool dh_config_ready_{false};

  int32 duration_{0};
  int64 connection_id_{0};

  enum class State : int32 {
    Empty,
    SendRequestQuery,
    WaitRequestResult,
    SendAcceptQuery,
    WaitAcceptResult,
    SendConfirmQuery,
    WaitConfirmResult,
    SendDiscardQuery,
    WaitDiscardResult,
    Discarded
  } state_{State::Empty};
  bool is_accepted_{false};

  bool is_outgoing_{false};
  UserId user_id_;
  tl_object_ptr<telegram_api::InputUser> input_user_;

  CallId local_call_id_;
  int64 call_id_{0};
  int64 call_access_hash_{0};
  int32 call_admin_id_{0};
  int32 call_participant_id_{0};

  CallState call_state_;
  bool call_state_need_flush_{false};
  bool call_state_has_config_{false};

  NetQueryRef request_query_ref_;

  tl_object_ptr<telegram_api::inputPhoneCall> get_input_phone_call();
  bool load_dh_config();
  void on_dh_config(Result<std::shared_ptr<DhConfig>> r_dh_config, bool dummy);
  void do_load_dh_config(Promise<std::shared_ptr<DhConfig>> promise);

  Status do_update_call(telegram_api::phoneCallEmpty &call);
  Status do_update_call(telegram_api::phoneCallWaiting &call);
  Status do_update_call(telegram_api::phoneCallRequested &call);
  Status do_update_call(telegram_api::phoneCallAccepted &call);
  Status do_update_call(telegram_api::phoneCall &call);
  Status do_update_call(telegram_api::phoneCallDiscarded &call);

  void send_received_query();
  void on_received_query_result(NetQueryPtr net_query);

  void try_send_request_query();
  void on_request_query_result(NetQueryPtr net_query);

  void try_send_accept_query();
  void on_accept_query_result(NetQueryPtr net_query);

  void try_send_confirm_query();
  void on_confirm_query_result(NetQueryPtr net_query);

  void try_send_discard_query();
  void on_discard_query_result(NetQueryPtr net_query);

  void on_begin_exchanging_key();

  void on_call_discarded(CallDiscardReason reason, bool need_rating, bool need_debug);

  void on_set_rating_query_result(NetQueryPtr net_query);
  void on_set_debug_query_result(NetQueryPtr net_query);

  void on_get_call_config_result(NetQueryPtr net_query);

  void flush_call_state();

  static vector<string> get_emojis_fingerprint(const string &key, const string &g_a);

  void start_up() override;
  void loop() override;

  Container<Promise<NetQueryPtr>> container_;
  void on_result(NetQueryPtr query) override;
  void send_with_promise(NetQueryPtr query, Promise<NetQueryPtr> promise);

  void timeout_expired() override;
  void hangup() override;

  void on_error(Status status);
};

}  // namespace td
