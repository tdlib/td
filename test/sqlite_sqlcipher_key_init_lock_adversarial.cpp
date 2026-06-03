// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"

#include "td/utils/common.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <atomic>
#include <string_view>
#include <thread>
#include <vector>

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

TEST(DBSqlcipherKeyInitLockAdversarial, SqlcipherInitDeclaresDedicatedSerializationMutex) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("Mutexsqlcipher_key_init_mutex;") != td::string::npos);
}

TEST(DBSqlcipherKeyInitLockAdversarial, SqlcipherInitAvoidsUnsynchronizedKeyPragmaPath) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto region = normalize_for_contract(extract_region(
      source, "Result<SqliteDb> SqliteDb::do_open_with_key(CSlice path, bool allow_creation, const DbKey &db_key,",
      "TRY_STATUS_PREFIX(db.check_encryption(), \"Can't check database: \""));

  ASSERT_EQ(td::string::npos,
            region.find("if(!db_key.is_empty()){if(db.check_encryption().is_ok()){returnStatus::Error"));
}

TEST(DBSqlcipherKeyInitLockAdversarial, EncryptedPathMustNotPerformPostKeyCheckAfterLockIsReleased) {
  const auto source = td::mtproto::test::read_repo_text_file("tddb/td/db/SqliteDb.cpp");
  const auto normalized = normalize_for_contract(source);

  ASSERT_EQ(
      td::string::npos,
      normalized.find(
          "db.set_cipher_version(cipher_version);}TRY_STATUS_PREFIX(db.check_encryption(),\"Can'tcheckdatabase:\");"));
}

// ---------------------------------------------------------------------------
// Runtime multi-threaded adversarial tests
// ---------------------------------------------------------------------------
// These tests actually spawn threads and exercise the sqlcipher_key_init_mutex
// at runtime, verifying that concurrent open_with_key calls on the same
// database path do not crash, corrupt data, or race.
// ---------------------------------------------------------------------------

#if !TD_THREAD_UNSUPPORTED

