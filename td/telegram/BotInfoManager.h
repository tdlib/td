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

  void set_bot_name(UserId bot_user_id, const string &language_code, const string &name, Promise<Unit> &&promise);

  void get_bot_name(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

  void set_bot_info_description(UserId bot_user_id, const string &language_code, const string &description,
                                Promise<Unit> &&promise);

  void get_bot_info_description(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

  void set_bot_info_about(UserId bot_user_id, const string &language_code, const string &about,
                          Promise<Unit> &&promise);

  void get_bot_info_about(UserId bot_user_id, const string &language_code, Promise<string> &&promise);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
