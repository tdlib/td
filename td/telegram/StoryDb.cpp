//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StoryDb.h"

#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/UserId.h"
#include "td/telegram/Version.h"

#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteStatement.h"

#include "td/actor/actor.h"
#include "td/actor/SchedulerLocalStorage.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/unicode.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <tuple>
#include <utility>

namespace td {

// NB: must happen inside a transaction
Status init_story_db(SqliteDb &db, int32 version) {
  LOG(INFO) << "Init story database " << tag("version", version);

  // Check if database exists
  TRY_RESULT(has_table, db.has_table("stories"));
  if (!has_table) {
    version = 0;
  } else if (version < static_cast<int32>(DbVersion::DialogDbCreated) || version > current_db_version()) {
    TRY_STATUS(drop_story_db(db, version));
    version = 0;
  }

  if (version == 0) {
    LOG(INFO) << "Create new story database";
    TRY_STATUS(
        db.exec("CREATE TABLE IF NOT EXISTS stories (dialog_id INT8, story_id INT4, expires_at INT4, notification_id "
                "INT4, data BLOB, PRIMARY KEY (dialog_id, story_id))"));

    TRY_STATUS(db.exec("CREATE INDEX IF NOT EXISTS story_by_ttl ON stories (expires_at) WHERE expires_at IS NOT NULL"));

    TRY_STATUS(
        db.exec("CREATE INDEX IF NOT EXISTS story_by_notification_id ON stories (dialog_id, notification_id) WHERE "
                "notification_id IS NOT NULL"));

    version = current_db_version();
  }
  return Status::OK();
}

// NB: must happen inside a transaction
Status drop_story_db(SqliteDb &db, int32 version) {
  LOG(WARNING) << "Drop story database " << tag("version", version) << tag("current_db_version", current_db_version());
  return db.exec("DROP TABLE IF EXISTS stories");
}

class StoryDbImpl final : public StoryDbSyncInterface {
 public:
  explicit StoryDbImpl(SqliteDb db) : db_(std::move(db)) {
    init().ensure();
  }

  Status init() {
    TRY_RESULT_ASSIGN(add_story_stmt_, db_.get_statement("INSERT OR REPLACE INTO stories VALUES(?1, ?2, ?3, ?4, ?5)"));
    TRY_RESULT_ASSIGN(delete_story_stmt_,
                      db_.get_statement("DELETE FROM stories WHERE dialog_id = ?1 AND story_id = ?2"));
    TRY_RESULT_ASSIGN(get_story_stmt_,
                      db_.get_statement("SELECT data FROM stories WHERE dialog_id = ?1 AND story_id = ?2"));

    TRY_RESULT_ASSIGN(get_expiring_stories_stmt_,
                      db_.get_statement("SELECT data FROM stories WHERE expires_at <= ?1 LIMIT ?2"));

    TRY_RESULT_ASSIGN(get_stories_from_notification_id_stmt_,
                      db_.get_statement("SELECT data FROM stories WHERE dialog_id = ?1 AND "
                                        "notification_id < ?2 ORDER BY notification_id DESC LIMIT ?3"));

    return Status::OK();
  }

  void add_story(StoryFullId story_full_id, int32 expires_at, NotificationId notification_id, BufferSlice data) final {
    LOG(INFO) << "Add " << story_full_id << " to database";
    auto dialog_id = story_full_id.get_dialog_id();
    auto story_id = story_full_id.get_story_id();
    LOG_CHECK(dialog_id.is_valid()) << dialog_id << ' ' << story_id << ' ' << story_full_id;
    CHECK(story_id.is_valid());
    SCOPE_EXIT {
      add_story_stmt_.reset();
    };

    add_story_stmt_.bind_int64(1, dialog_id.get()).ensure();
    add_story_stmt_.bind_int32(2, story_id.get()).ensure();
    if (expires_at != 0) {
      add_story_stmt_.bind_int32(3, expires_at).ensure();
    } else {
      add_story_stmt_.bind_null(3).ensure();
    }
    if (notification_id.is_valid()) {
      add_story_stmt_.bind_int32(4, notification_id.get()).ensure();
    } else {
      add_story_stmt_.bind_null(4).ensure();
    }
    add_story_stmt_.bind_blob(5, data.as_slice()).ensure();

    add_story_stmt_.step().ensure();
  }

