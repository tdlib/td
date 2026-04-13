#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"

#include "../sqlite/sqlite/sqlite3.h"

#include "td/utils/filesystem.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include "td/utils/port/Stat.h"

#include <string>

namespace {

td::string make_db_path(const char *prefix) {
  return td::string(prefix) + "_" + std::to_string(td::Random::secure_uint64());
}

void seed_kv(td::SqliteDb &db, td::Slice key, td::Slice value) {
  td::SqliteKeyValue kv;
  kv.init_with_connection(db.clone(), "kv").ensure();
  kv.set(key, value);
}

td::string load_kv_value(td::SqliteDb &db, td::Slice key) {
  td::SqliteKeyValue kv;
  kv.init_with_connection(db.clone(), "kv").ensure();
  return kv.get(key);
}

void populate_large_table(td::SqliteDb &db) {
  db.exec("CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT NOT NULL)").ensure();
  auto insert = db.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  for (td::int32 i = 0; i < 256; i++) {
    td::string value(256, static_cast<char>('a' + (i % 26)));
    insert.bind_int32(1, i).ensure();
    insert.bind_string(2, value).ensure();
    insert.step().ensure();
    insert.reset();
  }
}

size_t extract_page_size_from_header(td::Slice bytes) {
  CHECK(bytes.size() >= 18);
  auto high = static_cast<unsigned char>(bytes[16]);
  auto low = static_cast<unsigned char>(bytes[17]);
  size_t page_size = (static_cast<size_t>(high) << 8) | low;
  return page_size == 1 ? 65536 : page_size;
}

void set_be_uint16(td::string &bytes, size_t offset, size_t value) {
  CHECK(value <= 0xffff);
  bytes[offset] = static_cast<char>((value >> 8) & 0xff);
  bytes[offset + 1] = static_cast<char>(value & 0xff);
}

template <class MutatorT>
void expect_corruption_cleanup_after_disk_mutation(const char *prefix, MutatorT &&mutate) {
  auto path = make_db_path(prefix);
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    populate_large_table(db);
  }

  auto bytes = td::read_file_str(path).move_as_ok();
  auto page_size = extract_page_size_from_header(bytes);
  CHECK(bytes.size() > page_size + 32);
  mutate(bytes, page_size);
  td::write_file(path, bytes).ensure();

  auto reopened = td::SqliteDb::open_with_key(path, false, td::DbKey::empty());
  if (reopened.is_ok()) {
    auto db = reopened.move_as_ok();
    auto *native_db = db.get_native();
    auto status = db.exec("SELECT sum(length(value)) FROM items");
    CHECK(status.is_error());
    CHECK(tdsqlite3_errcode(native_db) == SQLITE_CORRUPT);
  } else {
    CHECK(reopened.is_error());
  }

#ifndef _WIN32
  CHECK(td::stat(path).is_error());
  CHECK(td::stat(path + "-journal").is_error());
  CHECK(td::stat(path + "-wal").is_error());
#endif
}

}  // namespace

TEST(DB, sqlite_phase3_wrong_key_rejects_without_mutating_payload) {
  auto path = make_db_path("sqlite_phase3_wrong_key");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto correct_key = td::DbKey::password("phase3-correct");
  auto wrong_key = td::DbKey::password("phase3-wrong");
  auto replacement_key = td::DbKey::raw_key(td::string(32, 'r'));

  {
    auto db = td::SqliteDb::change_key(path, true, correct_key, td::DbKey::empty()).move_as_ok();
    seed_kv(db, "alpha", "payload");
    db.set_user_version(91).ensure();
  }

  td::SqliteDb::open_with_key(path, false, wrong_key).ensure_error();
  CHECK(td::SqliteDb::change_key(path, false, replacement_key, wrong_key).is_error());
  td::SqliteDb::open_with_key(path, false, replacement_key).ensure_error();

  auto db = td::SqliteDb::open_with_key(path, false, correct_key).move_as_ok();
  CHECK(load_kv_value(db, "alpha") == "payload");
  CHECK(db.user_version().ok() == 91);
}

