//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/db/SqliteDb.h"
#include "td/db/SqliteStatement.h"

#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <unordered_map>

namespace td {
class SqliteKeyValue {
 public:
  static Status drop(SqliteDb &connection, Slice kv_name) TD_WARN_UNUSED_RESULT {
    return connection.exec(PSLICE() << "DROP TABLE IF EXISTS " << kv_name);
  }

  static Status init(SqliteDb &connection, Slice kv_name) TD_WARN_UNUSED_RESULT {
    return connection.exec(PSLICE() << "CREATE TABLE IF NOT EXISTS " << kv_name << " (k BLOB PRIMARY KEY, v BLOB)");
  }

  using SeqNo = uint64;
  Result<bool> init(string name) TD_WARN_UNUSED_RESULT {
    name_ = std::move(name);
    bool is_created = false;
    SqliteDb db;
    TRY_STATUS(db.init(name, &is_created));
    TRY_STATUS(db.exec("PRAGMA encoding=\"UTF-8\""));
    TRY_STATUS(db.exec("PRAGMA synchronous=NORMAL"));
    TRY_STATUS(db.exec("PRAGMA journal_mode=WAL"));
    TRY_STATUS(db.exec("PRAGMA temp_store=MEMORY"));
    TRY_STATUS(init_with_connection(std::move(db), "KV"));
    return is_created;
  }

  Status init_with_connection(SqliteDb connection, string kv_name) {
    db_ = std::move(connection);
    kv_name_ = std::move(kv_name);
    TRY_STATUS(init(db_, kv_name_));
    TRY_STATUS(db_.exec(PSLICE() << "CREATE TABLE IF NOT EXISTS " << kv_name_ << " (k BLOB PRIMARY KEY, v BLOB)"));

    TRY_RESULT(set_stmt, db_.get_statement(PSLICE() << "REPLACE INTO " << kv_name_ << " (k, v) VALUES (?1, ?2)"));
    set_stmt_ = std::move(set_stmt);
    TRY_RESULT(get_stmt, db_.get_statement(PSLICE() << "SELECT v FROM " << kv_name_ << " WHERE k = ?1"));
    get_stmt_ = std::move(get_stmt);
    TRY_RESULT(erase_stmt, db_.get_statement(PSLICE() << "DELETE FROM " << kv_name_ << " WHERE k = ?1"));
    erase_stmt_ = std::move(erase_stmt);
    TRY_RESULT(get_all_stmt, db_.get_statement(PSLICE() << "SELECT k, v FROM " << kv_name_ << ""));

    TRY_RESULT(erase_by_prefix_stmt,
               db_.get_statement(PSLICE() << "DELETE FROM " << kv_name_ << " WHERE ?1 <= k AND k < ?2"));
    erase_by_prefix_stmt_ = std::move(erase_by_prefix_stmt);

    TRY_RESULT(erase_by_prefix_rare_stmt,
               db_.get_statement(PSLICE() << "DELETE FROM " << kv_name_ << " WHERE ?1 <= k"));
    erase_by_prefix_rare_stmt_ = std::move(erase_by_prefix_rare_stmt);

    TRY_RESULT(get_by_prefix_stmt,
               db_.get_statement(PSLICE() << "SELECT k, v FROM " << kv_name_ << " WHERE ?1 <= k AND k < ?2"));
    get_by_prefix_stmt_ = std::move(get_by_prefix_stmt);

    TRY_RESULT(get_by_prefix_rare_stmt,
               db_.get_statement(PSLICE() << "SELECT k, v FROM " << kv_name_ << " WHERE ?1 <= k"));
    get_by_prefix_rare_stmt_ = std::move(get_by_prefix_rare_stmt);

    get_all_stmt_ = std::move(get_all_stmt);
    return Status::OK();
  }

  Result<bool> try_regenerate_index() TD_WARN_UNUSED_RESULT {
    return false;
  }
  void close() {
    clear();
  }
  void close_silent() {
    clear();
  }
  static Status destroy(Slice name) {
    return SqliteDb::destroy(name);
  }
  void close_and_destroy() {
    db_.exec(PSLICE() << "DROP TABLE IF EXISTS " << kv_name_).ensure();
    auto name = std::move(name_);
    clear();
    if (!name.empty()) {
      SqliteDb::destroy(name).ignore();
    }
  }

  SeqNo set(Slice key, Slice value) {
    set_stmt_.bind_blob(1, key).ensure();
    set_stmt_.bind_blob(2, value).ensure();
    set_stmt_.step().ensure();
    set_stmt_.reset();
    return 0;
  }

  SeqNo erase(Slice key) {
    erase_stmt_.bind_blob(1, key).ensure();
    erase_stmt_.step().ensure();
    erase_stmt_.reset();
    return 0;
  }
  string get(Slice key) {
    SCOPE_EXIT {
      get_stmt_.reset();
    };
    get_stmt_.bind_blob(1, key).ensure();
    get_stmt_.step().ensure();
    if (!get_stmt_.has_row()) {
      return "";
    }
    auto data = get_stmt_.view_blob(0).str();
    get_stmt_.step().ignore();
    return data;
  }

  Status begin_transaction() {
    return db_.begin_transaction();
  }
  Status commit_transaction() {
    return db_.commit_transaction();
  }

  void erase_by_prefix(Slice prefix) {
    auto next = next_prefix(prefix);
    if (next.empty()) {
      SCOPE_EXIT {
        erase_by_prefix_rare_stmt_.reset();
      };
      erase_by_prefix_rare_stmt_.bind_blob(1, prefix).ensure();
      erase_by_prefix_rare_stmt_.step().ensure();
    } else {
      SCOPE_EXIT {
        erase_by_prefix_stmt_.reset();
      };
      erase_by_prefix_stmt_.bind_blob(1, prefix).ensure();
      erase_by_prefix_stmt_.bind_blob(2, next).ensure();
      erase_by_prefix_stmt_.step().ensure();
    }
  };

  std::unordered_map<string, string> get_all() {
    std::unordered_map<string, string> res;
    get_by_prefix("", [&](Slice key, Slice value) { res.emplace(key.str(), value.str()); });
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
      callback(stmt->view_blob(0), stmt->view_blob(1));
      stmt->step().ensure();
    }
  }

  void clear() {
    *this = SqliteKeyValue();
  }

 private:
  string name_;  // deprecated
  string kv_name_;
  SqliteDb db_;
  SqliteStatement get_stmt_;
  SqliteStatement set_stmt_;
  SqliteStatement erase_stmt_;
  SqliteStatement get_all_stmt_;
  SqliteStatement erase_by_prefix_stmt_;
  SqliteStatement erase_by_prefix_rare_stmt_;
  SqliteStatement get_by_prefix_stmt_;
  SqliteStatement get_by_prefix_rare_stmt_;

  string next_prefix(Slice prefix) {
    string next = prefix.str();
    size_t pos = next.size();
    while (pos) {
      pos--;
      auto value = static_cast<uint8>(next[pos]);
      value++;
      next[pos] = static_cast<char>(value);
      if (value != 0) {
        return next;
      }
    }
    return string{};
  }
};
}  // namespace td
