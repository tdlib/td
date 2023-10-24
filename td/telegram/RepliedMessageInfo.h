//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageOrigin.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Td;

class RepliedMessageInfo {
 public:
  MessageId reply_to_message_id_;
  DialogId reply_in_dialog_id_;  // DialogId() if reply is to a message in the same chat
  int32 reply_date_ = 0;         // for replies in other chats
  MessageOrigin reply_origin_;   // for replies in other chats

  friend bool operator==(const RepliedMessageInfo &lhs, const RepliedMessageInfo &rhs);

  RepliedMessageInfo() = default;

  explicit RepliedMessageInfo(MessageId reply_to_message_id) : reply_to_message_id_(reply_to_message_id) {
  }

  RepliedMessageInfo(Td *td, tl_object_ptr<telegram_api::messageReplyHeader> &&reply_header, DialogId dialog_id,
                     MessageId message_id, int32 date);

  bool is_same_chat_reply() const {
    return reply_in_dialog_id_ == DialogId() && reply_date_ == 0;
  }

  bool is_empty() const {
    return reply_to_message_id_ == MessageId() && reply_in_dialog_id_ == DialogId() && reply_date_ == 0 &&
           reply_origin_.is_empty();
  }

  MessageId get_same_chat_reply_to_message_id() const;

  MessageFullId get_reply_message_full_id() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const RepliedMessageInfo &lhs, const RepliedMessageInfo &rhs);

bool operator!=(const RepliedMessageInfo &lhs, const RepliedMessageInfo &rhs);

}  // namespace td
