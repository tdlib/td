//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "data.h"

#include "td/db/binlog/BinlogHelper.h"
#include "td/db/binlog/ConcurrentBinlog.h"
#include "td/db/BinlogKeyValue.h"
#include "td/db/DbKey.h"
#include "td/db/SeqKeyValue.h"
#include "td/db/SqliteConnectionSafe.h"
#include "td/db/SqliteDb.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueSafe.h"
#include "td/db/TsSeqKeyValue.h"

#include "td/actor/actor.h"

#include "td/utils/base64.h"
#include "td/utils/common.h"
#include "td/utils/filesystem.h"
#include "td/utils/logging.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/tests.h"

#include <limits>
#include <map>
#include <memory>

REGISTER_TESTS(db);

using namespace td;

template <class ContainerT>
static typename ContainerT::value_type &rand_elem(ContainerT &cont) {
  CHECK(0 < cont.size() && cont.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
  return cont[Random::fast(0, static_cast<int>(cont.size()) - 1)];
}

TEST(DB, binlog_encryption_bug) {
  CSlice binlog_name = "test_binlog";
  Binlog::destroy(binlog_name).ignore();

  auto cucumber = DbKey::password("cucumber");
  auto empty = DbKey::empty();
  {
    Binlog binlog;
    binlog
        .init(
            binlog_name.str(), [&](const BinlogEvent &x) {}, cucumber)
        .ensure();
  }
  {
    Binlog binlog;
    binlog
        .init(
            binlog_name.str(), [&](const BinlogEvent &x) {}, cucumber)
        .ensure();
  }
}

TEST(DB, binlog_encryption) {
  CSlice binlog_name = "test_binlog";
  Binlog::destroy(binlog_name).ignore();

  auto hello = DbKey::raw_key(std::string(32, 'A'));
  auto cucumber = DbKey::password("cucumber");
  auto empty = DbKey::empty();
  auto long_data = string(10000, 'Z');
  {
    Binlog binlog;
    binlog.init(binlog_name.str(), [](const BinlogEvent &x) {}).ensure();
    binlog.add_raw_event(BinlogEvent::create_raw(binlog.next_id(), 1, 0, create_storer("AAAA")),
                         BinlogDebugInfo{__FILE__, __LINE__});
    binlog.add_raw_event(BinlogEvent::create_raw(binlog.next_id(), 1, 0, create_storer("BBBB")),
                         BinlogDebugInfo{__FILE__, __LINE__});
    binlog.add_raw_event(BinlogEvent::create_raw(binlog.next_id(), 1, 0, create_storer(long_data)),
                         BinlogDebugInfo{__FILE__, __LINE__});
    LOG(INFO) << "SET PASSWORD";
    binlog.change_key(cucumber);
    binlog.change_key(hello);
    LOG(INFO) << "OK";
    binlog.add_raw_event(BinlogEvent::create_raw(binlog.next_id(), 1, 0, create_storer("CCCC")),
                         BinlogDebugInfo{__FILE__, __LINE__});
    binlog.close().ensure();
  }

  auto add_suffix = [&] {
    auto fd = FileFd::open(binlog_name, FileFd::Flags::Write | FileFd::Flags::Append).move_as_ok();
    fd.write("abacabadaba").ensure();
  };

  add_suffix();

  {
    std::vector<string> v;
    LOG(INFO) << "RESTART";
    Binlog binlog;
    binlog
        .init(
            binlog_name.str(), [&](const BinlogEvent &x) { v.push_back(x.data_.str()); }, hello)
        .ensure();
    CHECK(v == std::vector<string>({"AAAA", "BBBB", long_data, "CCCC"}));
  }

  add_suffix();

  {
    std::vector<string> v;
    LOG(INFO) << "RESTART";
    Binlog binlog;
    auto status = binlog.init(
        binlog_name.str(), [&](const BinlogEvent &x) { v.push_back(x.data_.str()); }, cucumber);
    CHECK(status.is_error());
  }

  add_suffix();

  {
    std::vector<string> v;
    LOG(INFO) << "RESTART";
    Binlog binlog;
    auto status = binlog.init(
        binlog_name.str(), [&](const BinlogEvent &x) { v.push_back(x.data_.str()); }, cucumber, hello);
    CHECK(v == std::vector<string>({"AAAA", "BBBB", long_data, "CCCC"}));
  }
};

TEST(DB, sqlite_lfs) {
  string path = "test_sqlite_db";
  SqliteDb::destroy(path).ignore();
  SqliteDb db;
  db.init(path).ensure();
  db.exec("PRAGMA journal_mode=WAL").ensure();
  db.exec("PRAGMA user_version").ensure();
}

TEST(DB, sqlite_encryption) {
  string path = "test_sqlite_db";
  SqliteDb::destroy(path).ignore();

  auto empty = DbKey::empty();
  auto cucumber = DbKey::password("cucumber");
  auto tomato = DbKey::raw_key(string(32, 'a'));

  {
    auto db = SqliteDb::open_with_key(path, empty).move_as_ok();
    db.set_user_version(123).ensure();
    auto kv = SqliteKeyValue();
    kv.init_with_connection(db.clone(), "kv").ensure();
    kv.set("a", "b");
  }
  SqliteDb::open_with_key(path, cucumber).ensure_error();  // key was set...

  SqliteDb::change_key(path, cucumber, empty).ensure();

  SqliteDb::open_with_key(path, tomato).ensure_error();
  {
    auto db = SqliteDb::open_with_key(path, cucumber).move_as_ok();
    auto kv = SqliteKeyValue();
    kv.init_with_connection(db.clone(), "kv").ensure();
    CHECK(kv.get("a") == "b");
    CHECK(db.user_version().ok() == 123);
  }

  SqliteDb::change_key(path, tomato, cucumber).ensure();
  SqliteDb::change_key(path, tomato, cucumber).ensure();

  SqliteDb::open_with_key(path, cucumber).ensure_error();
  {
    auto db = SqliteDb::open_with_key(path, tomato).move_as_ok();
    auto kv = SqliteKeyValue();
    kv.init_with_connection(db.clone(), "kv").ensure();
    CHECK(kv.get("a") == "b");
    CHECK(db.user_version().ok() == 123);
  }

  SqliteDb::change_key(path, empty, tomato).ensure();
  SqliteDb::change_key(path, empty, tomato).ensure();

  {
    auto db = SqliteDb::open_with_key(path, empty).move_as_ok();
    auto kv = SqliteKeyValue();
    kv.init_with_connection(db.clone(), "kv").ensure();
    CHECK(kv.get("a") == "b");
    CHECK(db.user_version().ok() == 123);
  }
  SqliteDb::open_with_key(path, cucumber).ensure_error();
}

TEST(DB, sqlite_encryption_migrate) {
  string path = "test_sqlite_db";
  SqliteDb::destroy(path).ignore();
  auto cucumber = DbKey::password("cucumber");
  auto empty = DbKey::empty();
  if (false) {
    // sqlite_sample_db was generated by the following code
    {
      SqliteDb::change_key(path, cucumber, empty).ensure();
      auto db = SqliteDb::open_with_key(path, cucumber).move_as_ok();
      db.set_user_version(123).ensure();
      auto kv = SqliteKeyValue();
      kv.init_with_connection(db.clone(), "kv").ensure();
      kv.set("hello", "world");
    }
    LOG(ERROR) << base64_encode(read_file(path).move_as_ok());
  }
  write_file(path, base64_decode(Slice(sqlite_sample_db, sqlite_sample_db_size)).move_as_ok()).ensure();
  {
    auto db = SqliteDb::open_with_key(path, cucumber).move_as_ok();
    auto kv = SqliteKeyValue();
    kv.init_with_connection(db.clone(), "kv").ensure();
    CHECK(kv.get("hello") == "world");
    CHECK(db.user_version().ok() == 123);
  }
}

using SeqNo = uint64;
struct DbQuery {
  enum class Type { Get, Set, Erase } type = Type::Get;
  SeqNo tid = 0;
  int32 id = 0;
  string key;
  string value;
};

template <class ImplT>
class QueryHandler {
 public:
  ImplT &impl() {
    return impl_;
  }
  void do_query(DbQuery &query) {
    switch (query.type) {
      case DbQuery::Type::Get:
        query.value = impl_.get(query.key);
        return;
      case DbQuery::Type::Set:
        query.tid = impl_.set(query.key, query.value);
        return;
      case DbQuery::Type::Erase:
        query.tid = impl_.erase(query.key);
        return;
    }
  }

 private:
  ImplT impl_;
};

class SqliteKV {
 public:
  string get(string key) {
    return kv_->get().get(key);
  }
  SeqNo set(string key, string value) {
    kv_->get().set(key, value);
    return 0;
  }
  SeqNo erase(string key) {
    kv_->get().erase(key);
    return 0;
  }
  Status init(string name) {
    auto sql_connection = std::make_shared<SqliteConnectionSafe>(name);
    kv_ = std::make_shared<SqliteKeyValueSafe>("kv", sql_connection);
    return Status::OK();
  }
  void close() {
    kv_.reset();
  }

 private:
  std::shared_ptr<SqliteKeyValueSafe> kv_;
};

class BaselineKV {
 public:
  string get(string key) {
    return map_[key];
  }
  SeqNo set(string key, string value) {
    map_[key] = value;
    return ++current_tid_;
  }
  SeqNo erase(string key) {
    map_.erase(key);
    return ++current_tid_;
  }

 private:
  std::map<string, string> map_;
  SeqNo current_tid_ = 0;
};

TEST(DB, key_value) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  std::vector<std::string> keys;
  std::vector<std::string> values;

  for (int i = 0; i < 100; i++) {
    keys.push_back(rand_string('a', 'b', Random::fast(1, 10)));
  }
  for (int i = 0; i < 1000; i++) {
    values.push_back(rand_string('a', 'b', Random::fast(1, 10)));
  }

  int queries_n = 300000;
  std::vector<DbQuery> queries(queries_n);
  for (auto &q : queries) {
    int op = Random::fast(0, 2);
    const auto &key = rand_elem(keys);
    const auto &value = rand_elem(values);
    if (op == 0) {
      q.type = DbQuery::Type::Get;
      q.key = key;
    } else if (op == 1) {
      q.type = DbQuery::Type::Erase;
      q.key = key;
    } else if (op == 2) {
      q.type = DbQuery::Type::Set;
      q.key = key;
      q.value = value;
    }
  }

  QueryHandler<BaselineKV> baseline;
  QueryHandler<SeqKeyValue> kv;
  QueryHandler<TsSeqKeyValue> ts_kv;
  QueryHandler<BinlogKeyValue<Binlog>> new_kv;

  CSlice new_kv_name = "test_new_kv";
  Binlog::destroy(new_kv_name).ignore();
  new_kv.impl().init(new_kv_name.str()).ensure();

  QueryHandler<SqliteKeyValue> sqlite_kv;
  CSlice name = "test_sqlite_kv";
  SqliteDb::destroy(name).ignore();
  sqlite_kv.impl().init(name.str()).ensure();

  int cnt = 0;
  for (auto &q : queries) {
    DbQuery a = q;
    DbQuery b = q;
    DbQuery c = q;
    DbQuery d = q;
    DbQuery e = q;
    baseline.do_query(a);
    kv.do_query(b);
    ts_kv.do_query(c);
    sqlite_kv.do_query(d);
    new_kv.do_query(e);
    ASSERT_EQ(a.value, b.value);
    ASSERT_EQ(a.value, c.value);
    ASSERT_EQ(a.value, d.value);
    ASSERT_EQ(a.value, e.value);
    if (cnt++ % 10000 == 0) {
      new_kv.impl().init(new_kv_name.str()).ensure();
    }
  }
}

