#include "data.h"

#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"

#include "td/utils/base64.h"
#include "td/utils/filesystem.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include <string>

namespace {

td::string make_db_path(const char *prefix) {
  return td::string(prefix) + "_" + std::to_string(td::Random::secure_uint64());
}

void initialize_items_table(td::SqliteDb &db) {
  db.exec("CREATE TABLE items(id INTEGER PRIMARY KEY, value TEXT NOT NULL)").ensure();
}

void insert_item(td::SqliteDb &db, td::int32 id, td::Slice value) {
  auto stmt = db.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  stmt.bind_int32(1, id).ensure();
  stmt.bind_string(2, value).ensure();
  stmt.step().ensure();
}

td::string select_item(td::SqliteDb &db, td::int32 id) {
  auto stmt = db.get_statement("SELECT value FROM items WHERE id = ?1").move_as_ok();
  stmt.bind_int32(1, id).ensure();
  stmt.step().ensure();
  CHECK(stmt.has_row());
  auto value = stmt.view_string(0).str();
  stmt.step().ensure();
  return value;
}

void write_legacy_sample(td::CSlice path, const char *encoded, size_t encoded_size) {
  td::write_file(path, td::base64_decode(td::Slice(encoded, encoded_size)).move_as_ok()).ensure();
}

td::string load_kv_value(td::SqliteDb &db, td::Slice key) {
  td::SqliteKeyValue kv;
  kv.init_with_connection(db.clone(), "kv").ensure();
  return kv.get(key);
}

}  // namespace

TEST(DB, sqlite_phase3_unencrypted_open_read_write_contract) {
  auto path = make_db_path("sqlite_phase3_plain");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    CHECK(!db.get_cipher_version());
    initialize_items_table(db);
    insert_item(db, 1, "alpha");
    db.set_user_version(77).ensure();
    CHECK(select_item(db, 1) == "alpha");
  }

  {
    auto reopened = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
    CHECK(!reopened.get_cipher_version());
    CHECK(reopened.user_version().ok() == 77);
    CHECK(select_item(reopened, 1) == "alpha");
  }
}

TEST(DB, sqlite_phase3_encrypted_open_read_write_contract) {
  auto path = make_db_path("sqlite_phase3_encrypted");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto key = td::DbKey::password("phase3-password");
  auto wrong_key = td::DbKey::password("phase3-wrong-password");

  {
    auto db = td::SqliteDb::change_key(path, true, key, td::DbKey::empty()).move_as_ok();
    CHECK(db.get_cipher_version() == 0);
    initialize_items_table(db);
    insert_item(db, 4, "ciphertext-backed");
    db.set_user_version(55).ensure();
  }

  td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).ensure_error();
  td::SqliteDb::open_with_key(path, false, wrong_key).ensure_error();

  auto reopened = td::SqliteDb::open_with_key(path, false, key).move_as_ok();
  CHECK(reopened.get_cipher_version() == 0);
  CHECK(reopened.user_version().ok() == 55);
  CHECK(select_item(reopened, 4) == "ciphertext-backed");
}

TEST(DB, sqlite_phase3_statement_prepare_bind_step_reset_contract) {
  auto path = make_db_path("sqlite_phase3_statement");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  auto stmt = db.get_statement("SELECT ?1 || ':' || ?2").move_as_ok();

  stmt.bind_string(1, "alpha").ensure();
  stmt.bind_string(2, "beta").ensure();
  stmt.step().ensure();
  CHECK(stmt.has_row());
  CHECK(stmt.view_string(0) == "alpha:beta");
  stmt.step().ensure();
  CHECK(!stmt.can_step());
  CHECK(stmt.step().is_error());

  stmt.reset();
  CHECK(stmt.can_step());
  stmt.bind_string(1, "gamma").ensure();
  stmt.bind_string(2, "delta").ensure();
  stmt.step().ensure();
  CHECK(stmt.has_row());
  CHECK(stmt.view_string(0) == "gamma:delta");
  stmt.step().ensure();
  CHECK(!stmt.can_step());
}

TEST(DB, sqlite_phase3_statement_reset_clears_existing_bindings_contract) {
  auto path = make_db_path("sqlite_phase3_statement_reset");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
  auto stmt = db.get_statement("SELECT ?1, ?2").move_as_ok();

  stmt.bind_string(1, "alpha").ensure();
  stmt.bind_string(2, "beta").ensure();
  stmt.step().ensure();
  CHECK(stmt.has_row());
  CHECK(stmt.view_string(0) == "alpha");
  CHECK(stmt.view_string(1) == "beta");
  stmt.step().ensure();

  stmt.reset();
  stmt.bind_string(1, "gamma").ensure();
  stmt.step().ensure();
  CHECK(stmt.has_row());
  CHECK(stmt.view_string(0) == "gamma");
  CHECK(stmt.view_datatype(1) == td::SqliteStatement::Datatype::Null);
  stmt.step().ensure();
}

