//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogDb.h"

#include "td/telegram/Version.h"

#include "td/actor/actor.h"
#include "td/actor/SchedulerLocalStorage.h"

#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteStatement.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Time.h"

namespace td {
// NB: must happen inside a transaction
Status init_dialog_db(SqliteDb &db, int32 version, bool &was_created) {
  LOG(INFO) << "Init dialog database " << tag("version", version);
  was_created = false;

  // Check if database exists
  TRY_RESULT(has_table, db.has_table("dialogs"));
  if (!has_table) {
    version = 0;
  }

  if (version < static_cast<int32>(DbVersion::DialogDbCreated) || version > current_db_version()) {
    TRY_STATUS(drop_dialog_db(db, version));
    version = 0;
  }
  auto create_notification_group_table = [&]() {
    return db.exec(
        "CREATE TABLE IF NOT EXISTS notification_groups (notification_group_id INT4 PRIMARY KEY, dialog_id "
        "INT8, last_notification_date INT4)");
  };

  auto create_last_notification_date_index = [&]() {
    return db.exec(
        "CREATE INDEX IF NOT EXISTS notification_group_by_last_notification_date ON notification_groups "
        "(last_notification_date, dialog_id, notification_group_id) WHERE last_notification_date IS NOT NULL");
  };

  if (version == 0) {
    LOG(INFO) << "Create new dialog database";
    was_created = true;
    TRY_STATUS(
        db.exec("CREATE TABLE IF NOT EXISTS dialogs (dialog_id INT8 PRIMARY KEY, dialog_order INT8, data BLOB)"));
    TRY_STATUS(db.exec("CREATE INDEX IF NOT EXISTS dialog_by_dialog_order ON dialogs (dialog_order, dialog_id)"));
    TRY_STATUS(create_notification_group_table());
    TRY_STATUS(create_last_notification_date_index());
    version = current_db_version();
  }
  if (version < static_cast<int32>(DbVersion::AddNotificationsSupport)) {
    TRY_STATUS(create_notification_group_table());
    TRY_STATUS(create_last_notification_date_index());
  }

  return Status::OK();
}

// NB: must happen inside a transaction
Status drop_dialog_db(SqliteDb &db, int version) {
  if (version < static_cast<int32>(DbVersion::DialogDbCreated)) {
    LOG(WARNING) << "Drop old pmc dialog_db";
    SqliteKeyValue kv;
    kv.init_with_connection(db.clone(), "common").ensure();
    kv.erase_by_prefix("di");
  }

  LOG(WARNING) << "Drop dialog_db " << tag("version", version) << tag("current_db_version", current_db_version());
  auto status = db.exec("DROP TABLE IF EXISTS dialogs");
  TRY_STATUS(db.exec("DROP TABLE IF EXISTS notification_groups"));
  return status;
}

class DialogDbImpl : public DialogDbSyncInterface {
 public:
  explicit DialogDbImpl(SqliteDb db) : db_(std::move(db)) {
    init().ensure();
  }

