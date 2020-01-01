//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/db/SqliteDb.h"
#include "td/db/SqliteStatement.h"

#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <unordered_map>

namespace td {

class SqliteKeyValue {
 public:
  static Status drop(SqliteDb &connection, Slice table_name) TD_WARN_UNUSED_RESULT {
    return connection.exec(PSLICE() << "DROP TABLE IF EXISTS " << table_name);
  }

  static Status init(SqliteDb &connection, Slice table_name) TD_WARN_UNUSED_RESULT {
    return connection.exec(PSLICE() << "CREATE TABLE IF NOT EXISTS " << table_name << " (k BLOB PRIMARY KEY, v BLOB)");
  }

  using SeqNo = uint64;

  bool empty() const {
    return db_.empty();
  }

  Result<bool> init(string path) TD_WARN_UNUSED_RESULT;

  Status init_with_connection(SqliteDb connection, string table_name) TD_WARN_UNUSED_RESULT;

  Result<bool> try_regenerate_index() TD_WARN_UNUSED_RESULT {
    return false;
  }

  void close() {
    *this = SqliteKeyValue();
  }

  Status drop();

  SeqNo set(Slice key, Slice value);

  string get(Slice key);

  SeqNo erase(Slice key);

  Status begin_transaction() TD_WARN_UNUSED_RESULT {
    return db_.begin_transaction();
  }
  Status commit_transaction() TD_WARN_UNUSED_RESULT {
    return db_.commit_transaction();
  }

  void erase_by_prefix(Slice prefix);

  std::unordered_map<string, string> get_all() {
    std::unordered_map<string, string> res;
    get_by_prefix("", [&](Slice key, Slice value) {
      res.emplace(key.str(), value.str());
      return true;
    });
    return res;
  }

  template <class CallbackT>
  void get_by_prefix(Slice prefix, CallbackT &&callback) {
    string next;
    if (!prefix.empty()) {
      next = next_prefix(prefix);
    }
    get_by_range(prefix, next, callback);
  }

  template <class CallbackT>
  void get_by_range(Slice from, Slice till, CallbackT &&callback) {
    SqliteStatement *stmt = nullptr;
    if (from.empty()) {
      stmt = &get_all_stmt_;
    } else {
      if (till.empty()) {
        stmt = &get_by_prefix_rare_stmt_;
        stmt->bind_blob(1, till).ensure();
      } else {
        stmt = &get_by_prefix_stmt_;
        stmt->bind_blob(1, from).ensure();
        stmt->bind_blob(2, till).ensure();
      }
    }
    auto guard = stmt->guard();
    stmt->step().ensure();
    while (stmt->has_row()) {
      if (!callback(stmt->view_blob(0), stmt->view_blob(1))) {
        return;
      }
      stmt->step().ensure();
    }
  }

 private:
  string path_;
  string table_name_;
  SqliteDb db_;
  SqliteStatement get_stmt_;
  SqliteStatement set_stmt_;
  SqliteStatement erase_stmt_;
  SqliteStatement get_all_stmt_;
  SqliteStatement erase_by_prefix_stmt_;
  SqliteStatement erase_by_prefix_rare_stmt_;
  SqliteStatement get_by_prefix_stmt_;
  SqliteStatement get_by_prefix_rare_stmt_;

  string next_prefix(Slice prefix);
};

}  // namespace td
