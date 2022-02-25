//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DownloadsDb.h"

#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/Version.h"

#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteStatement.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
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

static constexpr int32 MESSAGES_DB_INDEX_COUNT = 30;
static constexpr int32 MESSAGES_DB_INDEX_COUNT_OLD = 9;

// NB: must happen inside a transaction
Status init_downloads_db(SqliteDb &db, int32 version) {
  LOG(INFO) << "Init downloads database " << tag("version", version);

  // Check if database exists
  TRY_RESULT(has_table, db.has_table("downloads"));
  if (!has_table) {
    version = 0;
  }

  auto add_fts = [&db] {
    TRY_STATUS(
        db.exec("CREATE VIRTUAL TABLE IF NOT EXISTS downloads_fts USING fts5(search_text, content='downloads', "
                "content_rowid='download_id', tokenize = \"unicode61 remove_diacritics 0 tokenchars '\a'\")"));
    TRY_STATUS(
        db.exec("CREATE TRIGGER IF NOT EXISTS trigger_downloads_fts_delete BEFORE DELETE ON downloads"
                " BEGIN INSERT INTO downloads_fts(downloads_fts, rowid, search_text) VALUES(\'delete\', "
                "OLD.download_id, OLD.search_text); END"));
    TRY_STATUS(
        db.exec("CREATE TRIGGER IF NOT EXISTS trigger_downloads_fts_insert AFTER INSERT ON downloads"
                " BEGIN INSERT INTO downloads_fts(rowid, search_text) VALUES(NEW.download_id, NEW.search_text); END"));
    // TODO: update?

    return Status::OK();
  };

  if (version == 0) {
    TRY_STATUS(
        db.exec("CREATE TABLE IF NOT EXISTS downloads(download_id INT8 PRIMARY KEY, unique_file_id "
                "BLOB UNIQUE, file_source BLOB, search_text STRING, date INT4, priority INT4)"));
    // TODO: add indexes
    //    TRY_STATUS(
    //        db.exec("CREATE INDEX IF NOT EXISTS message_by_random_id ON messages (dialog_id, random_id) "
    //                "WHERE random_id IS NOT NULL"));

    TRY_STATUS(add_fts());

    version = current_db_version();
  }
  return Status::OK();
}

// NB: must happen inside a transaction
Status drop_downloads_db(SqliteDb &db, int32 version) {
  LOG(WARNING) << "Drop downloads database " << tag("version", version)
               << tag("current_db_version", current_db_version());
  return db.exec("DROP TABLE IF EXISTS downloads");
}

class DownloadsDbImpl final : public DownloadsDbSyncInterface {
 public:
  explicit DownloadsDbImpl(SqliteDb db) : db_(std::move(db)) {
    init().ensure();
  }

  Status init() {
    TRY_RESULT_ASSIGN(add_download_stmt_,
                      db_.get_statement("INSERT OR REPLACE INTO downloads VALUES(NULL, ?1, ?2, ?3, ?4, ?5)"));
    TRY_RESULT_ASSIGN(
        get_downloads_fts_stmt_,
        db_.get_statement("SELECT download_id, unique_file_id, file_source, priority FROM downloads WHERE download_id "
                          "IN (SELECT rowid FROM downloads_fts WHERE downloads_fts MATCH ?1 AND rowid < ?2 "
                          "ORDER BY rowid DESC LIMIT ?3) ORDER BY download_id DESC"));

    // LOG(ERROR) << get_message_stmt_.explain().ok();
    // LOG(ERROR) << get_messages_from_notification_id_stmt.explain().ok();
    // LOG(ERROR) << get_message_by_random_id_stmt_.explain().ok();
    // LOG(ERROR) << get_message_by_unique_message_id_stmt_.explain().ok();

    // LOG(ERROR) << get_expiring_messages_stmt_.explain().ok();
    // LOG(ERROR) << get_expiring_messages_helper_stmt_.explain().ok();

    // LOG(FATAL) << "EXPLAINED";

    return Status::OK();
  }

  Result<DownloadsDbFtsResult> get_downloads_fts(DownloadsDbFtsQuery query) final {
    SCOPE_EXIT {
      get_downloads_fts_stmt_.reset();
    };

    auto &stmt = get_downloads_fts_stmt_;
    stmt.bind_string(1, query.query).ensure();
    stmt.bind_int64(2, query.offset).ensure();
    stmt.bind_int32(3, query.limit).ensure();
    DownloadsDbFtsResult result;
    auto status = stmt.step();
    if (status.is_error()) {
      LOG(ERROR) << status;
      return std::move(result);
    }
    while (stmt.has_row()) {
      int64 download_id{stmt.view_int64(0)};
      string unique_file_id{stmt.view_string(1).str()};
      string file_source{stmt.view_string(2).str()};
      int32 priority{stmt.view_int32(3)};
      result.next_download_id = download_id;
      result.downloads.push_back(DownloadsDbDownloadShort{std::move(unique_file_id), std::move(file_source), priority});
      stmt.step().ensure();
    }
    return std::move(result);
  }

  Status begin_write_transaction() final {
    return db_.begin_write_transaction();
  }
  Status commit_transaction() final {
    return db_.commit_transaction();
  }