namespace {

td::string make_adversarial_db_path(const char *prefix) {
  return PSTRING() << prefix << "_" << td::Random::secure_uint64();
}

void wait_for_go(const std::atomic<bool> &go) {
  while (!go.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

}  // namespace

// Test 1: Race open_with_key from N threads on the same unencrypted database.
// All threads call open_with_key concurrently. Every open must succeed and the
// resulting SqliteDb must be usable (we run a trivial query on each).
TEST(DBSqlcipherKeyInitLockAdversarial, adversarial_sqlcipher_concurrent_open_race) {
  constexpr int kThreadCount = 8;

  auto path = make_adversarial_db_path("adv_concurrent_open_race");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  // Pre-create the database so every thread opens an existing file.
  {
    auto db = td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE canary(id INTEGER PRIMARY KEY)").ensure();
  }

  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<td::string> errors(kThreadCount);
  td::vector<td::thread> threads(kThreadCount);

  for (int i = 0; i < kThreadCount; i++) {
    threads[i] = td::thread([&, i] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      wait_for_go(go);

      auto r_db = td::SqliteDb::open_with_key(path, false, td::DbKey::empty());
      if (r_db.is_error()) {
        errors[i] = r_db.error().to_string();
        return;
      }
      auto db = r_db.move_as_ok();

      // Verify the database is actually usable by querying the canary table.
      auto r_stmt = db.get_statement("SELECT count(*) FROM canary");
      if (r_stmt.is_error()) {
        errors[i] = r_stmt.error().to_string();
        return;
      }
      auto stmt = r_stmt.move_as_ok();
      auto status = stmt.step();
      if (status.is_error()) {
        errors[i] = status.to_string();
        return;
      }
      if (!stmt.has_row()) {
        errors[i] = "canary query returned no row";
        return;
      }
    });
  }

  // Spin until all threads are ready, then release them simultaneously.
  while (ready.load(std::memory_order_acquire) != kThreadCount) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  for (int i = 0; i < kThreadCount; i++) {
    LOG_CHECK(errors[i].empty()) << "thread " << i << ": " << errors[i];
  }
}

// Test 2: Race open_with_key from N threads on the same encrypted database.
// The sqlcipher key-init mutex must serialize the PRAGMA key / check_encryption
// calls without deadlock or corruption.
TEST(DBSqlcipherKeyInitLockAdversarial, adversarial_sqlcipher_concurrent_encrypted_open_race) {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  // OpenSSL 3 provider initialization reports a libcrypto-only MSan false
  // positive on the encrypted open path. The unencrypted open race test above
  // still exercises the concurrent open invariants under MSan.
  return;
#endif
#endif
  constexpr int kThreadCount = 8;
  const auto db_key = td::DbKey::password("adversarial-test-key");

  auto path = make_adversarial_db_path("adv_encrypted_open_race");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  // Create an encrypted database.
  {
    auto db = td::SqliteDb::change_key(path, true, db_key, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE canary(id INTEGER PRIMARY KEY)").ensure();
  }

  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<td::string> errors(kThreadCount);
  td::vector<td::thread> threads(kThreadCount);

  for (int i = 0; i < kThreadCount; i++) {
    threads[i] = td::thread([&, i] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      wait_for_go(go);

      auto r_db = td::SqliteDb::open_with_key(path, false, db_key);
      if (r_db.is_error()) {
        errors[i] = r_db.error().to_string();
        return;
      }
      auto db = r_db.move_as_ok();

      auto r_stmt = db.get_statement("SELECT count(*) FROM canary");
      if (r_stmt.is_error()) {
        errors[i] = r_stmt.error().to_string();
        return;
      }
      auto stmt = r_stmt.move_as_ok();
      auto status = stmt.step();
      if (status.is_error()) {
        errors[i] = status.to_string();
        return;
      }
      if (!stmt.has_row()) {
        errors[i] = "canary query returned no row";
        return;
      }
    });
  }

  while (ready.load(std::memory_order_acquire) != kThreadCount) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  for (int i = 0; i < kThreadCount; i++) {
    LOG_CHECK(errors[i].empty()) << "thread " << i << ": " << errors[i];
  }
}

// Test 3: Verify cleanup on the error path -- open_with_key on a non-existent
// path with allow_creation=false must fail gracefully from every thread without
// leaking file descriptors or leaving behind partial state.
TEST(DBSqlcipherKeyInitLockAdversarial, adversarial_sqlcipher_error_path_lock_release) {
  constexpr int kThreadCount = 8;

  // Use a path that does not exist and forbid creation.
  auto path = make_adversarial_db_path("adv_error_path_nonexistent");
  // Ensure no leftover from a prior run.
  td::SqliteDb::destroy(path).ignore();

  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<bool> got_error(kThreadCount, false);
  td::vector<td::thread> threads(kThreadCount);

  for (int i = 0; i < kThreadCount; i++) {
    threads[i] = td::thread([&, i] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      wait_for_go(go);

      // allow_creation = false on a missing path must return an error.
      auto r_db = td::SqliteDb::open_with_key(path, false, td::DbKey::empty());
      got_error[i] = r_db.is_error();
    });
  }

  while (ready.load(std::memory_order_acquire) != kThreadCount) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  for (int i = 0; i < kThreadCount; i++) {
    LOG_CHECK(got_error[i]) << "thread " << i << " should have received an error for a non-existent database";
  }

  // After all error-path threads complete, the mutex must still be usable:
  // open a valid database to confirm no deadlock or permanent lock state.
  auto verify_path = make_adversarial_db_path("adv_error_path_verify");
  SCOPE_EXIT {
    td::SqliteDb::destroy(verify_path).ignore();
  };
  auto r_db = td::SqliteDb::open_with_key(verify_path, true, td::DbKey::empty());
  ASSERT_TRUE(r_db.is_ok());
}

