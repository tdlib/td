//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/db/detail/RawSqliteDb.h"

#include "sqlite/sqlite3.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/path.h"
#include "td/utils/port/Stat.h"

namespace td {
namespace detail {

Status RawSqliteDb::last_error(sqlite3 *db, CSlice path) {
  return Status::Error(PSLICE() << Slice(sqlite3_errmsg(db)) << " for database \"" << path << '"');
}

Status RawSqliteDb::destroy(Slice path) {
  Status error;
  with_db_path(path, [&](auto path) {
    unlink(path).ignore();
    if (!stat(path).is_error()) {
      error = Status::Error(PSLICE() << "Failed to delete file \"" << path << '"');
    }
  });
  return error;
}

Status RawSqliteDb::last_error() {
  //If database was corrupted, try to delete it.
  auto code = sqlite3_errcode(db_);
  if (code == SQLITE_CORRUPT) {
    destroy(path_).ignore();
  }

  return last_error(db_, path());
}

RawSqliteDb::~RawSqliteDb() {
  auto rc = sqlite3_close(db_);
  LOG_IF(FATAL, rc != SQLITE_OK) << last_error(db_, path());
}

}  // namespace detail
}  // namespace td