  void delete_story(StoryFullId story_full_id) final {
    LOG(INFO) << "Delete " << story_full_id << " from database";
    auto dialog_id = story_full_id.get_dialog_id();
    auto story_id = story_full_id.get_story_id();
    CHECK(dialog_id.is_valid());
    CHECK(story_id.is_valid());
    SCOPE_EXIT {
      delete_story_stmt_.reset();
    };
    delete_story_stmt_.bind_int64(1, dialog_id.get()).ensure();
    delete_story_stmt_.bind_int32(2, story_id.get()).ensure();
    delete_story_stmt_.step().ensure();
  }

  Result<BufferSlice> get_story(StoryFullId story_full_id) final {
    auto dialog_id = story_full_id.get_dialog_id();
    auto story_id = story_full_id.get_story_id();
    CHECK(dialog_id.is_valid());
    CHECK(story_id.is_valid());
    SCOPE_EXIT {
      get_story_stmt_.reset();
    };

    get_story_stmt_.bind_int64(1, dialog_id.get()).ensure();
    get_story_stmt_.bind_int32(2, story_id.get()).ensure();
    get_story_stmt_.step().ensure();
    if (!get_story_stmt_.has_row()) {
      return Status::Error("Not found");
    }
    return BufferSlice(get_story_stmt_.view_blob(0));
  }

  vector<BufferSlice> get_expiring_stories(int32 expires_till, int32 limit) final {
    SCOPE_EXIT {
      get_expiring_stories_stmt_.reset();
    };

    vector<BufferSlice> stories;
    get_expiring_stories_stmt_.bind_int32(1, expires_till).ensure();
    get_expiring_stories_stmt_.bind_int32(2, limit).ensure();
    get_expiring_stories_stmt_.step().ensure();

    while (get_expiring_stories_stmt_.has_row()) {
      stories.emplace_back(get_expiring_stories_stmt_.view_blob(0));
      get_expiring_stories_stmt_.step().ensure();
    }

    return stories;
  }

  vector<BufferSlice> get_stories_from_notification_id(DialogId dialog_id, NotificationId from_notification_id,
                                                       int32 limit) final {
    auto &stmt = get_stories_from_notification_id_stmt_;
    SCOPE_EXIT {
      stmt.reset();
    };
    stmt.bind_int64(1, dialog_id.get()).ensure();
    stmt.bind_int32(2, from_notification_id.get()).ensure();
    stmt.bind_int32(3, limit).ensure();

    vector<BufferSlice> result;
    stmt.step().ensure();
    while (stmt.has_row()) {
      result.emplace_back(stmt.view_blob(0));
      stmt.step().ensure();
    }
    return result;
  }

  Status begin_write_transaction() final {
    return db_.begin_write_transaction();
  }
  Status commit_transaction() final {
    return db_.commit_transaction();
  }

 private:
  SqliteDb db_;

  SqliteStatement add_story_stmt_;
  SqliteStatement delete_story_stmt_;
  SqliteStatement get_story_stmt_;
  SqliteStatement get_expiring_stories_stmt_;
  SqliteStatement get_stories_from_notification_id_stmt_;
};

std::shared_ptr<StoryDbSyncSafeInterface> create_story_db_sync(
    std::shared_ptr<SqliteConnectionSafe> sqlite_connection) {
  class StoryDbSyncSafe final : public StoryDbSyncSafeInterface {
   public:
    explicit StoryDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> sqlite_connection)
        : lsls_db_([safe_connection = std::move(sqlite_connection)] {
          return make_unique<StoryDbImpl>(safe_connection->get().clone());
        }) {
    }
    StoryDbSyncInterface &get() final {
      return *lsls_db_.get();
    }

   private:
    LazySchedulerLocalStorage<unique_ptr<StoryDbSyncInterface>> lsls_db_;
  };
  return std::make_shared<StoryDbSyncSafe>(std::move(sqlite_connection));
}

class StoryDbAsync final : public StoryDbAsyncInterface {
 public:
  StoryDbAsync(std::shared_ptr<StoryDbSyncSafeInterface> sync_db, int32 scheduler_id) {
    impl_ = create_actor_on_scheduler<Impl>("StoryDbActor", scheduler_id, std::move(sync_db));
  }

  void add_story(StoryFullId story_full_id, int32 expires_at, NotificationId notification_id, BufferSlice data,
                 Promise<Unit> promise) final {
    send_closure_later(impl_, &Impl::add_story, story_full_id, expires_at, notification_id, std::move(data),
                       std::move(promise));
  }