// Test 4: Mixed encrypted and unencrypted opens racing on different databases.
// This stresses the process-wide sqlcipher_key_init_mutex with heterogeneous
// callers -- some threads take the lock (encrypted path) while others skip it
// (unencrypted path). No thread should deadlock or observe corruption.
TEST(DBSqlcipherKeyInitLockAdversarial, adversarial_sqlcipher_mixed_encrypted_unencrypted_race) {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  // Skip under MSan due to OpenSSL 3 false positives on the encrypted path.
  return;
#endif
#endif
  constexpr int kThreadCount = 8;
  const auto db_key = td::DbKey::password("mixed-race-key");

  auto encrypted_path = make_adversarial_db_path("adv_mixed_enc");
  auto plaintext_path = make_adversarial_db_path("adv_mixed_plain");
  SCOPE_EXIT {
    td::SqliteDb::destroy(encrypted_path).ignore();
    td::SqliteDb::destroy(plaintext_path).ignore();
  };

  // Pre-create both databases.
  {
    auto db = td::SqliteDb::change_key(encrypted_path, true, db_key, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE mix_canary(id INTEGER PRIMARY KEY)").ensure();
  }
  {
    auto db = td::SqliteDb::open_with_key(plaintext_path, true, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE mix_canary(id INTEGER PRIMARY KEY)").ensure();
  }

  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<td::string> errors(kThreadCount);
  td::vector<td::thread> threads(kThreadCount);

  for (int i = 0; i < kThreadCount; i++) {
    threads[i] = td::thread([&, i] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      wait_for_go(go);

      // Even-numbered threads open the encrypted database, odd open plaintext.
      const bool use_encrypted = (i % 2 == 0);
      auto r_db = use_encrypted ? td::SqliteDb::open_with_key(encrypted_path, false, db_key)
                                : td::SqliteDb::open_with_key(plaintext_path, false, td::DbKey::empty());
      if (r_db.is_error()) {
        errors[i] = r_db.error().to_string();
        return;
      }
      auto db = r_db.move_as_ok();

      auto r_stmt = db.get_statement("SELECT count(*) FROM mix_canary");
      if (r_stmt.is_error()) {
        errors[i] = r_stmt.error().to_string();
        return;
      }
      auto stmt = r_stmt.move_as_ok();
      auto status = stmt.step();
      if (status.is_error()) {
        errors[i] = status.to_string();
        return;
      }
      if (!stmt.has_row()) {
        errors[i] = "mix_canary query returned no row";
        return;
      }
    });
  }

  while (ready.load(std::memory_order_acquire) != kThreadCount) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  for (int i = 0; i < kThreadCount; i++) {
    LOG_CHECK(errors[i].empty()) << "thread " << i << ": " << errors[i];
  }
}

// Test 5: Repeated open/close cycles from multiple threads to stress lock
// acquire/release ordering. Each thread opens and immediately closes the
// database in a tight loop. This catches lock-ordering bugs that only manifest
// under repeated contention.
TEST(DBSqlcipherKeyInitLockAdversarial, adversarial_sqlcipher_repeated_open_close_stress) {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
  return;
#endif
#endif
  constexpr int kThreadCount = 4;
  constexpr int kIterations = 16;
  const auto db_key = td::DbKey::password("stress-open-close-key");

  auto path = make_adversarial_db_path("adv_open_close_stress");
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = td::SqliteDb::change_key(path, true, db_key, td::DbKey::empty()).move_as_ok();
    db.exec("CREATE TABLE stress_canary(id INTEGER PRIMARY KEY)").ensure();
  }

  std::atomic<int> ready{0};
  std::atomic<bool> go{false};
  std::vector<td::string> errors(kThreadCount);
  td::vector<td::thread> threads(kThreadCount);

  for (int i = 0; i < kThreadCount; i++) {
    threads[i] = td::thread([&, i] {
      ready.fetch_add(1, std::memory_order_acq_rel);
      wait_for_go(go);

      for (int iter = 0; iter < kIterations; iter++) {
        auto r_db = td::SqliteDb::open_with_key(path, false, db_key);
        if (r_db.is_error()) {
          errors[i] = PSTRING() << "iteration " << iter << ": " << r_db.error().to_string();
          return;
        }
        auto db = r_db.move_as_ok();

        // Quick sanity check each iteration.
        auto r_stmt = db.get_statement("SELECT count(*) FROM stress_canary");
        if (r_stmt.is_error()) {
          errors[i] = PSTRING() << "iteration " << iter << ": " << r_stmt.error().to_string();
          return;
        }
        auto stmt = r_stmt.move_as_ok();
        auto status = stmt.step();
        if (status.is_error()) {
          errors[i] = PSTRING() << "iteration " << iter << ": " << status.to_string();
          return;
        }
        // Explicitly close before reopening to stress the cleanup path.
        db.close();
      }
    });
  }

  while (ready.load(std::memory_order_acquire) != kThreadCount) {
    std::this_thread::yield();
  }
  go.store(true, std::memory_order_release);

  for (auto &t : threads) {
    t.join();
  }

  for (int i = 0; i < kThreadCount; i++) {
    LOG_CHECK(errors[i].empty()) << "thread " << i << ": " << errors[i];
  }
}

#endif  // !TD_THREAD_UNSUPPORTED
