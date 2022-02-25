//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/ServerMessageId.h"

#include "td/actor/PromiseFuture.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <memory>
#include <utility>

namespace td {

class SqliteConnectionSafe;
class SqliteDb;

struct DownloadsDbFtsQuery {
  string query;
  int64 offset{0};
  int32 limit{0};
};

struct DownloadsDbDownloadShort {
  string unique_file_id;
  string file_source;
  int32 priority;
};
struct DownloadsDbDownload {
  string unique_file_id;
  string file_source;
  string search_text;
  int32 date;
  int32 priority;
};

struct GetActiveDownloadsResult {
  std::vector<DownloadsDbDownloadShort> downloads;
};

struct DownloadsDbFtsResult {
  vector<DownloadsDbDownloadShort> downloads;
  int64 next_download_id{};
};

class DownloadsDbSyncInterface {
 public:
  DownloadsDbSyncInterface() = default;
  DownloadsDbSyncInterface(const DownloadsDbSyncInterface &) = delete;
  DownloadsDbSyncInterface &operator=(const DownloadsDbSyncInterface &) = delete;
  virtual ~DownloadsDbSyncInterface() = default;

  virtual Status add_download(DownloadsDbDownload) = 0;
  virtual Result<GetActiveDownloadsResult> get_active_downloads() = 0;
  virtual Result<DownloadsDbFtsResult> get_downloads_fts(DownloadsDbFtsQuery query) = 0;

  virtual Status begin_write_transaction() = 0;
  virtual Status commit_transaction() = 0;
};

class DownloadsDbSyncSafeInterface {
 public:
  DownloadsDbSyncSafeInterface() = default;
  DownloadsDbSyncSafeInterface(const DownloadsDbSyncSafeInterface &) = delete;
  DownloadsDbSyncSafeInterface &operator=(const DownloadsDbSyncSafeInterface &) = delete;
  virtual ~DownloadsDbSyncSafeInterface() = default;

  virtual DownloadsDbSyncInterface &get() = 0;
};

class DownloadsDbAsyncInterface {
 public:
  DownloadsDbAsyncInterface() = default;
  DownloadsDbAsyncInterface(const DownloadsDbAsyncInterface &) = delete;
  DownloadsDbAsyncInterface &operator=(const DownloadsDbAsyncInterface &) = delete;
  virtual ~DownloadsDbAsyncInterface() = default;

  virtual void add_download(DownloadsDbDownload, Promise<>) = 0;
  virtual void get_active_downloads(Promise<GetActiveDownloadsResult>) = 0;
  virtual void get_downloads_fts(DownloadsDbFtsQuery query, Promise<DownloadsDbFtsResult>) = 0;

  virtual void close(Promise<> promise) = 0;
  virtual void force_flush() = 0;
};

Status init_downloads_db(SqliteDb &db, int version) TD_WARN_UNUSED_RESULT;
Status drop_downloads_db(SqliteDb &db, int version) TD_WARN_UNUSED_RESULT;

std::shared_ptr<DownloadsDbSyncSafeInterface> create_downloads_db_sync(
    std::shared_ptr<SqliteConnectionSafe> sqlite_connection);

std::shared_ptr<DownloadsDbAsyncInterface> create_downloads_db_async(
    std::shared_ptr<DownloadsDbSyncSafeInterface> sync_db, int32 scheduler_id);

}  // namespace td
