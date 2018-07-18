//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

struct sqlite3;

namespace td {
namespace detail {

class RawSqliteDb {
 public:
  RawSqliteDb(sqlite3 *db, std::string path) : db_(db), path_(std::move(path)) {
  }
  RawSqliteDb(const RawSqliteDb &) = delete;
  RawSqliteDb(RawSqliteDb &&) = delete;
  RawSqliteDb &operator=(const RawSqliteDb &) = delete;
  RawSqliteDb &operator=(RawSqliteDb &&) = delete;
  ~RawSqliteDb();

  template <class F>
  static void with_db_path(Slice main_path, F &&f) {
    f(PSLICE() << main_path);
    f(PSLICE() << main_path << "-journal");
    f(PSLICE() << main_path << "-shm");
    f(PSLICE() << main_path << "-wal");
  }
  static Status destroy(Slice path) TD_WARN_UNUSED_RESULT;

  sqlite3 *db() {
    return db_;
  }
  CSlice path() const {
    return path_;
  }

  Status last_error();
  static Status last_error(sqlite3 *db);

 private:
  sqlite3 *db_;
  std::string path_;
};

}  // namespace detail
}  // namespace td