  void delete_story(StoryFullId story_full_id, Promise<Unit> promise) final {
    send_closure_later(impl_, &Impl::delete_story, story_full_id, std::move(promise));
  }

  void get_story(StoryFullId story_full_id, Promise<BufferSlice> promise) final {
    send_closure_later(impl_, &Impl::get_story, story_full_id, std::move(promise));
  }

  void get_expiring_stories(int32 expires_till, int32 limit, Promise<vector<BufferSlice>> promise) final {
    send_closure_later(impl_, &Impl::get_expiring_stories, expires_till, limit, std::move(promise));
  }

  void get_stories_from_notification_id(DialogId dialog_id, NotificationId from_notification_id, int32 limit,
                                        Promise<vector<BufferSlice>> promise) final {
    send_closure_later(impl_, &Impl::get_stories_from_notification_id, dialog_id, from_notification_id, limit,
                       std::move(promise));
  }

  void close(Promise<Unit> promise) final {
    send_closure_later(impl_, &Impl::close, std::move(promise));
  }

  void force_flush() final {
    send_closure_later(impl_, &Impl::force_flush);
  }

 private:
  class Impl final : public Actor {
   public:
    explicit Impl(std::shared_ptr<StoryDbSyncSafeInterface> sync_db_safe) : sync_db_safe_(std::move(sync_db_safe)) {
    }
    void add_story(StoryFullId story_full_id, int32 expires_at, NotificationId notification_id, BufferSlice data,
                   Promise<Unit> promise) {
      add_write_query([this, story_full_id, expires_at, notification_id, data = std::move(data),
                       promise = std::move(promise)](Unit) mutable {
        sync_db_->add_story(story_full_id, expires_at, notification_id, std::move(data));
        on_write_result(std::move(promise));
      });
    }

    void delete_story(StoryFullId story_full_id, Promise<Unit> promise) {
      add_write_query([this, story_full_id, promise = std::move(promise)](Unit) mutable {
        sync_db_->delete_story(story_full_id);
        on_write_result(std::move(promise));
      });
    }

    void on_write_result(Promise<Unit> &&promise) {
      // We are inside a transaction and don't know how to handle errors
      finished_writes_.push_back(std::move(promise));
    }

    void get_story(StoryFullId story_full_id, Promise<BufferSlice> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_story(story_full_id));
    }

    void get_expiring_stories(int32 expires_till, int32 limit, Promise<vector<BufferSlice>> promise) {
      add_read_query();
      promise.set_value(sync_db_->get_expiring_stories(expires_till, limit));
    }

    void get_stories_from_notification_id(DialogId dialog_id, NotificationId from_notification_id, int32 limit,
                                          Promise<vector<BufferSlice>> promise) {
      add_read_query();
      promise.set_value(sync_db_->get_stories_from_notification_id(dialog_id, from_notification_id, limit));
    }

    void close(Promise<Unit> promise) {
      do_flush();
      sync_db_safe_.reset();
      sync_db_ = nullptr;
      promise.set_value(Unit());
      stop();
    }

    void force_flush() {
      do_flush();
      LOG(INFO) << "StoryDb flushed";
    }

   private:
    std::shared_ptr<StoryDbSyncSafeInterface> sync_db_safe_;
    StoryDbSyncInterface *sync_db_ = nullptr;

    static constexpr size_t MAX_PENDING_QUERIES_COUNT{50};
    static constexpr double MAX_PENDING_QUERIES_DELAY{0.01};

    //NB: order is important, destructor of pending_writes_ will change finished_writes_
    vector<Promise<Unit>> finished_writes_;
    vector<Promise<Unit>> pending_writes_;  // TODO use Action
    double wakeup_at_ = 0;

    template <class F>
    void add_write_query(F &&f) {
      pending_writes_.push_back(PromiseCreator::lambda(std::forward<F>(f)));
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
      sync_db_->begin_write_transaction().ensure();
      set_promises(pending_writes_);
      sync_db_->commit_transaction().ensure();
      set_promises(finished_writes_);
      cancel_timeout();
    }
    void timeout_expired() final {
      do_flush();
    }

    void start_up() final {
      sync_db_ = &sync_db_safe_->get();
    }
  };
  ActorOwn<Impl> impl_;
};

std::shared_ptr<StoryDbAsyncInterface> create_story_db_async(std::shared_ptr<StoryDbSyncSafeInterface> sync_db,
                                                             int32 scheduler_id) {
  return std::make_shared<StoryDbAsync>(std::move(sync_db), scheduler_id);
}

}  // namespace td
