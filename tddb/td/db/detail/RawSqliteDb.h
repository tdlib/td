//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
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
    f(PSLICE() << main_path << "-wal");
    f(PSLICE() << main_path << "-shm");
  }
  static Status destroy(Slice path) TD_WARN_UNUSED_RESULT;

  sqlite3 *db() {
    return db_;
  }
  CSlice path() const {
    return path_;
  }

  Status last_error();
  static Status last_error(sqlite3 *db, CSlice path);

  bool on_begin() {
    begin_cnt_++;
    return begin_cnt_ == 1;
  }
  Result<bool> on_commit() {
    if (begin_cnt_ == 0) {
      return Status::Error("No matching begin for commit");
    }
    begin_cnt_--;
    return begin_cnt_ == 0;
  }

 private:
  sqlite3 *db_;
  std::string path_;
  size_t begin_cnt_{0};
};

}  // namespace detail
}  // namespace td
