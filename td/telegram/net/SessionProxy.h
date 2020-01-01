//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/NetQuery.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/AuthKey.h"

#include "td/actor/actor.h"

#include <memory>

namespace td {

class Session;

class SessionProxy : public Actor {
 public:
  friend class SessionCallback;
  class Callback {
   public:
    virtual ~Callback() = default;
    virtual void on_query_finished() = 0;
  };

  SessionProxy(unique_ptr<Callback> callback, std::shared_ptr<AuthDataShared> shared_auth_data, bool is_main,
               bool allow_media_only, bool is_media, bool use_pfs, bool is_cdn, bool need_destroy);

  void send(NetQueryPtr query);
  void update_main_flag(bool is_main);
  void update_mtproto_header();
  void update_destroy(bool need_destroy);

 private:
  unique_ptr<Callback> callback_;
  std::shared_ptr<AuthDataShared> auth_data_;
  AuthKeyState auth_key_state_;
  bool is_main_;
  bool allow_media_only_;
  bool is_media_;
  bool use_pfs_;
  mtproto::AuthKey tmp_auth_key_;
  std::vector<mtproto::ServerSalt> server_salts_;
  bool is_cdn_;
  bool need_destroy_;
  ActorOwn<Session> session_;
  std::vector<NetQueryPtr> pending_queries_;
  uint64 session_generation_ = 1;

  void on_failed();
  void on_closed();
  void close_session();
  void open_session(bool force = false);

  void update_auth_key_state();
  void on_tmp_auth_key_updated(mtproto::AuthKey auth_key);
  void on_server_salt_updated(std::vector<mtproto::ServerSalt> server_salts);

  void on_query_finished();

  void start_up() override;
  void tear_down() override;
};

}  // namespace td
