//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/SessionProxy.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/Session.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/logging.h"
#include "td/utils/Slice.h"

#include <functional>

namespace td {

namespace mtproto {
class RawConnection;
}  // namespace mtproto

class SessionCallback : public Session::Callback {
 public:
  SessionCallback(ActorShared<SessionProxy> parent, DcId dc_id, bool allow_media_only, bool is_media, size_t hash)
      : parent_(std::move(parent))
      , dc_id_(dc_id)
      , allow_media_only_(allow_media_only)
      , is_media_(is_media)
      , hash_(hash) {
  }

  void on_failed() override {
    send_closure(parent_, &SessionProxy::on_failed);
  }
  void on_closed() override {
    send_closure(parent_, &SessionProxy::on_closed);
  }
  void request_raw_connection(Promise<std::unique_ptr<mtproto::RawConnection>> promise) override {
    send_closure(G()->connection_creator(), &ConnectionCreator::request_raw_connection, dc_id_, allow_media_only_,
                 is_media_, std::move(promise), hash_);
  }

  void on_tmp_auth_key_updated(mtproto::AuthKey auth_key) override {
    send_closure(parent_, &SessionProxy::on_tmp_auth_key_updated, std::move(auth_key));
  }

  void on_server_salt_updated(std::vector<mtproto::ServerSalt> server_salts) override {
    send_closure(parent_, &SessionProxy::on_server_salt_updated, std::move(server_salts));
  }

 private:
  ActorShared<SessionProxy> parent_;
  DcId dc_id_;
  bool allow_media_only_ = false;
  bool is_media_ = false;
  size_t hash_ = 0;
};

SessionProxy::SessionProxy(std::shared_ptr<AuthDataShared> shared_auth_data, bool is_main, bool allow_media_only,
                           bool is_media, bool use_pfs, bool need_wait_for_key, bool is_cdn)
    : auth_data_(std::move(shared_auth_data))
    , is_main_(is_main)
    , allow_media_only_(allow_media_only)
    , is_media_(is_media)
    , use_pfs_(use_pfs)
    , need_wait_for_key_(need_wait_for_key)
    , is_cdn_(is_cdn) {
}

void SessionProxy::start_up() {
  class Listener : public AuthDataShared::Listener {
   public:
    explicit Listener(ActorShared<SessionProxy> session_proxy) : session_proxy_(std::move(session_proxy)) {
    }
    bool notify() override {
      if (!session_proxy_.is_alive()) {
        return false;
      }
      send_closure(session_proxy_, &SessionProxy::update_auth_state);
      return true;
    }

   private:
    ActorShared<SessionProxy> session_proxy_;
  };
  auth_state_ = auth_data_->get_auth_state().first;
  auth_data_->add_auth_key_listener(std::make_unique<Listener>(actor_shared(this)));
  if (is_main_ && !need_wait_for_key_) {
    open_session();
  }
}

void SessionProxy::tear_down() {
  for (auto &query : pending_queries_) {
    query->resend();
    G()->net_query_dispatcher().dispatch(std::move(query));
  }
  pending_queries_.clear();
}

void SessionProxy::send(NetQueryPtr query) {
  if (query->auth_flag() == NetQuery::AuthFlag::On && auth_state_ != AuthState::OK) {
    query->debug(PSTRING() << get_name() << ": wait for auth");
    pending_queries_.emplace_back(std::move(query));
    return;
  }
  if (session_.empty()) {
    open_session(true);
  }
  query->debug(PSTRING() << get_name() << ": sent to session");
  send_closure(session_, &Session::send, std::move(query));
}

void SessionProxy::update_main_flag(bool is_main) {
  if (is_main_ == is_main) {
    return;
  }
  LOG(INFO) << "Update " << get_name() << " is_main to " << is_main;
  is_main_ = is_main;
  close_session();
  open_session();
}

void SessionProxy::on_failed() {
  if (session_generation_ != get_link_token()) {
    return;
  }
  close_session();
  open_session();
}

void SessionProxy::update_mtproto_header() {
  close_session();
  open_session();
}

void SessionProxy::on_closed() {
}

void SessionProxy::close_session() {
  send_closure(std::move(session_), &Session::close);
  session_generation_++;
}
void SessionProxy::open_session(bool force) {
  if (!force && !is_main_) {
    return;
  }
  CHECK(session_.empty());
  auto dc_id = auth_data_->dc_id();
  string name = PSTRING() << "Session" << get_name().substr(Slice("SessionProxy").size());
  string hash_string = PSTRING() << name << " " << dc_id.get_raw_id() << " " << allow_media_only_;
  auto hash = std::hash<std::string>()(hash_string);
  int32 int_dc_id = dc_id.get_raw_id();
  if (G()->is_test_dc()) {
    int_dc_id += 10000;
  }
  if (allow_media_only_ && !is_cdn_) {
    int_dc_id = -int_dc_id;
  }
  session_ = create_actor<Session>(
      name,
      make_unique<SessionCallback>(actor_shared(this, session_generation_), dc_id, allow_media_only_, is_media_, hash),
      auth_data_, int_dc_id, is_main_, use_pfs_, is_cdn_, tmp_auth_key_, server_salts_);
}

void SessionProxy::update_auth_state() {
  auth_state_ = auth_data_->get_auth_state().first;
  if (pending_queries_.empty() && !need_wait_for_key_) {
    return;
  }
  if (auth_state_ != AuthState::OK) {
    return;
  }
  if (session_.empty()) {
    open_session(true);
  }
  for (auto &query : pending_queries_) {
    query->debug(PSTRING() << get_name() << ": sent to session");
    send_closure(session_, &Session::send, std::move(query));
  }
  pending_queries_.clear();
}

void SessionProxy::on_tmp_auth_key_updated(mtproto::AuthKey auth_key) {
  string state;
  if (auth_key.empty()) {
    state = "Empty";
  } else if (auth_key.auth_flag()) {
    state = "OK";
  } else {
    state = "NoAuth";
  }
  LOG(WARNING) << "tmp_auth_key " << auth_key.id() << ": " << state;
  tmp_auth_key_ = std::move(auth_key);
}
void SessionProxy::on_server_salt_updated(std::vector<mtproto::ServerSalt> server_salts) {
  server_salts_ = std::move(server_salts);
}

}  // namespace td
