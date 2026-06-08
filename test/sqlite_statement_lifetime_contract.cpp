// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"

#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
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

td::string make_db_path(const char *prefix) {
  return PSTRING() << prefix << "_" << td::Random::secure_uint64();
}

}  // namespace

TEST(SqliteStatementLifetimeContract, DestructorFinalizesStatementBeforeReleasingDatabaseHandle) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteStatement.cpp");
  const auto region = normalize_for_contract(
      extract_region(source, "SqliteStatement::~SqliteStatement()", "Result<string> SqliteStatement::explain()"));

  const auto stmt_reset_pos = region.find("stmt_.reset();");
  const auto db_reset_pos = region.find("db_.reset();");

  ASSERT_TRUE(stmt_reset_pos != td::string::npos);
  ASSERT_TRUE(db_reset_pos != td::string::npos);
  ASSERT_TRUE(stmt_reset_pos < db_reset_pos);
}

TEST(SqliteStatementLifetimeContract, ExplicitCloseWithLiveEncryptedStatementSurvivesStatementScopeExit) {
  const auto db_key = td::DbKey::password("sqlite-statement-lifetime-key");
  auto path = make_db_path("stmt_lifetime");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::change_key(path, true, db_key, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE lifetime_canary(value INTEGER NOT NULL)").ensure();
    db.exec("INSERT INTO lifetime_canary(value) VALUES(7)").ensure();
  }

  {
    auto db = td::SqliteDb::open_with_key(path, false, db_key).move_as_ok();
    {
      auto stmt = db.get_statement("SELECT value FROM lifetime_canary").move_as_ok();
      stmt.step().ensure();
      ASSERT_TRUE(stmt.has_row());
      ASSERT_EQ(stmt.view_int32(0), 7);
      db.close();
    }
  }

  auto reopened = td::SqliteDb::open_with_key(path, false, db_key).move_as_ok();
  auto stmt = reopened.get_statement("SELECT count(*) FROM lifetime_canary").move_as_ok();
  stmt.step().ensure();
  ASSERT_TRUE(stmt.has_row());
  ASSERT_EQ(stmt.view_int32(0), 1);
}
