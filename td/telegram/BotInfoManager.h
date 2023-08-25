//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogParticipant.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class BotInfoManager final : public Actor {
 public:
  BotInfoManager(Td *td, ActorShared<> parent);

  void set_default_group_administrator_rights(AdministratorRights administrator_rights, Promise<Unit> &&promise);

  void set_default_channel_administrator_rights(AdministratorRights administrator_rights, Promise<Unit> &&promise);

  void can_bot_send_messages(UserId bot_user_id, Promise<Unit> &&promise);

  void allow_bot_to_send_messages(UserId bot_user_id, Promise<Unit> &&promise);

  void set_bot_name(UserId bot_user_id, const string &language_code, const string &name, Promise<Unit> &&promise);

  void get_bot_name(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

  void set_bot_info_description(UserId bot_user_id, const string &language_code, const string &description,
                                Promise<Unit> &&promise);

  void get_bot_info_description(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

  void set_bot_info_about(UserId bot_user_id, const string &language_code, const string &about,
                          Promise<Unit> &&promise);

  void get_bot_info_about(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

 private:
  static constexpr double MAX_QUERY_DELAY = 0.01;

  struct PendingSetBotInfoQuery {
    UserId bot_user_id_;
    string language_code_;
    int type_ = 0;
    string value_;
    Promise<Unit> promise_;

    PendingSetBotInfoQuery(UserId bot_user_id, const string &language_code, int type, const string &value,
                           Promise<Unit> &&promise)
        : bot_user_id_(bot_user_id)
        , language_code_(language_code)
        , type_(type)
        , value_(value)
        , promise_(std::move(promise)) {
    }
  };

  struct PendingGetBotInfoQuery {
    UserId bot_user_id_;
    string language_code_;
    int type_ = 0;
    Promise<string> promise_;

    PendingGetBotInfoQuery(UserId bot_user_id, const string &language_code, int type, Promise<string> &&promise)
        : bot_user_id_(bot_user_id), language_code_(language_code), type_(type), promise_(std::move(promise)) {
    }
  };

  void tear_down() final;

  void hangup() final;

  void timeout_expired() final;

  void add_pending_set_query(UserId bot_user_id, const string &language_code, int type, const string &value,
                             Promise<Unit> &&promise);

  void add_pending_get_query(UserId bot_user_id, const string &language_code, int type, Promise<string> &&promise);

  vector<PendingSetBotInfoQuery> pending_set_bot_info_queries_;

  vector<PendingGetBotInfoQuery> pending_get_bot_info_queries_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
