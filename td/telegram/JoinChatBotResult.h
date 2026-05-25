//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class JoinChatBotResult {
 public:
  explicit JoinChatBotResult(telegram_api::object_ptr<telegram_api::JoinChatBotResult> &&result);

  explicit JoinChatBotResult(const td_api::object_ptr<td_api::ChatJoinRequestResult> &result);

  td_api::object_ptr<td_api::ChatJoinRequestResult> get_chat_join_request_result_object() const;

  telegram_api::object_ptr<telegram_api::JoinChatBotResult> get_input_join_chat_bot_result() const;

 private:
  enum class Type : int32 { Approved, Declined, Queued };

  friend StringBuilder &operator<<(StringBuilder &string_builder, const JoinChatBotResult &result);

  Type type_ = Type::Queued;
};

StringBuilder &operator<<(StringBuilder &string_builder, const JoinChatBotResult &result);

}  // namespace td