  Status init() {
    TRY_RESULT(add_dialog_stmt, db_.get_statement("INSERT OR REPLACE INTO dialogs VALUES(?1, ?2, ?3)"));
    TRY_RESULT(add_notification_group_stmt,
               db_.get_statement("INSERT OR REPLACE INTO notification_groups VALUES(?1, ?2, ?3)"));
    TRY_RESULT(delete_notification_group_stmt,
               db_.get_statement("DELETE FROM notification_groups WHERE notification_group_id = ?1"));
    TRY_RESULT(get_dialog_stmt, db_.get_statement("SELECT data FROM dialogs WHERE dialog_id = ?1"));
    TRY_RESULT(get_dialogs_stmt, db_.get_statement("SELECT data, dialog_id, dialog_order FROM dialogs WHERE "
                                                   "dialog_order < ?1 OR (dialog_order = ?1 AND dialog_id < ?2) ORDER "
                                                   "BY dialog_order DESC, dialog_id DESC LIMIT ?3"));
    TRY_RESULT(
        get_notification_groups_by_last_notification_date_stmt,
        db_.get_statement("SELECT notification_group_id, dialog_id, last_notification_date FROM notification_groups "
                          "WHERE last_notification_date < ?1 OR (last_notification_date = ?1 "
                          "AND (dialog_id < ?2 OR (dialog_id = ?2 AND notification_group_id < ?3))) ORDER BY "
                          "last_notification_date DESC, dialog_id DESC LIMIT ?4"));
    //                          "WHERE (last_notification_date, dialog_id, notification_group_id) < (?1, ?2, ?3) ORDER BY "
    //                          "last_notification_date DESC, dialog_id DESC, notification_group_id DESC LIMIT ?4"));
    TRY_RESULT(
        get_notification_group_stmt,
        db_.get_statement(
            "SELECT dialog_id, last_notification_date FROM notification_groups WHERE notification_group_id = ?1"));

    add_dialog_stmt_ = std::move(add_dialog_stmt);
    add_notification_group_stmt_ = std::move(add_notification_group_stmt);
    delete_notification_group_stmt_ = std::move(delete_notification_group_stmt);
    get_dialog_stmt_ = std::move(get_dialog_stmt);
    get_dialogs_stmt_ = std::move(get_dialogs_stmt);
    get_notification_groups_by_last_notification_date_stmt_ =
        std::move(get_notification_groups_by_last_notification_date_stmt);
    get_notification_group_stmt_ = std::move(get_notification_group_stmt);

    // LOG(ERROR) << get_dialog_stmt_.explain().ok();
    // LOG(ERROR) << get_dialogs_stmt_.explain().ok();
    // LOG(ERROR) << get_notification_groups_by_last_notification_date_stmt_.explain().ok();
    // LOG(ERROR) << get_notification_group_stmt_.explain().ok();
    // LOG(FATAL) << "EXPLAINED";

    return Status::OK();
  }

  Status add_dialog(DialogId dialog_id, int64 order, BufferSlice data,
                    vector<NotificationGroupKey> notification_groups) override {
    SCOPE_EXIT {
      add_dialog_stmt_.reset();
    };
    add_dialog_stmt_.bind_int64(1, dialog_id.get()).ensure();
    add_dialog_stmt_.bind_int64(2, order).ensure();
    add_dialog_stmt_.bind_blob(3, data.as_slice()).ensure();

    TRY_STATUS(add_dialog_stmt_.step());

    for (auto &to_add : notification_groups) {
      if (to_add.dialog_id.is_valid()) {
        SCOPE_EXIT {
          add_notification_group_stmt_.reset();
        };
        add_notification_group_stmt_.bind_int32(1, to_add.group_id.get()).ensure();
        add_notification_group_stmt_.bind_int64(2, to_add.dialog_id.get()).ensure();
        if (to_add.last_notification_date != 0) {
          add_notification_group_stmt_.bind_int32(3, to_add.last_notification_date).ensure();
        } else {
          add_notification_group_stmt_.bind_null(3).ensure();
        }
        TRY_STATUS(add_notification_group_stmt_.step());
      } else {
        SCOPE_EXIT {
          delete_notification_group_stmt_.reset();
        };
        delete_notification_group_stmt_.bind_int32(1, to_add.group_id.get()).ensure();
        TRY_STATUS(delete_notification_group_stmt_.step());
      }
    }
    return Status::OK();
  }

  Result<BufferSlice> get_dialog(DialogId dialog_id) override {
    SCOPE_EXIT {
      get_dialog_stmt_.reset();
    };

    get_dialog_stmt_.bind_int64(1, dialog_id.get()).ensure();
    TRY_STATUS(get_dialog_stmt_.step());
    if (!get_dialog_stmt_.has_row()) {
      return Status::Error("Not found");
    }
    return BufferSlice(get_dialog_stmt_.view_blob(0));
  }

  Result<NotificationGroupKey> get_notification_group(NotificationGroupId notification_group_id) override {
    SCOPE_EXIT {
      get_notification_group_stmt_.reset();
    };
    get_notification_group_stmt_.bind_int32(1, notification_group_id.get()).ensure();
    TRY_STATUS(get_notification_group_stmt_.step());
    if (!get_notification_group_stmt_.has_row()) {
      return Status::Error("Not found");
    }
    return NotificationGroupKey(notification_group_id, DialogId(get_notification_group_stmt_.view_int64(0)),
                                get_last_notification_date(get_notification_group_stmt_, 1));
  }