TEST(DB, thread_key_value) {
#if !TD_THREAD_UNSUPPORTED
  std::vector<std::string> keys;
  std::vector<std::string> values;

  for (int i = 0; i < 100; i++) {
    keys.push_back(rand_string('a', 'b', Random::fast(1, 10)));
  }
  for (int i = 0; i < 1000; i++) {
    values.push_back(rand_string('a', 'b', Random::fast(1, 10)));
  }

  int threads_n = 4;
  int queries_n = 100000;

  std::vector<std::vector<DbQuery>> queries(threads_n, std::vector<DbQuery>(queries_n));
  for (auto &qs : queries) {
    for (auto &q : qs) {
      int op = Random::fast(0, 10);
      const auto &key = rand_elem(keys);
      const auto &value = rand_elem(values);
      if (op > 1) {
        q.type = DbQuery::Type::Get;
        q.key = key;
      } else if (op == 0) {
        q.type = DbQuery::Type::Erase;
        q.key = key;
      } else if (op == 1) {
        q.type = DbQuery::Type::Set;
        q.key = key;
        q.value = value;
      }
    }
  }

  QueryHandler<BaselineKV> baseline;
  QueryHandler<TsSeqKeyValue> ts_kv;

  std::vector<thread> threads(threads_n);
  std::vector<std::vector<DbQuery>> res(threads_n);
  for (int i = 0; i < threads_n; i++) {
    threads[i] = thread([&ts_kv, &queries, &res, i] {
      for (auto q : queries[i]) {
        ts_kv.do_query(q);
        res[i].push_back(q);
      }
    });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  std::vector<std::size_t> pos(threads_n);
  while (true) {
    bool was = false;
    for (int i = 0; i < threads_n; i++) {
      auto p = pos[i];
      if (p == res[i].size()) {
        continue;
      }
      auto &q = res[i][p];
      if (q.tid == 0) {
        if (q.type == DbQuery::Type::Get) {
          auto nq = q;
          baseline.do_query(nq);
          if (nq.value == q.value) {
            was = true;
            pos[i]++;
          }
        } else {
          was = true;
          pos[i]++;
        }
      }
    }
    if (was) {
      continue;
    }

    int best = -1;
    SeqNo best_tid = 0;
    for (int i = 0; i < threads_n; i++) {
      auto p = pos[i];
      if (p == res[i].size()) {
        continue;
      }
      was = true;
      auto &q = res[i][p];
      if (q.tid != 0) {
        if (best == -1 || q.tid < best_tid) {
          best = i;
          best_tid = q.tid;
        }
      }
    }
    if (!was) {
      break;
    }
    ASSERT_TRUE(best != -1);
    baseline.do_query(res[best][pos[best]]);
    pos[best]++;
  }
#endif
}

TEST(DB, persistent_key_value) {
  using KeyValue = BinlogKeyValue<ConcurrentBinlog>;
  // using KeyValue = PersistentKeyValue;
  // using KeyValue = SqliteKV;
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  std::vector<std::string> keys;
  std::vector<std::string> values;
  CSlice name = "test_pmc";
  Binlog::destroy(name).ignore();
  SqliteDb::destroy(name).ignore();

  for (int i = 0; i < 100; i++) {
    keys.push_back(rand_string('a', 'b', Random::fast(1, 10)));
  }
  for (int i = 0; i < 1000; i++) {
    values.push_back(rand_string('a', 'b', Random::fast(1, 10)));
  }

  QueryHandler<BaselineKV> baseline;

  for (int iter = 0; iter < 25; iter++) {
    int threads_n = 4;
    int queries_n = 3000 / threads_n;

    std::vector<std::vector<DbQuery>> queries(threads_n, std::vector<DbQuery>(queries_n));
    for (auto &qs : queries) {
      for (auto &q : qs) {
        int op = Random::fast(0, 10);
        const auto &key = rand_elem(keys);
        const auto &value = rand_elem(values);
        if (op > 1) {
          q.type = DbQuery::Type::Get;
          q.key = key;
        } else if (op == 0) {
          q.type = DbQuery::Type::Erase;
          q.key = key;
        } else if (op == 1) {
          q.type = DbQuery::Type::Set;
          q.key = key;
          q.value = value;
        }
      }
    }

    std::vector<std::vector<DbQuery>> res(threads_n);
    class Worker : public Actor {
     public:
      Worker(ActorShared<> parent, std::shared_ptr<QueryHandler<KeyValue>> kv, const std::vector<DbQuery> *queries,
             std::vector<DbQuery> *res)
          : parent_(std::move(parent)), kv_(std::move(kv)), queries_(queries), res_(res) {
      }
      void loop() override {
        for (auto q : *queries_) {
          kv_->do_query(q);
          res_->push_back(q);
        }
        stop();
      }

     private:
      ActorShared<> parent_;
      std::shared_ptr<QueryHandler<KeyValue>> kv_;
      const std::vector<DbQuery> *queries_;
      std::vector<DbQuery> *res_;
    };
    class Main : public Actor {
     public:
      Main(int threads_n, const std::vector<std::vector<DbQuery>> *queries, std::vector<std::vector<DbQuery>> *res)
          : threads_n_(threads_n), queries_(queries), res_(res), ref_cnt_(threads_n) {
      }

      void start_up() override {
        LOG(INFO) << "Start up";
        kv_->impl().init("test_pmc").ensure();
        for (int i = 0; i < threads_n_; i++) {
          create_actor_on_scheduler<Worker>("Worker", i + 1, actor_shared(this, 2), kv_, &queries_->at(i), &res_->at(i))
              .release();
        }
      }

      void tear_down() override {
        LOG(INFO) << "Tear down";
        // kv_->impl().close();
      }
      void hangup_shared() override {
        LOG(INFO) << "Hang up";
        ref_cnt_--;
        if (ref_cnt_ == 0) {
          kv_->impl().close();
          Scheduler::instance()->finish();
          stop();
        }
      }
      void hangup() override {
        LOG(ERROR) << "BAD HANGUP";
      }

     private:
      int threads_n_;
      const std::vector<std::vector<DbQuery>> *queries_;
      std::vector<std::vector<DbQuery>> *res_;

      std::shared_ptr<QueryHandler<KeyValue>> kv_{new QueryHandler<KeyValue>()};
      int ref_cnt_;
    };

    ConcurrentScheduler sched;
    sched.init(threads_n);
    sched.create_actor_unsafe<Main>(0, "Main", threads_n, &queries, &res).release();
    sched.start();
    while (sched.run_main(10)) {
      // empty
    }
    sched.finish();

    std::vector<std::size_t> pos(threads_n);
    while (true) {
      bool was = false;
      for (int i = 0; i < threads_n; i++) {
        auto p = pos[i];
        if (p == res[i].size()) {
          continue;
        }
        auto &q = res[i][p];
        if (q.tid == 0) {
          if (q.type == DbQuery::Type::Get) {
            auto nq = q;
            baseline.do_query(nq);
            if (nq.value == q.value) {
              was = true;
              pos[i]++;
            }
          } else {
            was = true;
            pos[i]++;
          }
        }
      }
      if (was) {
        continue;
      }

      int best = -1;
      SeqNo best_tid = 0;
      for (int i = 0; i < threads_n; i++) {
        auto p = pos[i];
        if (p == res[i].size()) {
          continue;
        }
        was = true;
        auto &q = res[i][p];
        if (q.tid != 0) {
          if (best == -1 || q.tid < best_tid) {
            best = i;
            best_tid = q.tid;
          }
        }
      }
      if (!was) {
        break;
      }
      ASSERT_TRUE(best != -1);
      baseline.do_query(res[best][pos[best]]);
      pos[best]++;
    }
  }
}
