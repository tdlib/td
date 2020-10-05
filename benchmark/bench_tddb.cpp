//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesDb.h"
#include "td/telegram/NotificationId.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"

#include "td/utils/benchmark.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"

#include <memory>

namespace td {

static Status init_db(SqliteDb &db) {
  TRY_STATUS(db.exec("PRAGMA encoding=\"UTF-8\""));
  TRY_STATUS(db.exec("PRAGMA synchronous=NORMAL"));
  TRY_STATUS(db.exec("PRAGMA journal_mode=WAL"));
  TRY_STATUS(db.exec("PRAGMA temp_store=MEMORY"));
  TRY_STATUS(db.exec("PRAGMA secure_delete=1"));
  return Status::OK();
}

class MessagesDbBench : public Benchmark {
 public:
  string get_description() const override {
    return "MessagesDb";
  }
  void start_up() override {
    LOG(ERROR) << "START UP";
    do_start_up().ensure();
    scheduler_->start();
  }
  void run(int n) override {
    auto guard = scheduler_->get_main_guard();
    for (int i = 0; i < n; i += 20) {
      auto dialog_id = DialogId{UserId{Random::fast(1, 100)}};
      auto message_id_raw = Random::fast(1, 100000);
      for (int j = 0; j < 20; j++) {
        auto message_id = MessageId{ServerMessageId{message_id_raw + j}};
        auto unique_message_id = ServerMessageId{i + 1};
        auto sender_user_id = UserId{Random::fast(1, 1000)};
        auto random_id = i + 1;
        auto ttl_expires_at = 0;
        auto data = BufferSlice(Random::fast(100, 299));

        // use async on same thread.
        messages_db_async_->add_message({dialog_id, message_id}, unique_message_id, sender_user_id, random_id,
                                        ttl_expires_at, 0, 0, "", NotificationId(), MessageId(), std::move(data),
                                        Promise<>());
      }
    }
  }
  void tear_down() override {
    scheduler_->run_main(0.1);
    {
      auto guard = scheduler_->get_main_guard();
      sql_connection_.reset();
      messages_db_sync_safe_.reset();
      messages_db_async_.reset();
    }

    scheduler_->finish();
    scheduler_.reset();
    LOG(ERROR) << "TEAR DOWN";
  }

 private:
  td::unique_ptr<ConcurrentScheduler> scheduler_;
  std::shared_ptr<SqliteConnectionSafe> sql_connection_;
  std::shared_ptr<MessagesDbSyncSafeInterface> messages_db_sync_safe_;
  std::shared_ptr<MessagesDbAsyncInterface> messages_db_async_;

  Status do_start_up() {
    scheduler_ = make_unique<ConcurrentScheduler>();
    scheduler_->init(1);

    auto guard = scheduler_->get_main_guard();

    string sql_db_name = "testdb.sqlite";
    sql_connection_ = std::make_shared<SqliteConnectionSafe>(sql_db_name);
    auto &db = sql_connection_->get();
    TRY_STATUS(init_db(db));

    db.exec("BEGIN TRANSACTION").ensure();
    // version == 0 ==> db will be destroyed
    TRY_STATUS(init_messages_db(db, 0));
    db.exec("COMMIT TRANSACTION").ensure();

    messages_db_sync_safe_ = create_messages_db_sync(sql_connection_);
    messages_db_async_ = create_messages_db_async(messages_db_sync_safe_, 0);
    return Status::OK();
  }
};
}  // namespace td

int main() {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(WARNING));
  bench(td::MessagesDbBench());
}