TEST(DB, sqlite_phase3_invalid_bind_does_not_poison_statement_reuse) {
  auto path = make_db_path("sqlite_phase3_invalid_bind");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  auto stmt = db.get_statement("SELECT ?1").move_as_ok();

  CHECK(stmt.bind_string(2, "out-of-range").is_error());
  stmt.reset();
  stmt.bind_string(1, "usable").ensure();
  stmt.step().ensure();
  CHECK(stmt.has_row());
  CHECK(stmt.view_string(0) == "usable");
  stmt.step().ensure();
}

TEST(DB, sqlite_phase3_has_table_rejects_quoted_injection_input) {
  auto path = make_db_path("sqlite_phase3_has_table_injection");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  db.exec("CREATE TABLE safe_table(id INTEGER PRIMARY KEY, value TEXT NOT NULL)").ensure();

  auto injected_name = td::Slice("missing_table' OR 1=1 --");
  CHECK(db.has_table("safe_table").ok());
  CHECK(!db.has_table(injected_name).ok());
}

TEST(DB, sqlite_phase3_bind_string_copies_value_before_step) {
  auto path = make_db_path("sqlite_phase3_bind_string_copy");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  db.exec("CREATE TABLE items(value TEXT NOT NULL)").ensure();

  auto insert = db.get_statement("INSERT INTO items(value) VALUES(?1)").move_as_ok();
  td::string payload = "alpha";
  insert.bind_string(1, payload).ensure();
  payload = "omega";
  insert.step().ensure();

  auto select = db.get_statement("SELECT value FROM items").move_as_ok();
  select.step().ensure();
  CHECK(select.has_row());
  CHECK(select.view_string(0) == "alpha");
}

TEST(DB, sqlite_phase3_bind_blob_copies_value_before_step) {
  auto path = make_db_path("sqlite_phase3_bind_blob_copy");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  db.exec("CREATE TABLE blobs(value BLOB NOT NULL)").ensure();

  auto insert = db.get_statement("INSERT INTO blobs(value) VALUES(?1)").move_as_ok();
  td::string payload("abc\0z", 5);
  insert.bind_blob(1, payload).ensure();
  payload[0] = 'x';
  payload[1] = 'y';
  payload[2] = 'z';
  payload[3] = '\0';
  payload[4] = 'q';
  insert.step().ensure();

  auto select = db.get_statement("SELECT value FROM blobs").move_as_ok();
  select.step().ensure();
  CHECK(select.has_row());
  CHECK(select.view_blob(0) == td::Slice("abc\0z", 5));
}

TEST(DB, sqlite_phase3_constraint_failure_does_not_poison_statement_reuse) {
  auto path = make_db_path("sqlite_phase3_constraint_reuse");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  db.exec("CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT NOT NULL)").ensure();

  auto insert = db.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  insert.bind_int32(1, 1).ensure();
  insert.bind_string(2, "alpha").ensure();
  insert.step().ensure();
  insert.reset();

  insert.bind_int32(1, 1).ensure();
  insert.bind_string(2, "duplicate").ensure();
  CHECK(insert.step().is_error());
  CHECK(tdsqlite3_errcode(db.get_native()) == SQLITE_CONSTRAINT);
  CHECK(tdsqlite3_get_autocommit(db.get_native()) != 0);

  insert.reset();
  insert.bind_int32(1, 2).ensure();
  insert.bind_string(2, "beta").ensure();
  insert.step().ensure();

  auto count_stmt = db.get_statement("SELECT count(*), sum(id) FROM items").move_as_ok();
  count_stmt.step().ensure();
  CHECK(count_stmt.has_row());
  CHECK(count_stmt.view_int64(0) == 2);
  CHECK(count_stmt.view_int64(1) == 3);
  count_stmt.step().ensure();
}

