// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//
#pragma once

#include "td/utils/optional.h"
#include "td/utils/port/Mutex.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Status.h"

struct tdsqlite3;

namespace td {
namespace detail {

class RawSqliteDb {
 public:
  RawSqliteDb(tdsqlite3 *db, std::string path) : db_(db), path_(std::move(path)) {
  }
  RawSqliteDb(const RawSqliteDb &) = delete;
  RawSqliteDb(RawSqliteDb &&) = delete;
  RawSqliteDb &operator=(const RawSqliteDb &) = delete;
  RawSqliteDb &operator=(RawSqliteDb &&) = delete;
  ~RawSqliteDb();

  static Mutex::Guard lock_sqlcipher_key_init_mutex() TD_WARN_UNUSED_RESULT;

  template <class F>
  static void with_db_path(Slice main_path, F &&f) {
    f(PSLICE() << main_path);
    f(PSLICE() << main_path << "-journal");
    f(PSLICE() << main_path << "-wal");
    f(PSLICE() << main_path << "-shm");
  }
  static Status destroy(Slice path) TD_WARN_UNUSED_RESULT;

  tdsqlite3 *db() {
    return db_;
  }
  CSlice path() const {
    return path_;
  }

  Status last_error();
  static Status last_error(tdsqlite3 *db, CSlice path);

  static bool was_any_database_destroyed();

  bool need_begin() const {
    return begin_cnt_ == 0;
  }
  void on_begin_success() {
    begin_cnt_++;
  }
  Result<bool> need_commit() const {
    if (begin_cnt_ == 0) {
      return Status::Error("No matching begin for commit");
    }
    return begin_cnt_ == 1;
  }
  void on_commit_success() {
    CHECK(begin_cnt_ != 0);
    begin_cnt_--;
  }

  void set_cipher_version(int32 cipher_version) {
    cipher_version_ = cipher_version;
  }

  void set_close_under_sqlcipher_key_init_mutex() {
    close_under_sqlcipher_key_init_mutex_ = true;
  }

  optional<int32> get_cipher_version() const {
    return cipher_version_.copy();
  }

 private:
  void close();

  tdsqlite3 *db_;
  std::string path_;
  size_t begin_cnt_{0};
  optional<int32> cipher_version_;
  bool close_under_sqlcipher_key_init_mutex_{false};
};

}  // namespace detail
}  // namespace td
