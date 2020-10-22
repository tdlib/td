//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/TdDb.h"

#include "td/telegram/DialogDb.h"
#include "td/telegram/files/FileDb.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesDb.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdParameters.h"
#include "td/telegram/Version.h"

#include "td/actor/MultiPromise.h"

#include "td/db/binlog/Binlog.h"
#include "td/db/binlog/ConcurrentBinlog.h"
#include "td/db/BinlogKeyValue.h"
#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"
#include "td/db/SqliteKeyValueSafe.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/path.h"
#include "td/utils/Random.h"
#include "td/utils/StringBuilder.h"

#include <algorithm>

namespace td {

namespace {

std::string get_binlog_path(const TdParameters &parameters) {
  return PSTRING() << parameters.database_directory << "td" << (parameters.use_test_dc ? "_test" : "") << ".binlog";
}

std::string get_sqlite_path(const TdParameters &parameters) {
  const string db_name = "db" + (parameters.use_test_dc ? string("_test") : string());
  return parameters.database_directory + db_name + ".sqlite";
}

Result<TdDb::EncryptionInfo> check_encryption(string path) {
  Binlog binlog;
  auto status = binlog.init(path, Binlog::Callback());
  if (status.is_error() && status.code() != Binlog::Error::WrongPassword) {
    return Status::Error(400, status.message());
  }
  TdDb::EncryptionInfo info;
  info.is_encrypted = binlog.get_info().wrong_password;
  binlog.close(false /*need_sync*/).ensure();
  return info;
}

Status init_binlog(Binlog &binlog, string path, BinlogKeyValue<Binlog> &binlog_pmc, BinlogKeyValue<Binlog> &config_pmc,
                   TdDb::Events &events, DbKey key) {
  auto callback = [&](const BinlogEvent &event) {
    switch (event.type_) {
      case LogEvent::HandlerType::SecretChats:
        events.to_secret_chats_manager.push_back(event.clone());
        break;
      case LogEvent::HandlerType::Users:
        events.user_events.push_back(event.clone());
        break;
      case LogEvent::HandlerType::Chats:
        events.chat_events.push_back(event.clone());
        break;
      case LogEvent::HandlerType::Channels:
        events.channel_events.push_back(event.clone());
        break;
      case LogEvent::HandlerType::SecretChatInfos:
        events.secret_chat_events.push_back(event.clone());
        break;
      case LogEvent::HandlerType::WebPages:
        events.web_page_events.push_back(event.clone());
        break;
      case LogEvent::HandlerType::SetPollAnswer:
      case LogEvent::HandlerType::StopPoll:
        events.to_poll_manager.push_back(event.clone());
        break;
      case LogEvent::HandlerType::SendMessage:
      case LogEvent::HandlerType::DeleteMessage:
      case LogEvent::HandlerType::DeleteMessagesFromServer:
      case LogEvent::HandlerType::ReadHistoryOnServer:
      case LogEvent::HandlerType::ReadMessageContentsOnServer:
      case LogEvent::HandlerType::ForwardMessages:
      case LogEvent::HandlerType::SendBotStartMessage:
      case LogEvent::HandlerType::SendScreenshotTakenNotificationMessage:
      case LogEvent::HandlerType::SendInlineQueryResultMessage:
      case LogEvent::HandlerType::DeleteDialogHistoryFromServer:
      case LogEvent::HandlerType::ReadAllDialogMentionsOnServer:
      case LogEvent::HandlerType::DeleteAllChannelMessagesFromUserOnServer:
      case LogEvent::HandlerType::ToggleDialogIsPinnedOnServer:
      case LogEvent::HandlerType::ReorderPinnedDialogsOnServer:
      case LogEvent::HandlerType::SaveDialogDraftMessageOnServer:
      case LogEvent::HandlerType::UpdateDialogNotificationSettingsOnServer:
      case LogEvent::HandlerType::UpdateScopeNotificationSettingsOnServer:
      case LogEvent::HandlerType::ResetAllNotificationSettingsOnServer:
      case LogEvent::HandlerType::ChangeDialogReportSpamStateOnServer:
      case LogEvent::HandlerType::GetDialogFromServer:
      case LogEvent::HandlerType::GetChannelDifference:
      case LogEvent::HandlerType::ReadHistoryInSecretChat:
      case LogEvent::HandlerType::ToggleDialogIsMarkedAsUnreadOnServer:
      case LogEvent::HandlerType::SetDialogFolderIdOnServer:
      case LogEvent::HandlerType::DeleteScheduledMessagesFromServer:
      case LogEvent::HandlerType::ToggleDialogIsBlockedOnServer:
      case LogEvent::HandlerType::ReadMessageThreadHistoryOnServer:
      case LogEvent::HandlerType::BlockMessageSenderFromRepliesOnServer:
      case LogEvent::HandlerType::UnpinAllDialogMessagesOnServer:
        events.to_messages_manager.push_back(event.clone());
        break;
      case LogEvent::HandlerType::AddMessagePushNotification:
      case LogEvent::HandlerType::EditMessagePushNotification:
        events.to_notification_manager.push_back(event.clone());
        break;
      case LogEvent::HandlerType::BinlogPmcMagic:
        binlog_pmc.external_init_handle(event);
        break;
      case LogEvent::HandlerType::ConfigPmcMagic:
        config_pmc.external_init_handle(event);
        break;
      default:
        LOG(FATAL) << "Unsupported log event type " << event.type_;
    }
  };

  auto binlog_info = binlog.init(std::move(path), callback, std::move(key));
  if (binlog_info.is_error()) {
    return binlog_info.move_as_error();
  }
  return Status::OK();
}

Status init_db(SqliteDb &db) {
  TRY_STATUS(db.exec("PRAGMA encoding=\"UTF-8\""));
  TRY_STATUS(db.exec("PRAGMA journal_mode=WAL"));

  TRY_STATUS(db.exec("PRAGMA synchronous=NORMAL"));
  TRY_STATUS(db.exec("PRAGMA temp_store=MEMORY"));
  TRY_STATUS(db.exec("PRAGMA secure_delete=1"));

  return Status::OK();
}

}  // namespace

std::shared_ptr<FileDbInterface> TdDb::get_file_db_shared() {
  return file_db_;
}

std::shared_ptr<SqliteConnectionSafe> &TdDb::get_sqlite_connection_safe() {
  return sql_connection_;
}

BinlogInterface *TdDb::get_binlog_impl(const char *file, int line) {
  LOG_CHECK(binlog_) << G()->close_flag() << " " << file << " " << line;
  return binlog_.get();
}

std::shared_ptr<KeyValueSyncInterface> TdDb::get_binlog_pmc_shared() {
  CHECK(binlog_pmc_);
  return binlog_pmc_;
}

std::shared_ptr<KeyValueSyncInterface> TdDb::get_config_pmc_shared() {
  CHECK(config_pmc_);
  return config_pmc_;
}

KeyValueSyncInterface *TdDb::get_binlog_pmc() {
  CHECK(binlog_pmc_);
  return binlog_pmc_.get();
}

KeyValueSyncInterface *TdDb::get_config_pmc() {
  CHECK(config_pmc_);
  return config_pmc_.get();
}

SqliteKeyValue *TdDb::get_sqlite_sync_pmc() {
  CHECK(common_kv_safe_);
  return &common_kv_safe_->get();
}

SqliteKeyValueAsyncInterface *TdDb::get_sqlite_pmc() {
  CHECK(common_kv_async_);
  return common_kv_async_.get();
}

MessagesDbSyncInterface *TdDb::get_messages_db_sync() {
  return &messages_db_sync_safe_->get();
}
MessagesDbAsyncInterface *TdDb::get_messages_db_async() {
  return messages_db_async_.get();
}
DialogDbSyncInterface *TdDb::get_dialog_db_sync() {
  return &dialog_db_sync_safe_->get();
}
DialogDbAsyncInterface *TdDb::get_dialog_db_async() {
  return dialog_db_async_.get();
}

CSlice TdDb::binlog_path() const {
  return binlog_->get_path();
}
CSlice TdDb::sqlite_path() const {
  return sqlite_path_;
}

void TdDb::flush_all() {
  LOG(INFO) << "Flush all databases";
  if (messages_db_async_) {
    messages_db_async_->force_flush();
  }
  binlog_->force_flush();
}

void TdDb::close_all(Promise<> on_finished) {
  LOG(INFO) << "Close all databases";
  do_close(std::move(on_finished), false /*destroy_flag*/);
}

void TdDb::close_and_destroy_all(Promise<> on_finished) {
  LOG(INFO) << "Destroy all databases";
  do_close(std::move(on_finished), true /*destroy_flag*/);
}

void TdDb::do_close(Promise<> on_finished, bool destroy_flag) {
  MultiPromiseActorSafe mpas{"TdDbCloseMultiPromiseActor"};
  mpas.add_promise(PromiseCreator::lambda(
      [promise = std::move(on_finished), sql_connection = std::move(sql_connection_), destroy_flag](Unit) mutable {
        if (sql_connection) {
          LOG_CHECK(sql_connection.unique()) << sql_connection.use_count();
          if (destroy_flag) {
            sql_connection->close_and_destroy();
          } else {
            sql_connection->close();
          }
          sql_connection.reset();
        }
        promise.set_value(Unit());
      }));
  auto lock = mpas.get_promise();

  if (file_db_) {
    file_db_->close(mpas.get_promise());
    file_db_.reset();
  }

  common_kv_safe_.reset();
  if (common_kv_async_) {
    common_kv_async_->close(mpas.get_promise());
  }

  messages_db_sync_safe_.reset();
  if (messages_db_async_) {
    messages_db_async_->close(mpas.get_promise());
  }

  dialog_db_sync_safe_.reset();
  if (dialog_db_async_) {
    dialog_db_async_->close(mpas.get_promise());
  }

  // binlog_pmc is dependent on binlog_ and anyway it doesn't support close_and_destroy
  CHECK(binlog_pmc_.unique());
  binlog_pmc_.reset();
  CHECK(config_pmc_.unique());
  config_pmc_.reset();

  if (binlog_) {
    if (destroy_flag) {
      binlog_->close_and_destroy(mpas.get_promise());
    } else {
      binlog_->close(mpas.get_promise());
    }
    binlog_.reset();
  }
}

Status TdDb::init_sqlite(int32 scheduler_id, const TdParameters &parameters, DbKey key, DbKey old_key,
                         BinlogKeyValue<Binlog> &binlog_pmc) {
  CHECK(!parameters.use_message_db || parameters.use_chat_info_db);
  CHECK(!parameters.use_chat_info_db || parameters.use_file_db);

  const string sql_database_path = get_sqlite_path(parameters);

  bool use_sqlite = parameters.use_file_db;
  bool use_file_db = parameters.use_file_db;
  bool use_dialog_db = parameters.use_message_db;
  bool use_message_db = parameters.use_message_db;
  if (!use_sqlite) {
    unlink(sql_database_path).ignore();
    return Status::OK();
  }

  sqlite_path_ = sql_database_path;
  TRY_RESULT(db_instance, SqliteDb::change_key(sqlite_path_, key, old_key));
  sql_connection_ = std::make_shared<SqliteConnectionSafe>(sql_database_path, key, db_instance.get_cipher_version());
  sql_connection_->set(std::move(db_instance));
  auto &db = sql_connection_->get();

  TRY_STATUS(init_db(db));

  // Init databases
  // Do initialization once and before everything else to avoid "database is locked" error.
  // Must be in a transaction

  // NB: when database is dropped we should also drop corresponding binlog events
  TRY_STATUS(db.exec("BEGIN TRANSACTION"));

  // Get 'PRAGMA user_version'
  TRY_RESULT(user_version, db.user_version());
  LOG(WARNING) << "Got PRAGMA user_version = " << user_version;

  // init DialogDb
  bool dialog_db_was_created = false;
  if (use_dialog_db) {
    TRY_STATUS(init_dialog_db(db, user_version, binlog_pmc, dialog_db_was_created));
  } else {
    TRY_STATUS(drop_dialog_db(db, user_version));
  }

  // init MessagesDb
  if (use_message_db) {
    TRY_STATUS(init_messages_db(db, user_version));
  } else {
    TRY_STATUS(drop_messages_db(db, user_version));
  }

  // init filesDb
  if (use_file_db) {
    TRY_STATUS(init_file_db(db, user_version));
  } else {
    TRY_STATUS(drop_file_db(db, user_version));
  }

  // Update 'PRAGMA user_version'
  auto db_version = current_db_version();
  if (db_version != user_version) {
    LOG(WARNING) << "Set PRAGMA user_version = " << db_version;
    TRY_STATUS(db.set_user_version(db_version));
  }

  if (dialog_db_was_created) {
    binlog_pmc.erase_by_prefix("pinned_dialog_ids");
    binlog_pmc.erase_by_prefix("last_server_dialog_date");
    binlog_pmc.erase_by_prefix("unread_message_count");
    binlog_pmc.erase_by_prefix("unread_dialog_count");
    binlog_pmc.erase("sponsored_dialog_id");
    binlog_pmc.erase_by_prefix("top_dialogs");
  }
  if (user_version == 0) {
    binlog_pmc.erase("next_contacts_sync_date");
  }
  binlog_pmc.force_sync({});

  TRY_STATUS(db.exec("COMMIT TRANSACTION"));

  file_db_ = create_file_db(sql_connection_, scheduler_id);

  common_kv_safe_ = std::make_shared<SqliteKeyValueSafe>("common", sql_connection_);
  common_kv_async_ = create_sqlite_key_value_async(common_kv_safe_, scheduler_id);

  if (use_dialog_db) {
    dialog_db_sync_safe_ = create_dialog_db_sync(sql_connection_);
    dialog_db_async_ = create_dialog_db_async(dialog_db_sync_safe_, scheduler_id);
  }

  if (use_message_db) {
    messages_db_sync_safe_ = create_messages_db_sync(sql_connection_);
    messages_db_async_ = create_messages_db_async(messages_db_sync_safe_, scheduler_id);
  }

  return Status::OK();
}

Status TdDb::init(int32 scheduler_id, const TdParameters &parameters, DbKey key, Events &events) {
  // Init pmc
  Binlog *binlog_ptr = nullptr;
  auto binlog = std::shared_ptr<Binlog>(new Binlog, [&](Binlog *ptr) { binlog_ptr = ptr; });

  auto binlog_pmc = make_unique<BinlogKeyValue<Binlog>>();
  auto config_pmc = make_unique<BinlogKeyValue<Binlog>>();
  binlog_pmc->external_init_begin(static_cast<int32>(LogEvent::HandlerType::BinlogPmcMagic));
  config_pmc->external_init_begin(static_cast<int32>(LogEvent::HandlerType::ConfigPmcMagic));

  bool encrypt_binlog = !key.is_empty();
  VLOG(td_init) << "Start binlog loading";
  TRY_STATUS(init_binlog(*binlog, get_binlog_path(parameters), *binlog_pmc, *config_pmc, events, std::move(key)));
  VLOG(td_init) << "Finish binlog loading";

  binlog_pmc->external_init_finish(binlog);
  VLOG(td_init) << "Finish initialization of binlog PMC";
  config_pmc->external_init_finish(binlog);
  VLOG(td_init) << "Finish initialization of config PMC";

  DbKey new_sqlite_key;
  DbKey old_sqlite_key;
  bool encrypt_sqlite = encrypt_binlog;
  bool drop_sqlite_key = false;
  auto sqlite_key = binlog_pmc->get("sqlite_key");
  if (encrypt_sqlite) {
    if (sqlite_key.empty()) {
      sqlite_key = string(32, ' ');
      Random::secure_bytes(sqlite_key);
      binlog_pmc->set("sqlite_key", sqlite_key);
      binlog_pmc->force_sync(Auto());
    }
    new_sqlite_key = DbKey::raw_key(std::move(sqlite_key));
  } else {
    if (!sqlite_key.empty()) {
      old_sqlite_key = DbKey::raw_key(std::move(sqlite_key));
      drop_sqlite_key = true;
    }
  }
  VLOG(td_init) << "Start to init database";
  auto init_sqlite_status = init_sqlite(scheduler_id, parameters, new_sqlite_key, old_sqlite_key, *binlog_pmc);
  VLOG(td_init) << "Finish to init database";
  if (init_sqlite_status.is_error()) {
    LOG(ERROR) << "Destroy bad SQLite database because of " << init_sqlite_status;
    if (sql_connection_ != nullptr) {
      sql_connection_->get().close();
    }
    SqliteDb::destroy(get_sqlite_path(parameters)).ignore();
    TRY_STATUS(init_sqlite(scheduler_id, parameters, new_sqlite_key, old_sqlite_key, *binlog_pmc));
  }
  if (drop_sqlite_key) {
    binlog_pmc->erase("sqlite_key");
    binlog_pmc->force_sync(Auto());
  }

  VLOG(td_init) << "Create concurrent_binlog_pmc";
  auto concurrent_binlog_pmc = std::make_shared<BinlogKeyValue<ConcurrentBinlog>>();
  concurrent_binlog_pmc->external_init_begin(binlog_pmc->get_magic());
  concurrent_binlog_pmc->external_init_handle(std::move(*binlog_pmc));

  VLOG(td_init) << "Create concurrent_config_pmc";
  auto concurrent_config_pmc = std::make_shared<BinlogKeyValue<ConcurrentBinlog>>();
  concurrent_config_pmc->external_init_begin(config_pmc->get_magic());
  concurrent_config_pmc->external_init_handle(std::move(*config_pmc));

  binlog.reset();
  binlog_pmc.reset();
  config_pmc.reset();

  CHECK(binlog_ptr != nullptr);
  VLOG(td_init) << "Create concurrent_binlog";
  auto concurrent_binlog = std::make_shared<ConcurrentBinlog>(unique_ptr<Binlog>(binlog_ptr), scheduler_id);

  VLOG(td_init) << "Init concurrent_binlog_pmc";
  concurrent_binlog_pmc->external_init_finish(concurrent_binlog);
  VLOG(td_init) << "Init concurrent_config_pmc";
  concurrent_config_pmc->external_init_finish(concurrent_binlog);

  binlog_pmc_ = std::move(concurrent_binlog_pmc);
  config_pmc_ = std::move(concurrent_config_pmc);
  binlog_ = std::move(concurrent_binlog);

  return Status::OK();
}

TdDb::TdDb() = default;
TdDb::~TdDb() = default;

Result<unique_ptr<TdDb>> TdDb::open(int32 scheduler_id, const TdParameters &parameters, DbKey key, Events &events) {
  auto db = make_unique<TdDb>();
  TRY_STATUS(db->init(scheduler_id, parameters, std::move(key), events));
  return std::move(db);
}

Result<TdDb::EncryptionInfo> TdDb::check_encryption(const TdParameters &parameters) {
  return ::td::check_encryption(get_binlog_path(parameters));
}

void TdDb::change_key(DbKey key, Promise<> promise) {
  get_binlog()->change_key(std::move(key), std::move(promise));
}

Status TdDb::destroy(const TdParameters &parameters) {
  SqliteDb::destroy(get_sqlite_path(parameters)).ignore();
  Binlog::destroy(get_binlog_path(parameters)).ignore();
  return Status::OK();
}

void TdDb::with_db_path(std::function<void(CSlice)> callback) {
  SqliteDb::with_db_path(sqlite_path(), callback);
  callback(binlog_path());
}

Result<string> TdDb::get_stats() {
  auto sb = StringBuilder({}, true);
  auto &sql = sql_connection_->get();
  auto run_query = [&](CSlice query, Slice desc) -> Status {
    TRY_RESULT(stmt, sql.get_statement(query));
    TRY_STATUS(stmt.step());
    CHECK(stmt.has_row());
    auto key_size = stmt.view_int64(0);
    auto value_size = stmt.view_int64(1);
    auto count = stmt.view_int64(2);
    sb << query << "\n";
    sb << desc << ":\n";
    sb << format::as_size(key_size + value_size) << "\t";
    sb << format::as_size(key_size) << "\t";
    sb << format::as_size(value_size) << "\t";
    sb << format::as_size((key_size + value_size) / (count ? count : 1)) << "\t";
    sb << "\n";
    return Status::OK();
  };
  auto run_kv_query = [&](Slice mask, Slice table = Slice("common")) {
    return run_query(PSLICE() << "SELECT SUM(length(k)), SUM(length(v)), COUNT(*) FROM " << table << " WHERE k like '"
                              << mask << "'",
                     PSLICE() << table << ":" << mask);
  };
  TRY_STATUS(run_query("SELECT 0, SUM(length(data)), COUNT(*) FROM messages WHERE 1", "messages"));
  TRY_STATUS(run_query("SELECT 0, SUM(length(data)), COUNT(*) FROM dialogs WHERE 1", "dialogs"));
  TRY_STATUS(run_kv_query("%", "common"));
  TRY_STATUS(run_kv_query("%", "files"));
  TRY_STATUS(run_kv_query("wp%"));
  TRY_STATUS(run_kv_query("wpurl%"));
  TRY_STATUS(run_kv_query("wpiv%"));
  TRY_STATUS(run_kv_query("us%"));
  TRY_STATUS(run_kv_query("ch%"));
  TRY_STATUS(run_kv_query("ss%"));
  TRY_STATUS(run_kv_query("gr%"));

  vector<int32> prev(1);
  size_t count = 0;
  int32 max_bad_to = 0;
  size_t bad_count = 0;
  file_db_->pmc().get_by_range("file0", "file:", [&](Slice key, Slice value) {
    if (value.substr(0, 2) != "@@") {
      return true;
    }
    count++;
    auto from = to_integer<int32>(key.substr(4));
    auto to = to_integer<int32>(value.substr(2));
    if (from <= to) {
      LOG(DEBUG) << "Have forward reference from " << from << " to " << to;
      if (to > max_bad_to) {
        max_bad_to = to;
      }
      bad_count++;
      return true;
    }
    if (static_cast<size_t>(from) >= prev.size()) {
      prev.resize(from + 1);
    }
    if (static_cast<size_t>(to) >= prev.size()) {
      prev.resize(to + 1);
    }
    prev[from] = to;
    return true;
  });
  for (size_t i = 1; i < prev.size(); i++) {
    if (!prev[i]) {
      continue;
    }
    prev[i] = prev[prev[i]] + 1;
  }
  sb << "Max file database depth out of " << prev.size() << '/' << count
     << " elements: " << *std::max_element(prev.begin(), prev.end()) << "\n";
  sb << "Have " << bad_count << " forward references with maximum reference to " << max_bad_to;

  return sb.as_cslice().str();
}

}  // namespace td
