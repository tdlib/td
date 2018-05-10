//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"

#include <memory>

namespace td {

class SessionProxy;

class SessionMultiProxy : public Actor {
 public:
  SessionMultiProxy();
  SessionMultiProxy(const SessionMultiProxy &other) = delete;
  SessionMultiProxy &operator=(const SessionMultiProxy &other) = delete;
  ~SessionMultiProxy() override;
  SessionMultiProxy(int32 session_count, std::shared_ptr<AuthDataShared> shared_auth_data, bool is_main, bool use_pfs,
                    bool allow_media_only, bool is_media, bool is_cdn);

  void send(NetQueryPtr query);
  void update_main_flag(bool is_main);

  void update_session_count(int32 session_count);
  void update_use_pfs(bool use_pfs);
  void update_options(int32 session_count, bool use_pfs);
  void update_mtproto_header();

 private:
  size_t pos_ = 0;
  int32 session_count_ = 0;
  std::shared_ptr<AuthDataShared> auth_data_;
  bool is_main_ = false;
  bool use_pfs_ = false;
  bool allow_media_only_ = false;
  bool is_media_ = false;
  bool is_cdn_ = false;
  std::vector<ActorOwn<SessionProxy>> sessions_;

  void start_up() override;
  void init();

  bool get_pfs_flag() const;

  void update_auth_state();
};

}  // namespace td
