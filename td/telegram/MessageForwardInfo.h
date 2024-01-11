//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageOrigin.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class Td;

struct MessageForwardInfo {
  MessageOrigin origin_;
  int32 date_ = 0;
  DialogId from_dialog_id_;
  MessageId from_message_id_;
  string psa_type_;
  bool is_imported_ = false;

  MessageForwardInfo() = default;

  MessageForwardInfo(MessageOrigin &&origin, int32 date, DialogId from_dialog_id, MessageId from_message_id,
                     string &&psa_type, bool is_imported)
      : origin_(std::move(origin))
      , date_(date)
      , from_dialog_id_(from_dialog_id)
      , from_message_id_(from_message_id)
      , psa_type_(std::move(psa_type))
      , is_imported_(is_imported) {
    if (from_dialog_id_.is_valid() != from_message_id_.is_valid()) {
      from_dialog_id_ = DialogId();
      from_message_id_ = MessageId();
    }
  }

  static unique_ptr<MessageForwardInfo> get_message_forward_info(
      Td *td, telegram_api::object_ptr<telegram_api::messageFwdHeader> &&forward_header);

  td_api::object_ptr<td_api::messageForwardInfo> get_message_forward_info_object(Td *td) const;

  td_api::object_ptr<td_api::messageImportInfo> get_message_import_info_object() const;

  void add_dependencies(Dependencies &dependencies) const;

  void add_min_user_ids(vector<UserId> &user_ids) const;

  void add_min_channel_ids(vector<ChannelId> &channel_ids) const;

  int32 get_origin_date() const {
    return date_;
  }

  bool is_imported() const {
    return is_imported_;
  }

  MessageFullId get_origin_message_full_id() const {
    return origin_.get_message_full_id();
  }

  DialogId get_last_dialog_id() const {
    return from_dialog_id_;
  }

  MessageFullId get_last_message_full_id() const {
    return {from_dialog_id_, from_message_id_};
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs);

bool operator!=(const MessageForwardInfo &lhs, const MessageForwardInfo &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const MessageForwardInfo &forward_info);

}  // namespace td
