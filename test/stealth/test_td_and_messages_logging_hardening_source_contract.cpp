// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

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

TEST(TdAndMessagesLoggingHardeningSourceContract, NullResult404MappingIncludesRequestContextLog) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/Td.cpp");
  auto send_result_region =
      extract_region(source, "void Td::send_result(uint64 id, tl_object_ptr<td_api::Object> object)",
                     "void Td::send_error_impl(uint64 id, tl_object_ptr<td_api::error> error)");

  ASSERT_TRUE(send_result_region.find("produced no object result; returning 404 Not Found") != td::string::npos);
  ASSERT_TRUE(send_result_region.find("request_type=") != td::string::npos);
  ASSERT_TRUE(send_result_region.find("td_api::make_object<td_api::error>(404, \"Not Found\")") != td::string::npos);
}

TEST(TdAndMessagesLoggingHardeningSourceContract, GetMessageSkipReasonsAreExplicitlyLogged) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/MessagesManager.cpp");
  auto get_message_region =
      extract_region(source, "void MessagesManager::get_message_force_from_server(",
                     "void MessagesManager::get_message(MessageFullId message_full_id, Promise<Unit> &&promise)");

  ASSERT_TRUE(get_message_region.find("Skip server GetMessage because message is already known locally") !=
              td::string::npos);
  ASSERT_TRUE(get_message_region.find("Skip server GetMessage because message is marked as deleted") !=
              td::string::npos);
  ASSERT_TRUE(get_message_region.find("Skip server GetMessage because secret chats can't be fetched from server") !=
              td::string::npos);
  ASSERT_TRUE(get_message_region.find("Skip server GetMessage for future non-channel message") != td::string::npos);
}

TEST(TdAndMessagesLoggingHardeningSourceContract, DroppedUnknownRequestErrorsAreExplicitlyLogged) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/Td.cpp");
  auto send_error_region =
      extract_region(source, "void Td::send_error_impl(uint64 id, tl_object_ptr<td_api::error> error)",
                     "void Td::send_error(uint64 id, Status error)");

  ASSERT_TRUE(send_error_region.find("Drop error for unknown request") != td::string::npos);
  ASSERT_TRUE(send_error_region.find("oneline(to_string(error))") != td::string::npos);
}

}  // namespace