  Result<vector<BufferSlice>> get_dialogs(int64 order, DialogId dialog_id, int32 limit) override {
    SCOPE_EXIT {
      get_dialogs_stmt_.reset();
    };

    get_dialogs_stmt_.bind_int64(1, order).ensure();
    get_dialogs_stmt_.bind_int64(2, dialog_id.get()).ensure();
    get_dialogs_stmt_.bind_int32(3, limit).ensure();

    vector<BufferSlice> dialogs;
    TRY_STATUS(get_dialogs_stmt_.step());
    while (get_dialogs_stmt_.has_row()) {
      BufferSlice data(get_dialogs_stmt_.view_blob(0));
      auto loaded_dialog_id = get_dialogs_stmt_.view_int64(1);
      auto loaded_dialog_order = get_dialogs_stmt_.view_int64(2);
      LOG(INFO) << "Load chat " << loaded_dialog_id << " with order " << loaded_dialog_order;
      dialogs.emplace_back(std::move(data));
      TRY_STATUS(get_dialogs_stmt_.step());
    }

    return std::move(dialogs);
  }
  Result<vector<NotificationGroupKey>> get_notification_groups_by_last_notification_date(
      NotificationGroupKey notification_group_key, int32 limit) override {
    auto &stmt = get_notification_groups_by_last_notification_date_stmt_;
    SCOPE_EXIT {
      stmt.reset();
    };

    stmt.bind_int32(1, notification_group_key.last_notification_date).ensure();
    stmt.bind_int64(2, notification_group_key.dialog_id.get()).ensure();
    stmt.bind_int32(3, notification_group_key.group_id.get()).ensure();
    stmt.bind_int32(4, limit).ensure();

    vector<NotificationGroupKey> notification_groups;
    TRY_STATUS(stmt.step());
    while (stmt.has_row()) {
      notification_groups.emplace_back(NotificationGroupId(stmt.view_int32(0)), DialogId(stmt.view_int64(1)),
                                       get_last_notification_date(stmt, 2));
      TRY_STATUS(stmt.step());
    }

    return std::move(notification_groups);
  }
  Status begin_transaction() override {
    return db_.begin_transaction();
  }
  Status commit_transaction() override {
    return db_.commit_transaction();
  }

 private:
  SqliteDb db_;

  SqliteStatement add_dialog_stmt_;
  SqliteStatement add_notification_group_stmt_;
  SqliteStatement delete_notification_group_stmt_;
  SqliteStatement get_dialog_stmt_;
  SqliteStatement get_dialogs_stmt_;
  SqliteStatement get_notification_groups_by_last_notification_date_stmt_;
  SqliteStatement get_notification_group_stmt_;

  static int32 get_last_notification_date(SqliteStatement &stmt, int id) {
    if (stmt.view_datatype(id) == SqliteStatement::Datatype::Null) {
      return 0;
    }
    return stmt.view_int32(id);
  }
};

std::shared_ptr<DialogDbSyncSafeInterface> create_dialog_db_sync(
    std::shared_ptr<SqliteConnectionSafe> sqlite_connection) {
  class DialogDbSyncSafe : public DialogDbSyncSafeInterface {
   public:
    explicit DialogDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> sqlite_connection)
        : lsls_db_([safe_connection = std::move(sqlite_connection)] {
          return make_unique<DialogDbImpl>(safe_connection->get().clone());
        }) {
    }
    DialogDbSyncInterface &get() override {
      return *lsls_db_.get();
    }

   private:
    LazySchedulerLocalStorage<unique_ptr<DialogDbSyncInterface>> lsls_db_;
  };
  return std::make_shared<DialogDbSyncSafe>(std::move(sqlite_connection));
}

class DialogDbAsync : public DialogDbAsyncInterface {
 public:
  DialogDbAsync(std::shared_ptr<DialogDbSyncSafeInterface> sync_db, int32 scheduler_id) {
    impl_ = create_actor_on_scheduler<Impl>("DialogDbActor", scheduler_id, std::move(sync_db));
  }

  void add_dialog(DialogId dialog_id, int64 order, BufferSlice data, vector<NotificationGroupKey> notification_groups,
                  Promise<> promise) override {
    send_closure(impl_, &Impl::add_dialog, dialog_id, order, std::move(data), std::move(notification_groups),
                 std::move(promise));
  }

