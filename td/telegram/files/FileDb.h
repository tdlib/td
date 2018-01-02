//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/files/FileLocation.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/logging.h"
#include "td/utils/Status.h"

#include <memory>

namespace td {
class SqliteDb;
class SqliteConnectionSafe;
class SqliteKeyValue;
}  // namespace td

namespace td {
Status drop_file_db(SqliteDb &db, int32 version) TD_WARN_UNUSED_RESULT;
Status init_file_db(SqliteDb &db, int32 version) TD_WARN_UNUSED_RESULT;

class FileDbInterface;
std::shared_ptr<FileDbInterface> create_file_db(std::shared_ptr<SqliteConnectionSafe> connection,
                                                int32 scheduler_id = -1) TD_WARN_UNUSED_RESULT;

using FileDbId = uint64;

class FileDbInterface {
 public:
  using Id = FileDbId;
  FileDbInterface() = default;
  FileDbInterface(const FileDbInterface &) = delete;
  FileDbInterface &operator=(const FileDbInterface &) = delete;
  virtual ~FileDbInterface() = default;

  // non thread safe
  virtual Id create_pmc_id() = 0;

  // thread safe
  virtual void close(Promise<> promise) = 0;
  template <class LocationT>
  void get_file_data(const LocationT &location, Promise<FileData> promise) {
    get_file_data(as_key(location), std::move(promise));
  }

  template <class LocationT>
  Result<FileData> get_file_data_sync(const LocationT &location) {
    auto res = get_file_data_sync_impl(as_key(location));
    if (res.is_ok()) {
      LOG(DEBUG) << "GET " << location << " " << res.ok();
    } else {
      LOG(DEBUG) << "GET " << location << " " << res.error();
    }
    return res;
  }

  virtual void clear_file_data(Id id, const FileData &file_data) = 0;
  virtual void set_file_data(Id id, const FileData &file_data, bool new_remote, bool new_local, bool new_generate) = 0;
  virtual void set_file_data_ref(Id id, Id new_id) = 0;

  // For FileStatsWorker. TODO: remove it
  virtual SqliteKeyValue &pmc() = 0;

 private:
  virtual void get_file_data_impl(string key, Promise<FileData> promise) = 0;
  virtual Result<FileData> get_file_data_sync_impl(string key) = 0;
};
;
}  // namespace td