  Status add_download(DownloadsDbDownload download) override {
    SCOPE_EXIT {
      add_download_stmt_.reset();
    };
    auto &stmt = add_download_stmt_;

    TRY_RESULT_ASSIGN(add_download_stmt_,
                      db_.get_statement("INSERT OR REPLACE INTO downloads VALUES(NULL, ?1, ?2, ?3, ?4, ?5)"));
    stmt.bind_blob(1, download.unique_file_id).ensure();
    stmt.bind_blob(2, download.file_source).ensure();
    stmt.bind_string(3, download.search_text).ensure();
    stmt.bind_int32(4, download.date).ensure();
    stmt.bind_int32(5, download.priority).ensure();

    stmt.step().ensure();

    return Status();
  }
  Result<GetActiveDownloadsResult> get_active_downloads() override {
    DownloadsDbFtsQuery query;
    query.limit = 2000;
    query.offset = uint64(1) << 60;
    // TODO: optimize query
    // TODO: only active
    TRY_RESULT(ans, get_downloads_fts(query));
    return GetActiveDownloadsResult{std::move(ans.downloads)};
  }

 private:
  SqliteDb db_;

  SqliteStatement add_download_stmt_;
  SqliteStatement get_downloads_fts_stmt_;
};

std::shared_ptr<DownloadsDbSyncSafeInterface> create_downloads_db_sync(
    std::shared_ptr<SqliteConnectionSafe> sqlite_connection) {
  class DownloadsDbSyncSafe final : public DownloadsDbSyncSafeInterface {
   public:
    explicit DownloadsDbSyncSafe(std::shared_ptr<SqliteConnectionSafe> sqlite_connection)
        : lsls_db_([safe_connection = std::move(sqlite_connection)] {
          return make_unique<DownloadsDbImpl>(safe_connection->get().clone());
        }) {
    }
    DownloadsDbSyncInterface &get() final {
      return *lsls_db_.get();
    }

   private:
    LazySchedulerLocalStorage<unique_ptr<DownloadsDbSyncInterface>> lsls_db_;
  };
  return std::make_shared<DownloadsDbSyncSafe>(std::move(sqlite_connection));
}

class DownloadsDbAsync final : public DownloadsDbAsyncInterface {
 public:
  DownloadsDbAsync(std::shared_ptr<DownloadsDbSyncSafeInterface> sync_db, int32 scheduler_id) {
    impl_ = create_actor_on_scheduler<Impl>("DownloadsDbActor", scheduler_id, std::move(sync_db));
  }

  void add_download(DownloadsDbDownload query, Promise<> promise) final {
    send_closure(impl_, &Impl::add_download, std::move(query), std::move(promise));
  }

  void get_active_downloads(Promise<GetActiveDownloadsResult> promise) final {
    send_closure(impl_, &Impl::get_active_downloads, std::move(promise));
  }
  void get_downloads_fts(DownloadsDbFtsQuery query, Promise<DownloadsDbFtsResult> promise) final {
    send_closure(impl_, &Impl::get_downloads_fts, std::move(query), std::move(promise));
  }

  void close(Promise<> promise) final {
    send_closure_later(impl_, &Impl::close, std::move(promise));
  }

  void force_flush() final {
    send_closure_later(impl_, &Impl::force_flush);
  }

 private:
  class Impl final : public Actor {
   public:
    explicit Impl(std::shared_ptr<DownloadsDbSyncSafeInterface> sync_db_safe) : sync_db_safe_(std::move(sync_db_safe)) {
    }
    void add_download(DownloadsDbDownload query, Promise<> promise) {
      add_write_query([this, query = std::move(query), promise = std::move(promise)](Unit) mutable {
        on_write_result(std::move(promise), sync_db_->add_download(std::move(query)));
      });
    }

    void get_downloads_fts(DownloadsDbFtsQuery query, Promise<DownloadsDbFtsResult> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_downloads_fts(std::move(query)));
    }

    void get_active_downloads(Promise<> promise) {
      add_read_query();
      promise.set_result(sync_db_->get_active_downloads());
    }

    void close(Promise<> promise) {
      do_flush();
      sync_db_safe_.reset();
      sync_db_ = nullptr;
      promise.set_value(Unit());
      stop();
    }

    void force_flush() {
      LOG(INFO) << "DownloadsDb flushed";
      do_flush();
    }

   private:
    std::shared_ptr<DownloadsDbSyncSafeInterface> sync_db_safe_;
    DownloadsDbSyncInterface *sync_db_ = nullptr;

    static constexpr size_t MAX_PENDING_QUERIES_COUNT{50};
    static constexpr double MAX_PENDING_QUERIES_DELAY{0.01};

    //NB: order is important, destructor of pending_writes_ will change pending_write_results_
    vector<std::pair<Promise<>, Status>> pending_write_results_;
    vector<Promise<>> pending_writes_;
    double wakeup_at_ = 0;

    void on_write_result(Promise<> promise, Status status) {
      // We are inside a transaction and don't know how to handle the error
      status.ensure();
      pending_write_results_.emplace_back(std::move(promise), std::move(status));
    }

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
      sync_db_->begin_write_transaction().ensure();
      for (auto &query : pending_writes_) {
        query.set_value(Unit());
      }
      sync_db_->commit_transaction().ensure();
      pending_writes_.clear();
      for (auto &p : pending_write_results_) {
        p.first.set_result(std::move(p.second));
      }
      pending_write_results_.clear();
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

std::shared_ptr<DownloadsDbAsyncInterface> create_downloads_db_async(
    std::shared_ptr<DownloadsDbSyncSafeInterface> sync_db, int32 scheduler_id) {
  return std::make_shared<DownloadsDbAsync>(std::move(sync_db), scheduler_id);
}

}  // namespace td
