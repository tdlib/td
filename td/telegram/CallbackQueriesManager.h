//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/DialogId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/UserId.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"

#include <unordered_map>

namespace td {

class Td;

class CallbackQueriesManager {
 public:
  explicit CallbackQueriesManager(Td *td);

  void answer_callback_query(int64 callback_query_id, const string &text, bool show_alert, const string &url,
                             int32 cache_time, Promise<Unit> &&promise) const;

  void on_new_query(int32 flags, int64 callback_query_id, UserId sender_user_id, DialogId dialog_id,
                    MessageId message_id, BufferSlice &&data, int64 chat_instance, string &&game_short_name);

  void on_new_inline_query(int32 flags, int64 callback_query_id, UserId sender_user_id,
                           tl_object_ptr<telegram_api::inputBotInlineMessageID> &&inline_message_id, BufferSlice &&data,
                           int64 chat_instance, string &&game_short_name);

  int64 send_callback_query(FullMessageId full_message_id, const tl_object_ptr<td_api::CallbackQueryPayload> &payload,
                            Promise<Unit> &&promise);

  void on_get_callback_query_answer(int64 result_id, tl_object_ptr<telegram_api::messages_botCallbackAnswer> &&answer);

  tl_object_ptr<td_api::callbackQueryAnswer> get_callback_query_answer_object(int64 result_id);

 private:
  static constexpr int32 BOT_CALLBACK_ANSWER_FLAG_HAS_MESSAGE = 1 << 0;
  static constexpr int32 BOT_CALLBACK_ANSWER_FLAG_NEED_SHOW_ALERT = 1 << 1;
  static constexpr int32 BOT_CALLBACK_ANSWER_FLAG_HAS_URL = 1 << 2;

  struct CallbackQueryAnswer {
    bool show_alert;
    string text;
    string url;
  };

  tl_object_ptr<td_api::CallbackQueryPayload> get_query_payload(int32 flags, BufferSlice &&data,
                                                                string &&game_short_name);

  std::unordered_map<int64, CallbackQueryAnswer> callback_query_answers_;

  Td *td_;
};

}  // namespace td
