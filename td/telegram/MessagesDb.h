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

struct MessagesDbMessagesQuery {
  DialogId dialog_id;
  MessageSearchFilter filter{MessageSearchFilter::Empty};
  MessageId from_message_id;
  int32 offset{0};
  int32 limit{100};
};

struct MessagesDbDialogMessage {
  MessageId message_id;
  BufferSlice data;
};

struct MessagesDbMessage {
  DialogId dialog_id;
  MessageId message_id;
  BufferSlice data;
};

struct MessagesDbDialogCalendarQuery {
  DialogId dialog_id;
  MessageSearchFilter filter{MessageSearchFilter::Empty};
  MessageId from_message_id;
  int32 tz_offset{0};
};

struct MessagesDbCalendar {
  vector<MessagesDbDialogMessage> messages;
  vector<int32> total_counts;
};

struct MessagesDbGetDialogSparseMessagePositionsQuery {
  DialogId dialog_id;
  MessageSearchFilter filter{MessageSearchFilter::Empty};
  MessageId from_message_id;
  int32 limit{0};
};

struct MessagesDbMessagePosition {
  int32 position;
  int32 date;
  MessageId message_id;
};

struct MessagesDbMessagePositions {
  int32 total_count{0};
  vector<MessagesDbMessagePosition> positions;
};

struct MessagesDbFtsQuery {
  string query;
  DialogId dialog_id;
  MessageSearchFilter filter{MessageSearchFilter::Empty};
  int64 from_search_id{0};
  int32 limit{100};
};
struct MessagesDbFtsResult {
  vector<MessagesDbMessage> messages;
  int64 next_search_id{1};
};

struct MessagesDbCallsQuery {
  MessageSearchFilter filter{MessageSearchFilter::Empty};
  int32 from_unique_message_id{0};
  int32 limit{100};
};

struct MessagesDbCallsResult {
  vector<MessagesDbMessage> messages;
};

class MessagesDbSyncInterface {
 public:
  MessagesDbSyncInterface() = default;
  MessagesDbSyncInterface(const MessagesDbSyncInterface &) = delete;
  MessagesDbSyncInterface &operator=(const MessagesDbSyncInterface &) = delete;
  virtual ~MessagesDbSyncInterface() = default;

  virtual Status add_message(FullMessageId full_message_id, ServerMessageId unique_message_id,
                             DialogId sender_dialog_id, int64 random_id, int32 ttl_expires_at, int32 index_mask,
                             int64 search_id, string text, NotificationId notification_id,
                             MessageId top_thread_message_id, BufferSlice data) = 0;
  virtual Status add_scheduled_message(FullMessageId full_message_id, BufferSlice data) = 0;

  virtual Status delete_message(FullMessageId full_message_id) = 0;
  virtual Status delete_all_dialog_messages(DialogId dialog_id, MessageId from_message_id) = 0;
  virtual Status delete_dialog_messages_by_sender(DialogId dialog_id, DialogId sender_dialog_id) = 0;

  virtual Result<MessagesDbDialogMessage> get_message(FullMessageId full_message_id) = 0;
  virtual Result<MessagesDbMessage> get_message_by_unique_message_id(ServerMessageId unique_message_id) = 0;
  virtual Result<MessagesDbDialogMessage> get_message_by_random_id(DialogId dialog_id, int64 random_id) = 0;
  virtual Result<MessagesDbDialogMessage> get_dialog_message_by_date(DialogId dialog_id, MessageId first_message_id,
                                                                     MessageId last_message_id, int32 date) = 0;

  virtual Result<MessagesDbCalendar> get_dialog_message_calendar(MessagesDbDialogCalendarQuery query) = 0;

  virtual Result<MessagesDbMessagePositions> get_dialog_sparse_message_positions(
      MessagesDbGetDialogSparseMessagePositionsQuery query) = 0;

  virtual Result<vector<MessagesDbDialogMessage>> get_messages(MessagesDbMessagesQuery query) = 0;
  virtual Result<vector<MessagesDbDialogMessage>> get_scheduled_messages(DialogId dialog_id, int32 limit) = 0;
  virtual Result<vector<MessagesDbDialogMessage>> get_messages_from_notification_id(DialogId dialog_id,
                                                                                    NotificationId from_notification_id,
                                                                                    int32 limit) = 0;

  virtual Result<std::pair<vector<MessagesDbMessage>, int32>> get_expiring_messages(int32 expires_from,
                                                                                    int32 expires_till,
                                                                                    int32 limit) = 0;
  virtual Result<MessagesDbCallsResult> get_calls(MessagesDbCallsQuery query) = 0;
  virtual Result<MessagesDbFtsResult> get_messages_fts(MessagesDbFtsQuery query) = 0;

