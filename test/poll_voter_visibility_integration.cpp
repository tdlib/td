// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

TEST(PollVoterVisibilityIntegration, MessagePollSerializationUsesPollManagerPollObject) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessageContent.cpp");
  auto region = extract_region(source, "td_api::object_ptr<td_api::MessageContent> get_message_content_object(",
                               "td_api::object_ptr<td_api::upgradeGiftResult>");

  ASSERT_TRUE(region.find("poll_manager->get_poll_object(m->poll_id, is_server)") != td::string::npos);
}

TEST(PollVoterVisibilityIntegration, PublicPollObjectPathDelegatesToSharedBuilder) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.cpp");
  auto region =
      extract_region(source, "td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id) const {",
                     "td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id, const Poll *poll");

  ASSERT_TRUE(region.find("get_poll_object(poll_id, poll, true)") != td::string::npos);
}

TEST(PollVoterVisibilityIntegration, PollManagerPublicInterfaceExposesRealMessageContextOverload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollManager.h");

  ASSERT_TRUE(
      source.find(
          "td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id, bool is_real_message_content) const;") !=
      td::string::npos);
}

}  // namespace