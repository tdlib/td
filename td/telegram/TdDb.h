//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/PromiseFuture.h"

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/ConcurrentBinlog.h"
#include "td/db/BinlogKeyValue.h"
#include "td/db/DbKey.h"
#include "td/db/Pmc.h"

#include "td/telegram/TdParameters.h"

#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <functional>
#include <memory>

namespace td {

class SqliteConnectionSafe;
class SqliteKeyValueSafe;
class SqliteKeyValueAsyncInterface;
class SqliteKeyValue;
class MessagesDbSyncInterface;
class MessagesDbSyncSafeInterface;
class MessagesDbAsyncInterface;
class DialogDbSyncInterface;
class DialogDbSyncSafeInterface;
class DialogDbAsyncInterface;
class FileDbInterface;

struct EncryptionInfo {
  bool is_encrypted{false};
};

class TdDb {
 public:
  TdDb();
  TdDb(const TdDb &) = delete;
  TdDb &operator=(const TdDb &) = delete;
  TdDb(TdDb &&) = delete;
  TdDb &operator=(TdDb &&) = delete;
  ~TdDb();

  struct Events;
  static Result<std::unique_ptr<TdDb>> open(int32 scheduler_id, const TdParameters &parameters, DbKey key,
                                            Events &events);
  static Result<EncryptionInfo> check_encryption(const TdParameters &parameters);
  static Status destroy(const TdParameters &parameters);

  struct Events {
    vector<BinlogEvent> to_secret_chats_manager;
    vector<BinlogEvent> user_events;
    vector<BinlogEvent> chat_events;
    vector<BinlogEvent> channel_events;
    vector<BinlogEvent> secret_chat_events;
    vector<BinlogEvent> web_page_events;
    vector<BinlogEvent> to_messages_manager;
  };

  std::shared_ptr<FileDbInterface> get_file_db_shared();
  std::shared_ptr<SqliteConnectionSafe> &get_sqlite_connection_safe();
  ConcurrentBinlog *get_binlog();

  BinlogPmc get_binlog_pmc_shared();
  BinlogPmcPtr get_binlog_pmc();
  BinlogPmcPtr get_config_pmc();

  SqliteKeyValue *get_sqlite_sync_pmc();
  SqliteKeyValueAsyncInterface *get_sqlite_pmc();
  CSlice binlog_path() const;
  CSlice sqlite_path() const;
  void flush_all();
  void close_all(Promise<> on_finished);
  void close_and_destroy_all(Promise<> on_finished);

  MessagesDbSyncInterface *get_messages_db_sync();
  MessagesDbAsyncInterface *get_messages_db_async();

  DialogDbSyncInterface *get_dialog_db_sync();
  DialogDbAsyncInterface *get_dialog_db_async();

  void change_key(DbKey key, Promise<> promise);

  void with_db_path(std::function<void(CSlice)> callback);

 private:
  string sqlite_path_;
  std::shared_ptr<SqliteConnectionSafe> sql_connection_;

  std::shared_ptr<FileDbInterface> file_db_;

  std::shared_ptr<SqliteKeyValueSafe> common_kv_safe_;
  std::unique_ptr<SqliteKeyValueAsyncInterface> common_kv_async_;

  std::shared_ptr<MessagesDbSyncSafeInterface> messages_db_sync_safe_;
  std::shared_ptr<MessagesDbAsyncInterface> messages_db_async_;

  std::shared_ptr<DialogDbSyncSafeInterface> dialog_db_sync_safe_;
  std::shared_ptr<DialogDbAsyncInterface> dialog_db_async_;

  std::shared_ptr<BinlogKeyValue<ConcurrentBinlog>> binlog_pmc_;
  std::shared_ptr<BinlogKeyValue<ConcurrentBinlog>> config_pmc_;
  std::shared_ptr<ConcurrentBinlog> binlog_;

  Status init(int32 scheduler_id, const TdParameters &parameters, DbKey key, Events &events);
  Status init_sqlite(int32 scheduler_id, const TdParameters &parameters, DbKey key, DbKey old_key,
                     BinlogKeyValue<Binlog> &binlog_pmc);

  void do_close(Promise<> on_finished, bool destroy_flag);
};
}  // namespace td