  void get_notification_groups_by_last_notification_date(NotificationGroupKey notification_group_key, int32 limit,
                                                         Promise<vector<NotificationGroupKey>> promise) override {
    send_closure(impl_, &Impl::get_notification_groups_by_last_notification_date, notification_group_key, limit,
                 std::move(promise));
  }

  void get_notification_group(NotificationGroupId notification_group_id,
                              Promise<NotificationGroupKey> promise) override {
    send_closure(impl_, &Impl::get_notification_group, notification_group_id, std::move(promise));
  }

  void get_dialog(DialogId dialog_id, Promise<BufferSlice> promise) override {
    send_closure_later(impl_, &Impl::get_dialog, dialog_id, std::move(promise));
  }
  void get_dialogs(int64 order, DialogId dialog_id, int32 limit, Promise<vector<BufferSlice>> promise) override {
    send_closure_later(impl_, &Impl::get_dialogs, order, dialog_id, limit, std::move(promise));
  }
  void close(Promise<> promise) override {
    send_closure_later(impl_, &Impl::close, std::move(promise));
  }

 private:
  class Impl : public Actor {
   public:
    explicit Impl(std::shared_ptr<DialogDbSyncSafeInterface> sync_db_safe) : sync_db_safe_(std::move(sync_db_safe)) {
    }
    void add_dialog(DialogId dialog_id, int64 order, BufferSlice data, vector<NotificationGroupKey> notification_groups,
                    Promise<> promise) {
      add_write_query([=, promise = std::move(promise), data = std::move(data),
                       notification_groups = std::move(notification_groups)](Unit) mutable {
        promise.set_result(sync_db_->add_dialog(dialog_id, order, std::move(data), std::move(notification_groups)));
      });
    }

    void get_notification_groups_by_last_notification_date(NotificationGroupKey notification_group_key, int32 limit,
                                                           Promise<vector<NotificationGroupKey>> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_notification_groups_by_last_notification_date(notification_group_key, limit));
    }

    void get_notification_group(NotificationGroupId notification_group_id, Promise<NotificationGroupKey> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_notification_group(notification_group_id));
    }
    void get_dialog(DialogId dialog_id, Promise<BufferSlice> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_dialog(dialog_id));
    }
    void get_dialogs(int64 order, DialogId dialog_id, int32 limit, Promise<vector<BufferSlice>> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_dialogs(order, dialog_id, limit));
    }
    void close(Promise<> promise) {
      do_flush();
      sync_db_safe_.reset();
      sync_db_ = nullptr;
      promise.set_value(Unit());
      stop();
    }

   private:
    std::shared_ptr<DialogDbSyncSafeInterface> sync_db_safe_;
    DialogDbSyncInterface *sync_db_ = nullptr;

    static constexpr size_t MAX_PENDING_QUERIES_COUNT{50};
    static constexpr double MAX_PENDING_QUERIES_DELAY{0.01};
    vector<Promise<>> pending_writes_;
    double wakeup_at_ = 0;
    template <class F>
    void add_write_query(F &&f) {
      pending_writes_.push_back(PromiseCreator::lambda(std::forward<F>(f), PromiseCreator::Ignore()));
      if (pending_writes_.size() > MAX_PENDING_QUERIES_COUNT) {
        do_flush();
        wakeup_at_ = 0;
      } else if (wakeup_at_ == 0) {
        wakeup_at_ = Time::now_cached() + MAX_PENDING_QUERIES_DELAY;
      }
      if (wakeup_at_ != 0) {
        set_timeout_at(wakeup_at_);
      }
    }
    void add_read_query() {
      do_flush();
    }
    void do_flush() {
      if (pending_writes_.empty()) {
        return;
      }
      sync_db_->begin_transaction().ensure();
      for (auto &query : pending_writes_) {
        query.set_value(Unit());
      }
      sync_db_->commit_transaction().ensure();
      pending_writes_.clear();
      cancel_timeout();
    }
    void timeout_expired() override {
      do_flush();
    }

    void start_up() override {
      sync_db_ = &sync_db_safe_->get();
    }
  };
  ActorOwn<Impl> impl_;
};

std::shared_ptr<DialogDbAsyncInterface> create_dialog_db_async(std::shared_ptr<DialogDbSyncSafeInterface> sync_db,
                                                               int32 scheduler_id) {
  return std::make_shared<DialogDbAsync>(std::move(sync_db), scheduler_id);
}

}  // namespace td
