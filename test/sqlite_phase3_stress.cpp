#include "td/db/DbKey.h"
#include "td/db/SqliteDb.h"

#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace {

td::string make_db_path(const char *prefix) {
  return td::string(prefix) + "_" + std::to_string(td::Random::secure_uint64());
}

void initialize_stress_table(td::SqliteDb &db) {
  db.exec("PRAGMA journal_mode=WAL").ensure();
  db.exec(
        "CREATE TABLE stress_entries(writer_id INTEGER NOT NULL, seq_no INTEGER NOT NULL, payload BLOB NOT NULL, "
        "PRIMARY KEY(writer_id, seq_no))")
      .ensure();
}

void wait_for_start(const std::atomic<bool> &start) {
  while (!start.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
}

void run_concurrent_transaction_stress_case(const char *prefix, const td::DbKey &db_key, int writer_count,
                                            int reader_count, int writes_per_writer, int payload_size,
                                            int reader_tail_checks) {
  const td::int64 expected_total = static_cast<td::int64>(writer_count) * writes_per_writer;

  auto path = make_db_path(prefix);
  SCOPE_EXIT {
    td::SqliteDb::destroy(path).ignore();
  };

  {
    auto db = db_key.is_empty() ? td::SqliteDb::open_with_key(path, true, td::DbKey::empty()).move_as_ok()
                                : td::SqliteDb::change_key(path, true, db_key, td::DbKey::empty()).move_as_ok();
    initialize_stress_table(db);
  }

  const int total_threads = writer_count + reader_count;
  std::atomic<int> ready_threads{0};
  std::atomic<bool> start{false};
  std::atomic<int> completed_writers{0};
  std::vector<td::string> thread_errors(total_threads);
  std::vector<td::int64> reader_terminal_counts(reader_count, -1);
  td::vector<td::thread> threads(total_threads);

  for (int writer_id = 0; writer_id < writer_count; writer_id++) {
    threads[writer_id] = td::thread([&, writer_id] {
      auto r_db = td::SqliteDb::open_with_key(path, false, db_key);
      if (r_db.is_error()) {
        thread_errors[writer_id] = r_db.error().to_string();
        ready_threads.fetch_add(1, std::memory_order_acq_rel);
        return;
      }
      auto db = r_db.move_as_ok();
      auto r_insert = db.get_statement("INSERT INTO stress_entries(writer_id, seq_no, payload) VALUES(?1, ?2, ?3)");
      if (r_insert.is_error()) {
        thread_errors[writer_id] = r_insert.error().to_string();
        ready_threads.fetch_add(1, std::memory_order_acq_rel);
        return;
      }
      auto insert = r_insert.move_as_ok();
      auto payload = td::string(payload_size, static_cast<char>('a' + writer_id));

      ready_threads.fetch_add(1, std::memory_order_acq_rel);
      wait_for_start(start);

      for (int seq_no = 0; seq_no < writes_per_writer; seq_no++) {
        auto status = db.begin_write_transaction();
        if (status.is_error()) {
          thread_errors[writer_id] = status.to_string();
          return;
        }
        status = insert.bind_int32(1, writer_id);
        if (status.is_error()) {
          thread_errors[writer_id] = status.to_string();
          return;
        }
        status = insert.bind_int32(2, seq_no);
        if (status.is_error()) {
          thread_errors[writer_id] = status.to_string();
          return;
        }
        status = insert.bind_string(3, payload);
        if (status.is_error()) {
          thread_errors[writer_id] = status.to_string();
          return;
        }
        status = insert.step();
        if (status.is_error()) {
          thread_errors[writer_id] = status.to_string();
          return;
        }
        status = db.commit_transaction();
        if (status.is_error()) {
          thread_errors[writer_id] = status.to_string();
          return;
        }
        insert.reset();
      }

      completed_writers.fetch_add(1, std::memory_order_acq_rel);
    });
  }

  for (int reader_id = 0; reader_id < reader_count; reader_id++) {
    const int slot = writer_count + reader_id;
    threads[slot] = td::thread([&, reader_id, slot] {
      auto r_db = td::SqliteDb::open_with_key(path, false, db_key);
      if (r_db.is_error()) {
        thread_errors[slot] = r_db.error().to_string();
        ready_threads.fetch_add(1, std::memory_order_acq_rel);
        return;
      }
      auto db = r_db.move_as_ok();
      auto r_select = db.get_statement("SELECT count(*), coalesce(sum(length(payload)), 0) FROM stress_entries");
      if (r_select.is_error()) {
        thread_errors[slot] = r_select.error().to_string();
        ready_threads.fetch_add(1, std::memory_order_acq_rel);
        return;
      }
      auto select = r_select.move_as_ok();

      ready_threads.fetch_add(1, std::memory_order_acq_rel);
      wait_for_start(start);

      td::int64 last_count = 0;
      int tail_checks_left = reader_tail_checks;
      while (completed_writers.load(std::memory_order_acquire) != writer_count || tail_checks_left > 0) {
        auto status = db.begin_read_transaction();
        if (status.is_error()) {
          thread_errors[slot] = status.to_string();
          return;
        }
        status = select.step();
        if (status.is_error()) {
          thread_errors[slot] = status.to_string();
          return;
        }
        if (!select.has_row()) {
          thread_errors[slot] = "reader query returned no row";
          return;
        }

        auto count = select.view_int64(0);
        auto payload_bytes = select.view_int64(1);
        if (count < last_count) {
          thread_errors[slot] = "reader observed non-monotonic committed row count";
          return;
        }
        if (payload_bytes != count * payload_size) {
          thread_errors[slot] = "reader observed payload-length drift under concurrent commits";
          return;
        }

        status = select.step();
        if (status.is_error()) {
          thread_errors[slot] = status.to_string();
          return;
        }
        select.reset();
        status = db.commit_transaction();
        if (status.is_error()) {
          thread_errors[slot] = status.to_string();
          return;
        }

        last_count = count;
        if (completed_writers.load(std::memory_order_acquire) == writer_count) {
          tail_checks_left--;
        }
      }

      reader_terminal_counts[reader_id] = last_count;
    });
  }

  while (ready_threads.load(std::memory_order_acquire) != total_threads) {
    std::this_thread::yield();
  }
  start.store(true, std::memory_order_release);

  for (auto &thread : threads) {
    thread.join();
  }

  for (const auto &thread_error : thread_errors) {
    LOG_CHECK(thread_error.empty()) << thread_error;
  }
  for (auto reader_count_seen : reader_terminal_counts) {
    CHECK(reader_count_seen == expected_total);
  }

  auto db = td::SqliteDb::open_with_key(path, false, db_key).move_as_ok();
  {
    auto stmt = db.get_statement("SELECT count(*), coalesce(sum(length(payload)), 0) FROM stress_entries").move_as_ok();
    stmt.step().ensure();
    CHECK(stmt.has_row());
    CHECK(stmt.view_int64(0) == expected_total);
    CHECK(stmt.view_int64(1) == expected_total * payload_size);
    stmt.step().ensure();
  }
  for (int writer_id = 0; writer_id < writer_count; writer_id++) {
    auto stmt = db.get_statement("SELECT count(*), min(seq_no), max(seq_no) FROM stress_entries WHERE writer_id = ?1")
                    .move_as_ok();
    stmt.bind_int32(1, writer_id).ensure();
    stmt.step().ensure();
    CHECK(stmt.has_row());
    CHECK(stmt.view_int64(0) == writes_per_writer);
    CHECK(stmt.view_int64(1) == 0);
    CHECK(stmt.view_int64(2) == writes_per_writer - 1);
    stmt.step().ensure();
  }
}

}  // namespace

#if !TD_THREAD_UNSUPPORTED
TEST(DB, sqlite_phase3_concurrent_transaction_stress) {
  run_concurrent_transaction_stress_case("sqlite_phase3_tx_stress", td::DbKey::empty(), 4, 2, 128, 64, 32);
}

TEST(DB, sqlite_phase3_encrypted_concurrent_transaction_stress) {
  run_concurrent_transaction_stress_case("sqlite_phase3_tx_stress_encrypted",
                                         td::DbKey::password("phase3-stress-'password'"), 2, 1, 64, 96, 16);
}
#endif