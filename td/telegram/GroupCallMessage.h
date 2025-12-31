//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
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
  int64 random_id_ = 0;
  int32 server_id_ = 0;
  int32 date_ = 0;
  DialogId sender_dialog_id_;
  FormattedText text_;
  int64 paid_message_star_count_ = 0;
  bool from_admin_ = false;
  bool is_local_ = false;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallMessage &group_call_message);

 public:
  GroupCallMessage(Td *td, DialogId sender_dialog_id, string json_message);

  GroupCallMessage(Td *td, telegram_api::object_ptr<telegram_api::groupCallMessage> &&message);

  GroupCallMessage(DialogId sender_dialog_id, FormattedText text, int64 paid_message_star_count, bool from_admin);

  bool is_valid() const {
    return sender_dialog_id_.is_valid();
  }

  bool is_local() const {
    return is_local_;
  }

  bool is_reaction() const {
    return text_.text.empty();
  }

  bool is_from_admin() const {
    return from_admin_;
  }

  int32 get_server_id() const {
    return server_id_;
  }

  int64 get_random_id() const {
    return random_id_;
  }

  DialogId get_sender_dialog_id() const {
    return sender_dialog_id_;
  }

  int32 get_date() const {
    return date_;
  }

  int64 get_paid_message_star_count() const {
    return paid_message_star_count_;
  }

  string encode_to_json() const;

  td_api::object_ptr<td_api::groupCallMessage> get_group_call_message_object(Td *td, int32 message_id,
                                                                             bool can_be_deleted) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallMessage &group_call_message);

}  // namespace td
