//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/db/DbKey.h"
#include "td/db/SqliteStatement.h"

#include "td/db/detail/RawSqliteDb.h"

#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <memory>

struct sqlite3;

namespace td {

class SqliteDb {
 public:
  SqliteDb() = default;
  explicit SqliteDb(CSlice path) {
    auto status = init(path);
    LOG_IF(FATAL, status.is_error()) << status;
  }
  SqliteDb(SqliteDb &&) = default;
  SqliteDb &operator=(SqliteDb &&) = default;
  SqliteDb(const SqliteDb &) = delete;
  SqliteDb &operator=(const SqliteDb &) = delete;
  ~SqliteDb();

  // dangerous
  SqliteDb clone() const {
    return SqliteDb(raw_);
  }

  bool empty() const {
    return !raw_;
  }
  void close() {
    *this = SqliteDb();
  }

  Status init(CSlice path, bool *was_created = nullptr) TD_WARN_UNUSED_RESULT;
  Status exec(CSlice cmd) TD_WARN_UNUSED_RESULT;
  Result<bool> has_table(Slice table);
  Result<string> get_pragma(Slice name);
  Status begin_transaction() TD_WARN_UNUSED_RESULT;
  Status commit_transaction() TD_WARN_UNUSED_RESULT;

  Result<int32> user_version();
  Status set_user_version(int32 version) TD_WARN_UNUSED_RESULT;
  void trace(bool flag);

  static Status destroy(Slice path) TD_WARN_UNUSED_RESULT;

  // Anyway we can't change the key on the fly, so having static functions is more than enough
  static Result<SqliteDb> open_with_key(CSlice path, const DbKey &db_key);
  static Status change_key(CSlice path, const DbKey &new_db_key, const DbKey &old_db_key);

  Status last_error();

  sqlite3 *get_native() const {
    return raw_->db();
  }

  Result<SqliteStatement> get_statement(CSlice statement) TD_WARN_UNUSED_RESULT;

  template <class F>
  static void with_db_path(Slice main_path, F &&f) {
    detail::RawSqliteDb::with_db_path(main_path, f);
  }

 private:
  explicit SqliteDb(std::shared_ptr<detail::RawSqliteDb> raw) : raw_(std::move(raw)) {
  }
  std::shared_ptr<detail::RawSqliteDb> raw_;
  bool enable_logging_ = false;

  Status check_encryption();
};

}  // namespace td
