//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/AuthDataShared.h"

#include "td/telegram/Global.h"
#include "td/telegram/TdDb.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/RwMutex.h"
#include "td/utils/tl_helpers.h"

namespace td {

class AuthDataSharedImpl : public AuthDataShared {
 public:
  AuthDataSharedImpl(DcId dc_id, std::shared_ptr<PublicRsaKeyShared> public_rsa_key, std::shared_ptr<Guard> guard)
      : dc_id_(dc_id), public_rsa_key_(std::move(public_rsa_key)), guard_(std::move(guard)) {
    log_auth_key(get_auth_key());
  }

  DcId dc_id() const override {
    return dc_id_;
  }

  const std::shared_ptr<PublicRsaKeyShared> &public_rsa_key() override {
    return public_rsa_key_;
  }

  mtproto::AuthKey get_auth_key() override {
    string dc_key = G()->td_db()->get_binlog_pmc()->get(auth_key_key());

    mtproto::AuthKey res;
    if (!dc_key.empty()) {
      unserialize(res, dc_key).ensure();
    }
    return res;
  }
  using AuthDataShared::get_auth_key_state;
  std::pair<AuthKeyState, bool> get_auth_key_state() override {
    // TODO (perf):
    auto auth_key = get_auth_key();
    AuthKeyState state = get_auth_key_state(auth_key);
    return std::make_pair(state, auth_key.was_auth_flag());
  }

  void set_auth_key(const mtproto::AuthKey &auth_key) override {
    G()->td_db()->get_binlog_pmc()->set(auth_key_key(), serialize(auth_key));
    log_auth_key(auth_key);

    notify();
  }

  // TODO: extract it from G()
  void update_server_time_difference(double diff) override {
    G()->update_server_time_difference(diff);
  }

  double get_server_time_difference() override {
    return G()->get_server_time_difference();
  }

  void add_auth_key_listener(unique_ptr<Listener> listener) override {
    if (listener->notify()) {
      auto lock = rw_mutex_.lock_write();
      auth_key_listeners_.push_back(std::move(listener));
    }
  }

  void set_future_salts(const std::vector<mtproto::ServerSalt> &future_salts) override {
    G()->td_db()->get_binlog_pmc()->set(future_salts_key(), serialize(future_salts));
  }

  std::vector<mtproto::ServerSalt> get_future_salts() override {
    string future_salts = G()->td_db()->get_binlog_pmc()->get(future_salts_key());
    std::vector<mtproto::ServerSalt> res;
    if (!future_salts.empty()) {
      unserialize(res, future_salts).ensure();
    }
    return res;
  }

 private:
  DcId dc_id_;
  std::vector<unique_ptr<Listener>> auth_key_listeners_;
  std::shared_ptr<PublicRsaKeyShared> public_rsa_key_;
  std::shared_ptr<Guard> guard_;
  RwMutex rw_mutex_;

  string auth_key_key() {
    return PSTRING() << "auth" << dc_id_.get_raw_id();
  }
  string future_salts_key() {
    return PSTRING() << "salt" << dc_id_.get_raw_id();
  }

  void notify() {
    auto lock = rw_mutex_.lock_read();

    td::remove_if(auth_key_listeners_, [&](auto &listener) { return !listener->notify(); });
  }

  void log_auth_key(const mtproto::AuthKey &auth_key) {
    LOG(WARNING) << dc_id_ << " " << tag("auth_key_id", auth_key.id()) << tag("state", get_auth_key_state(auth_key))
                 << tag("created_at", auth_key.created_at());
  }
};

std::shared_ptr<AuthDataShared> AuthDataShared::create(DcId dc_id, std::shared_ptr<PublicRsaKeyShared> public_rsa_key,
                                                       std::shared_ptr<Guard> guard) {
  return std::make_shared<AuthDataSharedImpl>(dc_id, std::move(public_rsa_key), std::move(guard));
}

}  // namespace td