TEST(DB, sqlite_phase3_failed_begin_write_retry_preserves_transaction_isolation) {
  auto path = make_db_path("sqlite_phase3_begin_retry");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    db.exec("PRAGMA journal_mode=WAL").ensure();
    db.exec("CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT NOT NULL)").ensure();
  }

  auto lock_holder = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  auto contended = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  auto observer = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  contended.exec("PRAGMA busy_timeout = 0").ensure();

  lock_holder.begin_write_transaction().ensure();
  auto locked_insert = lock_holder.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  locked_insert.bind_int32(1, 1).ensure();
  locked_insert.bind_string(2, "held").ensure();
  locked_insert.step().ensure();

  CHECK(contended.begin_write_transaction().is_error());

  lock_holder.commit_transaction().ensure();

  contended.begin_write_transaction().ensure();
  auto contended_insert = contended.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  contended_insert.bind_int32(1, 2).ensure();
  contended_insert.bind_string(2, "pending").ensure();
  contended_insert.step().ensure();

  {
    auto count_stmt = observer.get_statement("SELECT count(*) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 1);
    count_stmt.step().ensure();
  }

  contended.commit_transaction().ensure();

  {
    auto count_stmt = observer.get_statement("SELECT count(*), sum(id) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 2);
    CHECK(count_stmt.view_int64(1) == 3);
    count_stmt.step().ensure();
  }
}

TEST(DB, sqlite_phase3_failed_commit_retry_preserves_open_transaction_state) {
  auto path = make_db_path("sqlite_phase3_commit_retry");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT NOT NULL)").ensure();
    db.exec("INSERT INTO items(id, value) VALUES(1, 'seed')").ensure();
  }

  auto writer = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  auto reader = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  writer.exec("PRAGMA busy_timeout = 0").ensure();

  writer.begin_write_transaction().ensure();
  auto writer_insert = writer.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  writer_insert.bind_int32(1, 2).ensure();
  writer_insert.bind_string(2, "pending").ensure();
  writer_insert.step().ensure();

  reader.begin_read_transaction().ensure();
  {
    auto count_stmt = reader.get_statement("SELECT count(*) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 1);
    count_stmt.step().ensure();
  }

  CHECK(writer.commit_transaction().is_error());

  {
    auto count_stmt = reader.get_statement("SELECT count(*) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 1);
    count_stmt.step().ensure();
  }

  reader.commit_transaction().ensure();
  writer.commit_transaction().ensure();

  {
    auto count_stmt = reader.get_statement("SELECT count(*), sum(id) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 2);
    CHECK(count_stmt.view_int64(1) == 3);
    count_stmt.step().ensure();
  }
}

TEST(DB, sqlite_phase3_deferred_foreign_key_commit_failure_can_be_repaired_in_place) {
  auto path = make_db_path("sqlite_phase3_deferred_fk_retry");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    db.exec("PRAGMA foreign_keys = ON").ensure();
    db.exec("CREATE TABLE parent(id INTEGER PRIMARY KEY)").ensure();
    db.exec(
          "CREATE TABLE child(id INTEGER PRIMARY KEY, parent_id INTEGER NOT NULL, FOREIGN KEY(parent_id) REFERENCES "
          "parent(id) DEFERRABLE INITIALLY DEFERRED)")
        .ensure();
  }

  auto writer = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  auto observer = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  writer.exec("PRAGMA foreign_keys = ON").ensure();

  writer.begin_write_transaction().ensure();
  auto insert_child = writer.get_statement("INSERT INTO child(id, parent_id) VALUES(?1, ?2)").move_as_ok();
  insert_child.bind_int32(1, 1).ensure();
  insert_child.bind_int32(2, 42).ensure();
  insert_child.step().ensure();

  CHECK(tdsqlite3_get_autocommit(writer.get_native()) == 0);
  CHECK(writer.commit_transaction().is_error());
  CHECK(tdsqlite3_get_autocommit(writer.get_native()) == 0);

  {
    auto count_stmt = observer.get_statement("SELECT count(*) FROM child").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 0);
    count_stmt.step().ensure();
  }

  auto insert_parent = writer.get_statement("INSERT INTO parent(id) VALUES(?1)").move_as_ok();
  insert_parent.bind_int32(1, 42).ensure();
  insert_parent.step().ensure();

  writer.commit_transaction().ensure();
  CHECK(tdsqlite3_get_autocommit(writer.get_native()) != 0);

  {
    auto count_stmt = observer
                          .get_statement(
                              "SELECT (SELECT count(*) FROM parent), (SELECT count(*) FROM child), (SELECT parent_id "
                              "FROM child WHERE id = 1)")
                          .move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 1);
    CHECK(count_stmt.view_int64(1) == 1);
    CHECK(count_stmt.view_int64(2) == 42);
    count_stmt.step().ensure();
  }
}

