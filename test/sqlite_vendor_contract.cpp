// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "../sqlite/sqlite/sqlite3.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/tests.h"

#include <cstring>

TEST(DB, sqlite_vendor_wrapper_surface_contract) {
  CHECK(std::strcmp(SQLITE_VERSION, tdsqlite3_libversion()) == 0);
  CHECK(tdsqlite3_compileoption_used("ENABLE_FTS5") != 0);
  CHECK(tdsqlite3_compileoption_used("HAS_CODEC") != 0);

  tdsqlite3 *db = nullptr;
  auto open_rc =
      tdsqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_MEMORY, nullptr);
  CHECK(open_rc == SQLITE_OK);
  SCOPE_EXIT {
    tdsqlite3_close(db);
  };

  tdsqlite3_stmt *stmt = nullptr;
  auto prepare_rc = tdsqlite3_prepare_v2(db, "SELECT ?1 || ?2", -1, &stmt, nullptr);
  CHECK(prepare_rc == SQLITE_OK);
  SCOPE_EXIT {
    tdsqlite3_finalize(stmt);
  };

  CHECK(tdsqlite3_bind_text(stmt, 1, "tele", -1, SQLITE_STATIC) == SQLITE_OK);
  CHECK(tdsqlite3_bind_text(stmt, 2, "gram", -1, SQLITE_STATIC) == SQLITE_OK);
  CHECK(tdsqlite3_step(stmt) == SQLITE_ROW);

  td::Slice value(reinterpret_cast<const char *>(tdsqlite3_column_text(stmt, 0)));
  CHECK(value == "telegram");
}