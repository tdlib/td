//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class StarGiftManager final : public Actor {
 public:
  StarGiftManager(Td *td, ActorShared<> parent);

  void get_gift_payment_options(Promise<td_api::object_ptr<td_api::gifts>> &&promise);

  void on_get_gift_prices(FlatHashMap<int64, int64> gift_prices);

  void send_gift(int64 gift_id, UserId user_id, td_api::object_ptr<td_api::formattedText> text, bool is_private,
                 Promise<Unit> &&promise);

  void convert_gift(UserId user_id, MessageId message_id, Promise<Unit> &&promise);

  void save_gift(UserId user_id, MessageId message_id, bool is_saved, Promise<Unit> &&promise);

  void get_user_gifts(UserId user_id, const string &offset, int32 limit,
                      Promise<td_api::object_ptr<td_api::userGifts>> &&promise);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;

  FlatHashMap<int64, int64> gift_prices_;
};

}  // namespace td
