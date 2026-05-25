//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/JoinChatBotResult.h"

#include "td/utils/logging.h"

namespace td {

JoinChatBotResult::JoinChatBotResult(telegram_api::object_ptr<telegram_api::JoinChatBotResult> &&result) {
  switch (result->get_id()) {
    case telegram_api::joinChatBotResultApproved::ID:
      type_ = Type::Approved;
      break;
    case telegram_api::joinChatBotResultDeclined::ID:
      type_ = Type::Declined;
      break;
    case telegram_api::joinChatBotResultQueued::ID:
      type_ = Type::Queued;
      break;
    case telegram_api::joinChatBotResultWebView::ID:
      LOG(ERROR) << "Receive " << to_string(result);
      break;
    default:
      UNREACHABLE();
  }
}

JoinChatBotResult::JoinChatBotResult(const td_api::object_ptr<td_api::ChatJoinRequestResult> &result) {
  if (result == nullptr) {
    return;
  }
  switch (result->get_id()) {
    case td_api::chatJoinRequestResultApproved::ID:
      type_ = Type::Approved;
      break;
    case td_api::chatJoinRequestResultDeclined::ID:
      type_ = Type::Declined;
      break;
    case td_api::chatJoinRequestResultQueued::ID:
      type_ = Type::Queued;
      break;
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::ChatJoinRequestResult> JoinChatBotResult::get_join_chat_bot_result_object() const {
  switch (type_) {
    case Type::Approved:
      return td_api::make_object<td_api::chatJoinRequestResultApproved>();
    case Type::Declined:
      return td_api::make_object<td_api::chatJoinRequestResultDeclined>();
    case Type::Queued:
      return td_api::make_object<td_api::chatJoinRequestResultQueued>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

telegram_api::object_ptr<telegram_api::JoinChatBotResult> JoinChatBotResult::get_input_join_chat_bot_result() const {
  switch (type_) {
    case Type::Approved:
      return telegram_api::make_object<telegram_api::joinChatBotResultApproved>();
    case Type::Declined:
      return telegram_api::make_object<telegram_api::joinChatBotResultDeclined>();
    case Type::Queued:
      return telegram_api::make_object<telegram_api::joinChatBotResultQueued>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const JoinChatBotResult &result) {
  switch (result.type_) {
    case JoinChatBotResult::Type::Approved:
      return string_builder << "JoinRequestApproved";
    case JoinChatBotResult::Type::Declined:
      return string_builder << "JoinRequestDeclined";
    case JoinChatBotResult::Type::Queued:
      return string_builder << "JoinRequestQueued";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
