//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/BusinessConnectionId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageInputReplyTo.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"

namespace td {

struct InputMessageContent;
struct ReplyMarkup;
class Td;

class BusinessConnectionManager final : public Actor {
 public:
  BusinessConnectionManager(Td *td, ActorShared<> parent);
  BusinessConnectionManager(const BusinessConnectionManager &) = delete;
  BusinessConnectionManager &operator=(const BusinessConnectionManager &) = delete;
  BusinessConnectionManager(BusinessConnectionManager &&) = delete;
  BusinessConnectionManager &operator=(BusinessConnectionManager &&) = delete;
  ~BusinessConnectionManager() final;

  Status check_business_connection(const BusinessConnectionId &connection_id, DialogId dialog_id) const;

  DcId get_business_connection_dc_id(const BusinessConnectionId &connection_id) const;

  void on_update_bot_business_connect(telegram_api::object_ptr<telegram_api::botBusinessConnection> &&connection);

  void on_update_bot_new_business_message(const BusinessConnectionId &connection_id,
                                          telegram_api::object_ptr<telegram_api::Message> &&message);

  void on_update_bot_edit_business_message(const BusinessConnectionId &connection_id,
                                           telegram_api::object_ptr<telegram_api::Message> &&message);

  void on_update_bot_delete_business_messages(const BusinessConnectionId &connection_id, DialogId dialog_id,
                                              vector<int32> &&messages);

  void get_business_connection(const BusinessConnectionId &connection_id,
                               Promise<td_api::object_ptr<td_api::businessConnection>> &&promise);

  void send_message(BusinessConnectionId business_connection_id, DialogId dialog_id,
                    td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to, bool disable_notification,
                    bool protect_content, td_api::object_ptr<td_api::ReplyMarkup> &&reply_markup,
                    td_api::object_ptr<td_api::InputMessageContent> &&input_message_content,
                    Promise<td_api::object_ptr<td_api::message>> &&promise);

 private:
  struct BusinessConnection;
  struct PendingMessage;
  class SendBusinessMessageQuery;

  void tear_down() final;

  void on_get_business_connection(const BusinessConnectionId &connection_id,
                                  Result<telegram_api::object_ptr<telegram_api::Updates>> r_updates);

  MessageInputReplyTo create_business_message_input_reply_to(
      td_api::object_ptr<td_api::InputMessageReplyTo> &&reply_to);

  Result<InputMessageContent> process_input_message_content(
      DialogId dialog_id, td_api::object_ptr<td_api::InputMessageContent> &&input_message_content);

  unique_ptr<PendingMessage> create_business_message_to_send(BusinessConnectionId business_connection_id,
                                                             DialogId dialog_id, MessageInputReplyTo &&input_reply_to,
                                                             bool disable_notification, bool protect_content,
                                                             unique_ptr<ReplyMarkup> &&reply_markup,
                                                             InputMessageContent &&input_content) const;

  void do_send_message(unique_ptr<PendingMessage> &&message, Promise<td_api::object_ptr<td_api::message>> &&promise);

  WaitFreeHashMap<BusinessConnectionId, unique_ptr<BusinessConnection>, BusinessConnectionIdHash> business_connections_;

  FlatHashMap<BusinessConnectionId, vector<Promise<td_api::object_ptr<td_api::businessConnection>>>,
              BusinessConnectionIdHash>
      get_business_connection_queries_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
