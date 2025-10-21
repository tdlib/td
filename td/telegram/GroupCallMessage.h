//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Td;

class GroupCallMessage {
  int64 id_ = 0;
  DialogId dialog_id_;
  FormattedText text_;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallMessage &group_call_message);

 public:
  GroupCallMessage(Td *td, DialogId sender_dialog_id, string json_message);

  GroupCallMessage(Td *td, telegram_api::object_ptr<telegram_api::groupCallMessage> &&message);

  GroupCallMessage(DialogId dialog_id, FormattedText text);

  bool is_valid() const {
    return dialog_id_.is_valid() && !text_.text.empty() && id_ != 0;
  }

  int64 get_message_id() const {
    return id_;
  }

  td_api::object_ptr<td_api::groupCallMessage> get_group_call_message_object(Td *td) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallMessage &group_call_message);

}  // namespace td
