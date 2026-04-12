#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"

#include "td/utils/FlatHashMap.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include <limits>
#include <string>

namespace {

td::string make_db_path(const char *prefix) {
  return td::string(prefix) + "_" + std::to_string(td::Random::secure_uint64());
}

int pick_blob_size() {
  switch (td::Random::fast(0, 6)) {
    case 0:
      return 0;
    case 1:
      return 1;
    case 2:
      return 31;
    case 3:
      return 32;
    case 4:
      return 255;
    case 5:
      return 512;
    default:
      return td::Random::fast(0, 384);
  }
}

}  // namespace

TEST(DB, sqlite_phase3_statement_blob_roundtrip_light_fuzz) {
  auto path = make_db_path("sqlite_phase3_fuzz");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  td::FlatHashMap<td::string, td::string> expected;

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE kv(key TEXT PRIMARY KEY, value BLOB NOT NULL)").ensure();

    auto upsert = db.get_statement(
                        "INSERT INTO kv(key, value) VALUES(?1, ?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value")
                      .move_as_ok();
    auto select = db.get_statement("SELECT value FROM kv WHERE key = ?1").move_as_ok();

    for (int i = 0; i < 1024; i++) {
      auto key = td::string("k") + std::to_string(td::Random::fast(0, 31));
      auto value =
          td::rand_string(std::numeric_limits<char>::min(), std::numeric_limits<char>::max(), pick_blob_size());

      upsert.bind_string(1, key).ensure();
      upsert.bind_blob(2, value).ensure();
      upsert.step().ensure();
      upsert.reset();

      expected[key] = value;

      if (i % 17 == 0) {
        select.bind_string(1, key).ensure();
        select.step().ensure();
        CHECK(select.has_row());
        CHECK(select.view_blob(0).str() == expected[key]);
        select.step().ensure();
        select.reset();
      }
    }
  }

  {
    auto reopened = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
    auto select = reopened.get_statement("SELECT value FROM kv WHERE key = ?1").move_as_ok();
    for (const auto &it : expected) {
      select.bind_string(1, it.first).ensure();
      select.step().ensure();
      CHECK(select.has_row());
      CHECK(select.view_blob(0).str() == it.second);
      select.step().ensure();
      select.reset();
    }
  }
}