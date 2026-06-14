// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    const auto b = static_cast<unsigned char>(c);
    if (b == ' ' || b == '\t' || b == '\r' || b == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  const auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  const auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

}  // namespace

TEST(DBSqlitePhase3StressContract, ReaderLoopRoutesSnapshotReadsThroughBusyRetryHelper) {
  const auto source = td::mtproto::test::read_repo_text_file("test/sqlite_phase3_stress.cpp");
  const auto reader_loop = normalize_for_contract(extract_region(
      source, "td::int64 last_count = 0;", "reader_terminal_counts[reader_id] = last_count;"));

  ASSERT_TRUE(
      reader_loop.find("StressReadSnapshotsnapshot;autostatus=read_stress_snapshot_with_busy_retry(db,select,snapshot);") !=
      td::string::npos);
  ASSERT_TRUE(reader_loop.find("db.begin_read_transaction();") == td::string::npos);
  ASSERT_TRUE(reader_loop.find("select.step();") == td::string::npos);
  ASSERT_TRUE(reader_loop.find("db.commit_transaction();") == td::string::npos);
}

TEST(DBSqlitePhase3StressContract, BusyRetryHelperResetsStatementBeforeTransactionCleanup) {
  const auto source = td::mtproto::test::read_repo_text_file("test/sqlite_phase3_stress.cpp");
  const auto helper = normalize_for_contract(extract_region(
      source, "td::Status finish_read_snapshot_attempt(", "td::Status read_stress_snapshot_once("));
  const auto read_once = normalize_for_contract(extract_region(
      source, "td::Status read_stress_snapshot_once(", "td::Status read_stress_snapshot_with_busy_retry("));

  ASSERT_TRUE(helper.find("select.reset();") != td::string::npos);
  ASSERT_TRUE(helper.find("autocleanup_status=db.commit_transaction();") != td::string::npos);
  ASSERT_TRUE(helper.find("select.reset();") < helper.find("autocleanup_status=db.commit_transaction();"));
  ASSERT_TRUE(read_once.find("retryable_busy=is_database_locked(status);returnfinish_read_snapshot_attempt(") !=
              td::string::npos);
}
