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

TEST(PollMessageIdAdversarial, PollManagerMustNotInlineLegacyMessageLookups) {
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"delete_poll_option\")"));
  ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"set_poll_answer\")"));
  ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"get_poll_voters\")"));
  ASSERT_FALSE(poll_manager_source.contains("get_message_force(message_full_id,\"stop_poll\")"));
}

TEST(PollMessageIdAdversarial, PollManagerMustNotDuplicateMessagesManagerPollValidationErrors) {
  auto poll_manager_source = read_normalized("td/telegram/PollManager.cpp");

  ASSERT_FALSE(poll_manager_source.contains("Status::Error(400,\"Messageisnotapoll\")"));
  ASSERT_FALSE(poll_manager_source.contains("Status::Error(400,\"Wrongpollmessagespecified\")"));
  ASSERT_FALSE(poll_manager_source.contains("Status::Error(400,\"Pollcan'tbestopped\")"));
  ASSERT_FALSE(
      poll_manager_source.contains("have_input_peer(message_full_id.get_dialog_id(),false,AccessRights::Read)"));
}

TEST(PollMessageIdAdversarial, PollContentHelperMustNotLeakPollIdThroughFailOpenBranches) {
  auto message_content_source = read_normalized("td/telegram/MessageContent.cpp");

  ASSERT_FALSE(
      message_content_source.contains("if(content==nullptr){returnstatic_cast<constMessagePoll*>(content)->poll_id;}"));
  ASSERT_FALSE(message_content_source.contains("default:returnstatic_cast<constMessagePoll*>(content)->poll_id;"));
}