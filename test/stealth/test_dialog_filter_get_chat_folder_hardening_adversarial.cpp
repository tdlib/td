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

TEST(DialogFilterGetChatFolderHardeningAdversarial, HangupFailsQueuedGetChatFolderPromises) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto hangup_region =
      extract_region(source, "void DialogFilterManager::hangup()", "void DialogFilterManager::tear_down()");

  ASSERT_TRUE(hangup_region.find("pending_get_dialog_filter_queries_") != td::string::npos);
  ASSERT_TRUE(hangup_region.find("fail_promises(pending_query.second.promises") != td::string::npos);
  ASSERT_TRUE(hangup_region.find("pending_get_dialog_filter_queries_.clear()") != td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningAdversarial, MissingPendingQueueIsTreatedAsInternalInvariantBreach) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto finish_region = extract_region(source, "void DialogFilterManager::on_load_dialog_filter_result(",
                                      "void DialogFilterManager::on_load_dialog_filter(");

  ASSERT_TRUE(finish_region.find("Missing pending getChatFolder queue") != td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningAdversarial, DialogPreloadErrorsEmitStructuredContextLogs) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto query_region = extract_region(source, "class GetDialogsQuery final", "class GetSuggestedDialogFiltersQuery");

  ASSERT_TRUE(query_region.find("GetChatFolder preload tolerates single-dialog 400 error") != td::string::npos);
  ASSERT_TRUE(query_region.find("GetChatFolder preload query failed") != td::string::npos);
  ASSERT_TRUE(query_region.find("is_single=") != td::string::npos);
}

}  // namespace
