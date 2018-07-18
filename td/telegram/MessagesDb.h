//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"

#include "td/actor/PromiseFuture.h"

#include "td/db/SqliteConnectionSafe.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <memory>
#include <utility>

namespace td {
// append only before Size
enum class SearchMessagesFilter : int32 {
  Empty,
  Animation,
  Audio,
  Document,
  Photo,
  Video,
  VoiceNote,
  PhotoAndVideo,
  Url,
  ChatPhoto,
  Call,
  MissedCall,
  VideoNote,
  VoiceAndVideoNote,
  Mention,
  UnreadMention,
  Size
};

struct MessagesDbMessagesQuery {
  DialogId dialog_id;
  int32 index_mask{0};
  MessageId from_message_id;
  int32 offset{0};
  int32 limit{100};
};

struct MessagesDbMessagesResult {
  std::vector<BufferSlice> messages;
};

struct MessagesDbMessage {
  DialogId dialog_id;
  BufferSlice data;
};

struct MessagesDbFtsQuery {
  string query;
  DialogId dialog_id;
  int32 index_mask{0};
  int64 from_search_id{0};
  int32 limit{100};
};
struct MessagesDbFtsResult {
  std::vector<MessagesDbMessage> messages;
  int64 next_search_id{1};
};

struct MessagesDbCallsQuery {
  int32 index_mask{0};
  int32 from_unique_message_id{0};
  int32 limit{100};
};
struct MessagesDbCallsResult {
  std::vector<MessagesDbMessage> messages;
};

class MessagesDbSyncInterface {
 public:
  MessagesDbSyncInterface() = default;
  MessagesDbSyncInterface(const MessagesDbSyncInterface &) = delete;
  MessagesDbSyncInterface &operator=(const MessagesDbSyncInterface &) = delete;
  virtual ~MessagesDbSyncInterface() = default;

  virtual Status add_message(FullMessageId full_message_id, ServerMessageId unique_message_id, UserId sender_user_id,
                             int64 random_id, int32 ttl_expires_at, int32 index_mask, int64 search_id, string text,
                             BufferSlice data) = 0;

  virtual Status delete_message(FullMessageId full_message_id) = 0;
  virtual Status delete_all_dialog_messages(DialogId dialog_id, MessageId from_message_id) = 0;
  virtual Status delete_dialog_messages_from_user(DialogId dialog_id, UserId sender_user_id) = 0;

  virtual Result<BufferSlice> get_message(FullMessageId full_message_id) = 0;
  virtual Result<std::pair<DialogId, BufferSlice>> get_message_by_unique_message_id(
      ServerMessageId unique_message_id) = 0;
  virtual Result<BufferSlice> get_message_by_random_id(DialogId dialog_id, int64 random_id) = 0;
  virtual Result<BufferSlice> get_dialog_message_by_date(DialogId dialog_id, MessageId first_message_id,
                                                         MessageId last_message_id, int32 date) = 0;

  virtual Result<MessagesDbMessagesResult> get_messages(MessagesDbMessagesQuery query) = 0;

  virtual Result<std::pair<std::vector<std::pair<DialogId, BufferSlice>>, int32>> get_expiring_messages(
      int32 expire_from, int32 expire_till, int32 limit) = 0;
  virtual Result<MessagesDbCallsResult> get_calls(MessagesDbCallsQuery query) = 0;
  virtual Result<MessagesDbFtsResult> get_messages_fts(MessagesDbFtsQuery query) = 0;

  virtual Status begin_transaction() = 0;
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

  virtual void add_message(FullMessageId full_message_id, ServerMessageId unique_message_id, UserId sender_user_id,
                           int64 random_id, int32 ttl_expires_at, int32 index_mask, int64 search_id, string text,
                           BufferSlice data, Promise<> promise) = 0;

  virtual void delete_message(FullMessageId full_message_id, Promise<> promise) = 0;
  virtual void delete_all_dialog_messages(DialogId dialog_id, MessageId from_message_id, Promise<> promise) = 0;
  virtual void delete_dialog_messages_from_user(DialogId dialog_id, UserId sender_user_id, Promise<> promise) = 0;

  virtual void get_message(FullMessageId full_message_id, Promise<BufferSlice> promise) = 0;
  virtual void get_message_by_unique_message_id(ServerMessageId unique_message_id,
                                                Promise<std::pair<DialogId, BufferSlice>> promise) = 0;
  virtual void get_message_by_random_id(DialogId dialog_id, int64 random_id, Promise<BufferSlice> promise) = 0;
  virtual void get_dialog_message_by_date(DialogId dialog_id, MessageId first_message_id, MessageId last_message_id,
                                          int32 date, Promise<BufferSlice> promise) = 0;

  virtual void get_messages(MessagesDbMessagesQuery query, Promise<MessagesDbMessagesResult>) = 0;

  virtual void get_calls(MessagesDbCallsQuery, Promise<MessagesDbCallsResult>) = 0;
  virtual void get_messages_fts(MessagesDbFtsQuery query, Promise<MessagesDbFtsResult> promise) = 0;

  virtual void get_expiring_messages(
      int32 expire_from, int32 expire_till, int32 limit,
      Promise<std::pair<std::vector<std::pair<DialogId, BufferSlice>>, int32>> promise) = 0;

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