TEST(DB, sqlite_phase3_corruption_cleanup_deletes_damaged_database_files_after_malformed_schema) {
  auto path = make_db_path("sqlite_phase3_corrupt");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    populate_large_table(db);
    db.exec("PRAGMA writable_schema = ON").ensure();
    db.exec(
          "UPDATE sqlite_master SET sql='CREATE TABL items(id INTEGER PRIMARY KEY, value TEXT NOT NULL)' WHERE "
          "name='items'")
        .ensure();
    db.exec("PRAGMA writable_schema = OFF").ensure();
  }

  auto reopened = td::SqliteDb::open_with_key(path, false, td::DbKey::empty());
  if (reopened.is_ok()) {
    auto db = reopened.move_as_ok();
    auto *native_db = db.get_native();
    auto status = db.exec("SELECT sum(length(value)) FROM items");
    CHECK(status.is_error());
    CHECK(tdsqlite3_errcode(native_db) == SQLITE_CORRUPT);
  } else {
    CHECK(reopened.is_error());
  }

#ifndef _WIN32
  CHECK(td::stat(path).is_error());
  CHECK(td::stat(path + "-journal").is_error());
  CHECK(td::stat(path + "-wal").is_error());
#endif
}

TEST(DB, sqlite_phase3_corruption_cleanup_deletes_damaged_database_files_after_invalid_leaf_page_type) {
  expect_corruption_cleanup_after_disk_mutation(
      "sqlite_phase3_corrupt_page_type",
      [](td::string &bytes, size_t page_size) { bytes[page_size] = static_cast<char>(0x7f); });
}

TEST(DB, sqlite_phase3_corruption_cleanup_deletes_damaged_database_files_after_invalid_cell_pointer) {
  expect_corruption_cleanup_after_disk_mutation("sqlite_phase3_corrupt_cell_pointer",
                                                [](td::string &bytes, size_t page_size) {
                                                  bytes[page_size + 8] = static_cast<char>(0xff);
                                                  bytes[page_size + 9] = static_cast<char>(0xff);
                                                });
}

TEST(DB,
     sqlite_phase3_corruption_cleanup_deletes_damaged_database_files_after_header_cell_pointer_targets_btree_header) {
  expect_corruption_cleanup_after_disk_mutation(
      "sqlite_phase3_corrupt_header_cell_pointer",
      [](td::string &bytes, size_t page_size) { set_be_uint16(bytes, page_size + 8, 4); });
}

TEST(DB, sqlite_phase3_corruption_cleanup_deletes_damaged_database_files_after_first_cell_pointer_targets_page_tail) {
  expect_corruption_cleanup_after_disk_mutation(
      "sqlite_phase3_corrupt_page_tail_pointer",
      [](td::string &bytes, size_t page_size) { set_be_uint16(bytes, page_size + 8, page_size - 1); });
}

TEST(DB, sqlite_phase3_corruption_cleanup_deletes_damaged_database_files_after_impossible_cell_count_header) {
  expect_corruption_cleanup_after_disk_mutation(
      "sqlite_phase3_corrupt_cell_count",
      [](td::string &bytes, size_t page_size) { set_be_uint16(bytes, page_size + 3, 0xffff); });
}