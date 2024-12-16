//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageSearchOffset.h"
#include "td/telegram/td_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

class Td;

class MessageQueryManager final : public Actor {
 public:
  MessageQueryManager(Td *td, ActorShared<> parent);

  void report_message_delivery(MessageFullId message_full_id, int32 until_date, bool from_push);

  void search_outgoing_document_messages(const string &query, int32 limit,
                                         Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void on_get_outgoing_document_messages(vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
                                         Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void search_hashtag_posts(string hashtag, string offset_str, int32 limit,
                            Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void on_get_hashtag_search_result(const string &hashtag, const MessageSearchOffset &old_offset, int32 limit,
                                    int32 total_count,
                                    vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, int32 next_rate,
                                    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void search_dialog_recent_location_messages(DialogId dialog_id, int32 limit,
                                              Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void on_get_recent_locations(DialogId dialog_id, int32 limit, int32 total_count,
                               vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
                               Promise<td_api::object_ptr<td_api::messages>> &&promise);

 private:
  static constexpr int32 MAX_SEARCH_MESSAGES = 100;  // server-side limit

  void tear_down() final;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
