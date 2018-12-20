//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/NotificationGroupId.h"
#include "td/telegram/NotificationGroupKey.h"

#include "td/actor/PromiseFuture.h"

#include "td/db/SqliteConnectionSafe.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <memory>
#include <utility>

namespace td {
class DialogDbSyncInterface {
 public:
  DialogDbSyncInterface() = default;
  DialogDbSyncInterface(const DialogDbSyncInterface &) = delete;
  DialogDbSyncInterface &operator=(const DialogDbSyncInterface &) = delete;
  virtual ~DialogDbSyncInterface() = default;

  virtual Status add_dialog_new(DialogId dialog_id, int64 order, BufferSlice data,
                                std::vector<NotificationGroupKey> notification_groups) = 0;

  virtual Result<BufferSlice> get_dialog(DialogId dialog_id) = 0;

  virtual Result<std::vector<BufferSlice>> get_dialogs(int64 order, DialogId dialog_id, int32 limit) = 0;

  virtual Result<std::vector<NotificationGroupKey>> get_notification_groups_by_last_notification_date(
      NotificationGroupKey notification_group_key, int32 limit) = 0;

  virtual Result<NotificationGroupKey> get_notification_group(NotificationGroupId notification_group_id) = 0;

  virtual Status begin_transaction() = 0;
  virtual Status commit_transaction() = 0;

  virtual Status add_dialog(DialogId dialog_id, int64 order, int32 last_notification_date,
                            NotificationGroupId notification_group_id, BufferSlice data) {
    LOG(FATAL) << "method is removed";
    return Status::Error();
  }
  virtual Result<std::vector<BufferSlice>> get_dialogs_by_last_notification_date(int32 last_notification_date,
                                                                                 DialogId dialog_id, int32 limit) {
    LOG(FATAL) << "method is removed";
    return Status::Error();
  }
  virtual Result<BufferSlice> get_dialog_by_notification_group_id(NotificationGroupId notification_group_id) {
    LOG(FATAL) << "method is removed";
    return Status::Error();
  }
};

class DialogDbSyncSafeInterface {
 public:
  DialogDbSyncSafeInterface() = default;
  DialogDbSyncSafeInterface(const DialogDbSyncSafeInterface &) = delete;
  DialogDbSyncSafeInterface &operator=(const DialogDbSyncSafeInterface &) = delete;
  virtual ~DialogDbSyncSafeInterface() = default;

  virtual DialogDbSyncInterface &get() = 0;
};

class DialogDbAsyncInterface {
 public:
  DialogDbAsyncInterface() = default;
  DialogDbAsyncInterface(const DialogDbAsyncInterface &) = delete;
  DialogDbAsyncInterface &operator=(const DialogDbAsyncInterface &) = delete;
  virtual ~DialogDbAsyncInterface() = default;

  virtual void add_dialog_new(DialogId dialog_id, int64 order, BufferSlice data,
                              std::vector<NotificationGroupKey> notification_groups, Promise<> promise) = 0;

  virtual void get_dialog(DialogId dialog_id, Promise<BufferSlice> promise) = 0;

  virtual void get_dialogs(int64 order, DialogId dialog_id, int32 limit, Promise<std::vector<BufferSlice>> promise) = 0;

  virtual void get_notification_groups_by_last_notification_date(
      NotificationGroupKey notification_group_key, int32 limit, Promise<std::vector<NotificationGroupKey>> promise) = 0;

  virtual void get_notification_group(NotificationGroupId notification_group_id,
                                      Promise<NotificationGroupKey> promise) = 0;

  virtual void close(Promise<> promise) = 0;

  virtual void add_dialog(DialogId dialog_id, int64 order, int32 last_notification_date,
                          NotificationGroupId notification_group_id, BufferSlice data, Promise<> promise) {
    LOG(FATAL) << "method removed";
  }
  virtual void get_dialogs_by_last_notification_date(int32 last_notification_date, DialogId dialog_id, int32 limit,
                                                     Promise<std::vector<BufferSlice>> promise) {
    LOG(FATAL) << "method removed";
  }
  virtual void get_dialog_by_notification_group_id(NotificationGroupId notification_group_id,
                                                   Promise<BufferSlice> promise) {
    LOG(FATAL) << "method removed";
  }
};

Status init_dialog_db(SqliteDb &db, int version, bool &was_created) TD_WARN_UNUSED_RESULT;
Status drop_dialog_db(SqliteDb &db, int version) TD_WARN_UNUSED_RESULT;

std::shared_ptr<DialogDbSyncSafeInterface> create_dialog_db_sync(
    std::shared_ptr<SqliteConnectionSafe> sqlite_connection);

std::shared_ptr<DialogDbAsyncInterface> create_dialog_db_async(std::shared_ptr<DialogDbSyncSafeInterface> sync_db,
                                                               int32 scheduler_id);
}  // namespace td
