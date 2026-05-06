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

TEST(DialogFilterGetChatFolderHardeningContract, CoalescesConcurrentLoadsForSameFolder) {
  auto header = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.h");
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");

  ASSERT_TRUE(header.find("pending_get_dialog_filter_queries_") != td::string::npos);

  auto get_dialog_filter_region = extract_region(source, "void DialogFilterManager::get_dialog_filter(",
                                                 "void DialogFilterManager::on_load_dialog_filter_result(");
  ASSERT_TRUE(get_dialog_filter_region.find("pending_get_dialog_filter_queries_[dialog_filter_id]") !=
              td::string::npos);
  ASSERT_TRUE(get_dialog_filter_region.find("pending_query.is_loading") != td::string::npos);
  ASSERT_TRUE(get_dialog_filter_region.find("Coalesce getChatFolder request") != td::string::npos);
  ASSERT_TRUE(get_dialog_filter_region.find("Begin getChatFolder load") != td::string::npos);
  ASSERT_TRUE(get_dialog_filter_region.find("&DialogFilterManager::on_load_dialog_filter_result") != td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningContract, FanoutPathLogsAndPreservesPromiseSemantics) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto finish_region = extract_region(source, "void DialogFilterManager::on_load_dialog_filter_result(",
                                      "void DialogFilterManager::on_load_dialog_filter(");

  ASSERT_TRUE(finish_region.find("Failed to load getChatFolder") != td::string::npos);
  ASSERT_TRUE(finish_region.find("Finish getChatFolder load") != td::string::npos);
  ASSERT_TRUE(finish_region.find("promise.set_error(status.clone())") != td::string::npos);
  ASSERT_TRUE(finish_region.find("on_load_dialog_filter(dialog_filter_id, std::move(promise))") != td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningContract, QueueHasExplicitUpperBoundForCoalescedWaiters) {
  auto header = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.h");
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");

  ASSERT_TRUE(header.find("MAX_PENDING_GET_DIALOG_FILTER_PROMISES") != td::string::npos);

  auto get_dialog_filter_region = extract_region(source, "void DialogFilterManager::get_dialog_filter(",
                                                 "void DialogFilterManager::on_load_dialog_filter_result(");
  ASSERT_TRUE(get_dialog_filter_region.find(
                  "pending_query.promises.size() >= MAX_PENDING_GET_DIALOG_FILTER_PROMISES") != td::string::npos);
  ASSERT_TRUE(get_dialog_filter_region.find("Too Many Requests: retry after 1") != td::string::npos);
  ASSERT_TRUE(get_dialog_filter_region.find("Reject getChatFolder because waiter queue reached limit") !=
              td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningContract, NonRateLimitUpdateFailureSchedulesImmediateReload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto update_region = extract_region(source, "void DialogFilterManager::on_update_dialog_filter(",
                                      "void DialogFilterManager::delete_dialog_filter(");

  ASSERT_TRUE(update_region.find("result.code() != 429") != td::string::npos);
  ASSERT_TRUE(update_region.find("schedule_dialog_filters_reload(0.0)") != td::string::npos);
  ASSERT_TRUE(update_region.find("Reload chat folders after non-rate-limit update failure") != td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningContract, NonRateLimitDeleteFailureSchedulesImmediateReload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto delete_region = extract_region(source, "void DialogFilterManager::on_delete_dialog_filter(",
                                      "void DialogFilterManager::get_leave_dialog_filter_suggestions(");

  ASSERT_TRUE(delete_region.find("result.code() != 429") != td::string::npos);
  ASSERT_TRUE(delete_region.find("schedule_dialog_filters_reload(0.0)") != td::string::npos);
  ASSERT_TRUE(delete_region.find("Reload chat folders after non-rate-limit delete failure") != td::string::npos);
}

TEST(DialogFilterGetChatFolderHardeningContract, NonRateLimitReorderFailureSchedulesImmediateReload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogFilterManager.cpp");
  auto reorder_region = extract_region(source, "void DialogFilterManager::on_reorder_dialog_filters(",
                                       "void DialogFilterManager::toggle_dialog_filter_tags(");

  ASSERT_TRUE(reorder_region.find("result.code() != 429") != td::string::npos);
  ASSERT_TRUE(reorder_region.find("schedule_dialog_filters_reload(0.0)") != td::string::npos);
  ASSERT_TRUE(reorder_region.find("Reload chat folders after non-rate-limit reorder failure") != td::string::npos);
}

}  // namespace