TEST(DB, sqlite_phase3_cipher_compatibility_fallback_records_legacy_v3) {
  auto path = make_db_path("sqlite_phase3_legacy_v3");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto cucumber = td::DbKey::password("cucumber");
  write_legacy_sample(path, sqlite_sample_db_v3, sqlite_sample_db_v3_size);

  auto db = td::SqliteDb::open_with_key(path, false, cucumber).move_as_ok();
  CHECK(db.get_cipher_version() == 3);
  CHECK(load_kv_value(db, "hello") == "world");
  CHECK(db.user_version().ok() == 123);
}

TEST(DB, sqlite_phase3_cipher_compatibility_prefers_native_v4) {
  auto path = make_db_path("sqlite_phase3_legacy_v4");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto cucumber = td::DbKey::password("cucu'\"mb er");
  write_legacy_sample(path, sqlite_sample_db_v4, sqlite_sample_db_v4_size);

  auto db = td::SqliteDb::open_with_key(path, false, cucumber).move_as_ok();
  CHECK(db.get_cipher_version() == 0);
  CHECK(load_kv_value(db, "hello") == "world");
  CHECK(db.user_version().ok() == 123);
}

TEST(DB, sqlite_phase3_change_key_roundtrip_preserves_payload_and_user_version) {
  auto path = make_db_path("sqlite_phase3_rekey");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto empty = td::DbKey::empty();
  auto password_key = td::DbKey::password("phase3-password");
  auto raw_key = td::DbKey::raw_key(td::string(32, 'q'));

  {
    auto db = td::SqliteDb::open_with_key(path, true, empty).move_as_ok();
    initialize_items_table(db);
    insert_item(db, 7, "payload");
    db.set_user_version(91).ensure();
  }

  {
    auto encrypted = td::SqliteDb::change_key(path, false, password_key, empty).move_as_ok();
    CHECK(select_item(encrypted, 7) == "payload");
    CHECK(encrypted.user_version().ok() == 91);
  }

  {
    auto rekeyed = td::SqliteDb::change_key(path, false, raw_key, password_key).move_as_ok();
    CHECK(select_item(rekeyed, 7) == "payload");
    CHECK(rekeyed.user_version().ok() == 91);
  }

  {
    auto decrypted = td::SqliteDb::change_key(path, false, empty, raw_key).move_as_ok();
    CHECK(select_item(decrypted, 7) == "payload");
    CHECK(decrypted.user_version().ok() == 91);
  }
}

TEST(DB, sqlite_phase3_change_key_roundtrip_supports_quoted_database_path_contract) {
  auto path = make_db_path("sqlite_phase3_rekey_path") + "_'semi;--";
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  auto empty = td::DbKey::empty();
  auto password_key = td::DbKey::password("phase3-'password'");
  auto raw_key = td::DbKey::raw_key(td::string(32, 'k'));

  {
    auto db = td::SqliteDb::open_with_key(path, true, empty).move_as_ok();
    initialize_items_table(db);
    insert_item(db, 9, "quoted-path");
    db.set_user_version(33).ensure();
  }

  {
    auto encrypted = td::SqliteDb::change_key(path, false, password_key, empty).move_as_ok();
    CHECK(select_item(encrypted, 9) == "quoted-path");
    CHECK(encrypted.user_version().ok() == 33);
  }

  {
    auto rekeyed = td::SqliteDb::change_key(path, false, raw_key, password_key).move_as_ok();
    CHECK(select_item(rekeyed, 9) == "quoted-path");
    CHECK(rekeyed.user_version().ok() == 33);
  }

  {
    auto decrypted = td::SqliteDb::change_key(path, false, empty, raw_key).move_as_ok();
    CHECK(select_item(decrypted, 9) == "quoted-path");
    CHECK(decrypted.user_version().ok() == 33);
  }
}

TEST(DB, sqlite_phase3_nested_write_transaction_commits_only_on_outermost_commit_contract) {
  auto path = make_db_path("sqlite_phase3_nested_write");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    db.exec("PRAGMA journal_mode=WAL").ensure();
    initialize_items_table(db);
  }

  auto writer = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();
  auto observer = td::SqliteDb::open_with_key(path, false, td::DbKey::empty()).move_as_ok();

  writer.begin_write_transaction().ensure();
  writer.begin_write_transaction().ensure();

  auto insert = writer.get_statement("INSERT INTO items(id, value) VALUES(?1, ?2)").move_as_ok();
  insert.bind_int32(1, 1).ensure();
  insert.bind_string(2, "alpha").ensure();
  insert.step().ensure();

  writer.commit_transaction().ensure();

  {
    auto count_stmt = observer.get_statement("SELECT count(*) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 0);
    count_stmt.step().ensure();
  }

  writer.commit_transaction().ensure();

  {
    auto count_stmt = observer.get_statement("SELECT count(*), sum(id) FROM items").move_as_ok();
    count_stmt.step().ensure();
    CHECK(count_stmt.has_row());
    CHECK(count_stmt.view_int64(0) == 1);
    CHECK(count_stmt.view_int64(1) == 1);
    count_stmt.step().ensure();
  }
}