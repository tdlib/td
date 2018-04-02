//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/SchedulerLocalStorage.h"

#include "td/db/SqliteDb.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"

namespace td {

class SqliteConnectionSafe {
 public:
  SqliteConnectionSafe() = default;
  explicit SqliteConnectionSafe(string name, DbKey key = DbKey::empty())
      : lsls_connection_([name = name, key = std::move(key)] {
        auto r_db = SqliteDb::open_with_key(name, key);
        if (r_db.is_error()) {
          LOG(FATAL) << "Can't open database " << name << ": " << r_db.error();
        }
        auto db = r_db.move_as_ok();
        db.exec("PRAGMA synchronous=NORMAL").ensure();
        db.exec("PRAGMA temp_store=MEMORY").ensure();
        db.exec("PRAGMA secure_delete=1").ensure();
        db.exec("PRAGMA recursive_triggers=1").ensure();
        return db;
      })
      , name_(std::move(name)) {
  }

  SqliteDb &get() {
    return lsls_connection_.get();
  }

  void close() {
    LOG(INFO) << "Close sqlite db " << tag("path", name_);
    lsls_connection_.clear_values();
  }
  void close_and_destroy() {
    close();
    LOG(INFO) << "Destroy sqlite db " << tag("path", name_);
    SqliteDb::destroy(name_).ignore();
  }

 private:
  LazySchedulerLocalStorage<SqliteDb> lsls_connection_;
  string name_;
};

}  // namespace td
