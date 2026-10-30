//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/buffer.h"
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

  void send_widget_query(MessageFullId message_full_id, td_api::object_ptr<td_api::WidgetQueryType> &&query_type,
                         string &&data, Promise<td_api::object_ptr<td_api::widgetQueryAnswer>> &&promise);

  void on_new_widget_query(int64 widget_query_id, UserId sender_user_id, DialogId dialog_id, MessageId message_id,
                           BufferSlice &&data, int64 chat_instance, string &&chat_type, int64 widget_id);

  void on_new_inline_widget_query(int64 widget_query_id, UserId sender_user_id,
                                  telegram_api::object_ptr<telegram_api::InputBotInlineMessageID> &&inline_message_id,
                                  BufferSlice &&data, int64 chat_instance, string &&chat_type, int64 widget_id);

  void on_new_widget_copy_query(int64 widget_query_id, UserId sender_user_id, BufferSlice &&data, int64 widget_id);

  void answer_widget_query(int64 widget_query_id, const string &data, Promise<Unit> &&promise);

  void edit_message_widget_data(MessageFullId message_full_id, bool edit_public_data, const string &public_data,
                                bool edit_chat_data, const string &chat_data, Promise<Unit> &&promise);

  void edit_widget_data(int64 widget_id, const string &public_data, int32 cache_time, Promise<Unit> &&promise);

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
