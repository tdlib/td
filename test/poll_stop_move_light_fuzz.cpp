// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string read_normalized(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

}  // namespace

TEST(PollStopMoveLightFuzz, RandomizedProbeOrderKeepsMovedStopPollEntryPointPinned) {
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  const std::array<td::Slice, 5> required = {
      td::Slice("voidstop_poll(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::ReplyMarkup>&&reply_markup,"
                "Promise<Unit>&&promise);"),
      td::Slice("voidPollManager::stop_poll(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::ReplyMarkup>"
                "&&reply_markup,Promise<Unit>&&promise){TRY_RESULT_PROMISE(promise,poll_id,td_->messages_"
                "manager_->get_message_poll_id(message_full_id,true));if(get_poll_is_closed(poll_id)){"),
      td::Slice("if(is_local_poll_id(poll_id)){LOG(ERROR)<<\"Receivelocal\"<<poll_id<<\"from\"<<message_full_id"
                "<<\"instop_poll\";stop_local_poll(poll_id);promise.set_value(Unit());return;}"),
      td::Slice("do_stop_poll(poll_id,message_full_id,std::move(new_reply_markup),0,std::move(promise));"),
      td::Slice("td_->poll_manager_->stop_poll({DialogId(request.chat_id_),MessageId(request.message_id_)},std::"
                "move(request.reply_markup_),std::move(promise));"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    auto snippet = required[idx];
    bool found = poll_manager_header.contains(snippet.str()) || poll_manager_source.contains(snippet.str()) ||
                 requests_source.contains(snippet.str());
    ASSERT_TRUE(found);
  }
}

TEST(PollStopMoveLightFuzz, RandomizedProbeOrderKeepsLegacyStopPollPatternsAbsent) {
  auto message_content_header = read_normalized("td/telegram/MessageContent.h");
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");
  auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  const std::array<td::Slice, 6> forbidden = {
      td::Slice("stop_message_content_poll("),
      td::Slice("voidMessagesManager::stop_poll(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::ReplyMarkup>"
                "&&reply_markup,Promise<Unit>&&promise){"),
      td::Slice("td_->messages_manager_->stop_poll({DialogId(request.chat_id_),MessageId(request.message_id_)},"),
      td::Slice("voidstop_poll(PollIdpoll_id,MessageFullIdmessage_full_id,td_api::object_ptr<td_api::ReplyMarkup>"
                "&&reply_markup,Promise<Unit>&&promise);"),
      td::Slice("voidPollManager::stop_poll(PollIdpoll_id,MessageFullIdmessage_full_id,td_api::object_ptr<td_api::"
                "ReplyMarkup>&&reply_markup,Promise<Unit>&&promise){"),
      td::Slice("get_message_force(message_full_id,\"stop_poll\")"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    auto snippet = forbidden[idx];
    bool found = message_content_header.contains(snippet.str()) || message_content_source.contains(snippet.str()) ||
                 messages_manager_header.contains(snippet.str()) || messages_manager_source.contains(snippet.str()) ||
                 poll_manager_header.contains(snippet.str()) || poll_manager_source.contains(snippet.str()) ||
                 requests_source.contains(snippet.str());
    ASSERT_FALSE(found);
  }
}