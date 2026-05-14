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

TEST(PollOptionMoveLightFuzz, RandomizedProbeOrderKeepsMovedPollOptionEntryPointsPinned) {
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  const std::array<td::Slice, 7> required = {
      td::Slice("voidadd_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::inputPollOption>"
                "&&option,Promise<Unit>&&promise);"),
      td::Slice("voiddelete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,Promise<Unit>"
                "&&promise);"),
      td::Slice("voidPollManager::add_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::"
                "inputPollOption>&&option,Promise<Unit>&&promise){TRY_STATUS_PROMISE(promise,td_->messages_"
                "manager_->check_can_add_poll_option(message_full_id));if(option==nullptr){"),
      td::Slice("td_->create_handler<AddPollAnswerQuery>(std::move(promise))->send(message_full_id,poll_option);"),
      td::Slice("voidPollManager::delete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,Promise<"
                "Unit>&&promise){autor_poll_id=td_->messages_manager_->get_message_poll_id(message_full_id,false);"
                "if(r_poll_id.is_error()){returnpromise.set_error(r_poll_id.move_as_error());}td_->create_handler<"
                "DeletePollAnswerQuery>(std::move(promise))->send(message_full_id,option_id);"),
      td::Slice("td_->poll_manager_->add_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},"
                "std::move(request.option_),std::move(promise));"),
      td::Slice("td_->poll_manager_->delete_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)}"
                ",request.option_id_,std::move(promise));"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    auto snippet = required[idx];
    bool found = poll_manager_header.contains(snippet.str()) || poll_manager_source.contains(snippet.str()) ||
                 requests_source.contains(snippet.str());
    ASSERT_TRUE(found);
  }
}

TEST(PollOptionMoveLightFuzz, RandomizedProbeOrderKeepsLegacyPollOptionRoutingPatternsAbsent) {
  auto message_content_header = read_normalized("td/telegram/MessageContent.h");
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");
  auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");
  auto requests_source = read_normalized("td/telegram/Requests.cpp");

  const std::array<td::Slice, 11> forbidden = {
      td::Slice("add_message_content_poll_option("),
      td::Slice("delete_message_content_poll_option("),
      td::Slice("voidMessagesManager::add_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::"
                "inputPollOption>&&option,Promise<Unit>&&promise){"),
      td::Slice("voidMessagesManager::delete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,"
                "Promise<Unit>&&promise){"),
      td::Slice("td_->messages_manager_->add_poll_option({DialogId(request.chat_id_),MessageId(request.message_"
                "id_)},"),
      td::Slice("td_->messages_manager_->delete_poll_option({DialogId(request.chat_id_),MessageId(request.message_"
                "id_)},"),
      td::Slice("voidadd_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,td_api::object_ptr<td_api::"
                "inputPollOption>&&option,Promise<Unit>&&promise);"),
      td::Slice("voiddelete_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,conststring&option_id,Promise<"
                "Unit>&&promise);"),
      td::Slice("voidPollManager::add_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,td_api::object_ptr<"
                "td_api::inputPollOption>&&option,Promise<Unit>&&promise){"),
      td::Slice("voidPollManager::delete_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,conststring&"
                "option_id,Promise<Unit>&&promise){"),
      td::Slice("get_message_force(message_full_id,\"delete_poll_option\")"),
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