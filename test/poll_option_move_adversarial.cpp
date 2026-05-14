// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

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

TEST(PollOptionMoveAdversarial, MessagesManagerMustNotRetainLegacyAddOrDeletePollOptionOwnership) {
  auto messages_manager_header = read_normalized("td/telegram/MessagesManager.h");
  auto messages_manager_source = read_normalized("td/telegram/MessagesManager.cpp");

  ASSERT_FALSE(messages_manager_header.contains(
      "voidadd_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::inputPollOption>&&option,"
      "Promise<Unit>&&promise);"));
  ASSERT_FALSE(messages_manager_header.contains(
      "voiddelete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,Promise<Unit>&&promise);"));
  ASSERT_FALSE(messages_manager_source.contains(
      "voidMessagesManager::add_poll_option(MessageFullIdmessage_full_id,td_api::object_ptr<td_api::inputPollOption>"
      "&&option,Promise<Unit>&&promise){"));
  ASSERT_FALSE(messages_manager_source.contains(
      "voidMessagesManager::delete_poll_option(MessageFullIdmessage_full_id,conststring&option_id,Promise<Unit>"
      "&&promise){"));
}

TEST(PollOptionMoveAdversarial, MessageContentMustNotExposeLegacyPollOptionMutationHelpers) {
  auto message_content_header = read_normalized("td/telegram/MessageContent.h");
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");

  ASSERT_FALSE(message_content_header.contains("add_message_content_poll_option("));
  ASSERT_FALSE(message_content_header.contains("delete_message_content_poll_option("));
  ASSERT_FALSE(message_content_source.contains("add_message_content_poll_option("));
  ASSERT_FALSE(message_content_source.contains("delete_message_content_poll_option("));
}

TEST(PollOptionMoveAdversarial, RequestsAndPollManagerMustNotReintroduceLegacyRoutingOrSignatures) {
  auto requests_source = read_normalized("td/telegram/Requests.cpp");
  auto poll_manager_header = read_normalized("td/telegram/PollManager.h");
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_FALSE(requests_source.contains(
      "td_->messages_manager_->add_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},"));
  ASSERT_FALSE(requests_source.contains(
      "td_->messages_manager_->delete_poll_option({DialogId(request.chat_id_),MessageId(request.message_id_)},"));
  ASSERT_FALSE(poll_manager_header.contains(
      "voidadd_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,td_api::object_ptr<td_api::inputPollOption>"
      "&&option,Promise<Unit>&&promise);"));
  ASSERT_FALSE(poll_manager_header.contains(
      "voiddelete_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,conststring&option_id,Promise<Unit>"
      "&&promise);"));
  ASSERT_FALSE(poll_manager_source.contains(
      "voidPollManager::add_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,td_api::object_ptr<td_api::"
      "inputPollOption>&&option,Promise<Unit>&&promise){"));
  ASSERT_FALSE(poll_manager_source.contains(
      "voidPollManager::delete_poll_option(PollIdpoll_id,MessageFullIdmessage_full_id,conststring&option_id,"
      "Promise<Unit>&&promise){"));
}