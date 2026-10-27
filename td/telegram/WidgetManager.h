//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageFullId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class WidgetManager final : public Actor {
 public:
  WidgetManager(Td *td, ActorShared<> parent);
  WidgetManager(const WidgetManager &) = delete;
  WidgetManager &operator=(const WidgetManager &) = delete;
  WidgetManager(WidgetManager &&) = delete;
  WidgetManager &operator=(WidgetManager &&) = delete;
  ~WidgetManager() final;

  void send_widget_query(MessageFullId message_full_id, string &&data,
                         Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> &&promise);

  void answer_widget_query(int64 widget_query_id, const string &data, Promise<Unit> &&promise);

  void get_widget_public_data(MessageFullId message_full_id,
                              Promise<td_api::object_ptr<td_api::widgetPublicData>> &&promise);

  void on_get_widget_public_data(MessageFullId message_full_id,
                                 telegram_api::object_ptr<telegram_api::messages_WidgetPublicData> &&public_data,
                                 Promise<td_api::object_ptr<td_api::widgetPublicData>> &&promise);

 private:
  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