  virtual Status begin_write_transaction() = 0;
  virtual Status commit_transaction() = 0;
};

class MessagesDbSyncSafeInterface {
 public:
  MessagesDbSyncSafeInterface() = default;
  MessagesDbSyncSafeInterface(const MessagesDbSyncSafeInterface &) = delete;
  MessagesDbSyncSafeInterface &operator=(const MessagesDbSyncSafeInterface &) = delete;
  virtual ~MessagesDbSyncSafeInterface() = default;

  virtual MessagesDbSyncInterface &get() = 0;
};

class MessagesDbAsyncInterface {
 public:
  MessagesDbAsyncInterface() = default;
  MessagesDbAsyncInterface(const MessagesDbAsyncInterface &) = delete;
  MessagesDbAsyncInterface &operator=(const MessagesDbAsyncInterface &) = delete;
  virtual ~MessagesDbAsyncInterface() = default;

  virtual void add_message(FullMessageId full_message_id, ServerMessageId unique_message_id, DialogId sender_dialog_id,
                           int64 random_id, int32 ttl_expires_at, int32 index_mask, int64 search_id, string text,
                           NotificationId notification_id, MessageId top_thread_message_id, BufferSlice data,
                           Promise<> promise) = 0;
  virtual void add_scheduled_message(FullMessageId full_message_id, BufferSlice data, Promise<> promise) = 0;

  virtual void delete_message(FullMessageId full_message_id, Promise<> promise) = 0;
  virtual void delete_all_dialog_messages(DialogId dialog_id, MessageId from_message_id, Promise<> promise) = 0;
  virtual void delete_dialog_messages_by_sender(DialogId dialog_id, DialogId sender_dialog_id, Promise<> promise) = 0;

  virtual void get_message(FullMessageId full_message_id, Promise<MessagesDbDialogMessage> promise) = 0;
  virtual void get_message_by_unique_message_id(ServerMessageId unique_message_id,
                                                Promise<MessagesDbMessage> promise) = 0;
  virtual void get_message_by_random_id(DialogId dialog_id, int64 random_id,
                                        Promise<MessagesDbDialogMessage> promise) = 0;
  virtual void get_dialog_message_by_date(DialogId dialog_id, MessageId first_message_id, MessageId last_message_id,
                                          int32 date, Promise<MessagesDbDialogMessage> promise) = 0;

  virtual void get_dialog_message_calendar(MessagesDbDialogCalendarQuery query,
                                           Promise<MessagesDbCalendar> promise) = 0;

  virtual void get_dialog_sparse_message_positions(MessagesDbGetDialogSparseMessagePositionsQuery query,
                                                   Promise<MessagesDbMessagePositions> promise) = 0;

  virtual void get_messages(MessagesDbMessagesQuery query, Promise<vector<MessagesDbDialogMessage>> promise) = 0;
  virtual void get_scheduled_messages(DialogId dialog_id, int32 limit,
                                      Promise<vector<MessagesDbDialogMessage>> promise) = 0;
  virtual void get_messages_from_notification_id(DialogId dialog_id, NotificationId from_notification_id, int32 limit,
                                                 Promise<vector<MessagesDbDialogMessage>> promise) = 0;

  virtual void get_calls(MessagesDbCallsQuery, Promise<MessagesDbCallsResult> promise) = 0;
  virtual void get_messages_fts(MessagesDbFtsQuery query, Promise<MessagesDbFtsResult> promise) = 0;

  virtual void get_expiring_messages(int32 expires_from, int32 expires_till, int32 limit,
                                     Promise<std::pair<vector<MessagesDbMessage>, int32>> promise) = 0;

  virtual void close(Promise<> promise) = 0;
  virtual void force_flush() = 0;
};

Status init_messages_db(SqliteDb &db, int version) TD_WARN_UNUSED_RESULT;
Status drop_messages_db(SqliteDb &db, int version) TD_WARN_UNUSED_RESULT;

std::shared_ptr<MessagesDbSyncSafeInterface> create_messages_db_sync(
    std::shared_ptr<SqliteConnectionSafe> sqlite_connection);

std::shared_ptr<MessagesDbAsyncInterface> create_messages_db_async(std::shared_ptr<MessagesDbSyncSafeInterface> sync_db,
                                                                   int32 scheduler_id);

}  // namespace td
