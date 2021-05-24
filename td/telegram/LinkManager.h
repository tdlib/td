//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

namespace td {

class Td;

class LinkManager : public Actor {
 public:
  LinkManager(Td *td, ActorShared<> parent);

  LinkManager(const LinkManager &) = delete;
  LinkManager &operator=(const LinkManager &) = delete;
  LinkManager(LinkManager &&) = delete;
  LinkManager &operator=(LinkManager &&) = delete;
  ~LinkManager() override;

  void get_login_url_info(DialogId dialog_id, MessageId message_id, int32 button_id,
                          Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise);

  void get_login_url(DialogId dialog_id, MessageId message_id, int32 button_id, bool allow_write_access,
                     Promise<td_api::object_ptr<td_api::httpUrl>> &&promise);

  void get_link_login_url_info(const string &url, Promise<td_api::object_ptr<td_api::LoginUrlInfo>> &&promise);

  void get_link_login_url(const string &url, bool allow_write_access,
                          Promise<td_api::object_ptr<td_api::httpUrl>> &&promise);

 private:
  void tear_down() override;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
