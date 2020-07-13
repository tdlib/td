//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"

#include <limits>
#include <map>
#include <memory>
#include <utility>

using namespace td;

REGISTER_TESTS(actors_main);

namespace {

template <class ContainerT>
static typename ContainerT::value_type &rand_elem(ContainerT &cont) {
  CHECK(0 < cont.size() && cont.size() <= static_cast<size_t>(std::numeric_limits<int>::max()));
  return cont[Random::fast(0, static_cast<int>(cont.size()) - 1)];
}

static uint32 fast_pow_mod_uint32(uint32 x, uint32 p) {
  uint32 res = 1;
  while (p) {
    if (p & 1) {
      res *= x;
    }
    x *= x;
    p >>= 1;
  }
  return res;
}

static uint32 slow_pow_mod_uint32(uint32 x, uint32 p) {
  uint32 res = 1;
  for (uint32 i = 0; i < p; i++) {
    res *= x;
  }
  return res;
}

struct Query {
  uint32 query_id{};
  uint32 result{};
  std::vector<int> todo;
  Query() = default;
  Query(const Query &) = delete;
  Query &operator=(const Query &) = delete;
  Query(Query &&) = default;
  Query &operator=(Query &&) = default;
  ~Query() {
    LOG_CHECK(todo.empty()) << "Query lost";
  }
  int next_pow() {
    CHECK(!todo.empty());
    int res = todo.back();
    todo.pop_back();
    return res;
  }
  bool ready() {
    return todo.empty();
  }
};

static uint32 fast_calc(Query &q) {
  uint32 result = q.result;
  for (auto x : q.todo) {
    result = fast_pow_mod_uint32(result, x);
  }
  return result;
}

class Worker final : public Actor {
 public:
  explicit Worker(int threads_n) : threads_n_(threads_n) {
  }
  void query(PromiseActor<uint32> &&promise, uint32 x, uint32 p) {
    uint32 result = slow_pow_mod_uint32(x, p);
    promise.set_value(std::move(result));

    (void)threads_n_;
    // if (threads_n_ > 1 && Random::fast(0, 9) == 0) {
    // migrate(Random::fast(2, threads_n));
    //}
  }

 private:
  int threads_n_;
};

class QueryActor final : public Actor {
 public:
  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    Callback(Callback &&) = delete;
    Callback &operator=(Callback &&) = delete;
    virtual ~Callback() = default;
    virtual void on_result(Query &&query) = 0;
    virtual void on_closed() = 0;
  };

  explicit QueryActor(int threads_n) : threads_n_(threads_n) {
  }

  void set_callback(unique_ptr<Callback> callback) {
    callback_ = std::move(callback);
  }
  void set_workers(std::vector<ActorId<Worker>> workers) {
    workers_ = std::move(workers);
  }

  void query(Query &&query) {
    uint32 x = query.result;
    uint32 p = query.next_pow();
    if (Random::fast(0, 3) && (p <= 1000 || workers_.empty())) {
      query.result = slow_pow_mod_uint32(x, p);
      callback_->on_result(std::move(query));
    } else {
      auto future = Random::fast(0, 3) == 0
                        ? send_promise<ActorSendType::Immediate>(rand_elem(workers_), &Worker::query, x, p)
                        : send_promise<ActorSendType::Later>(rand_elem(workers_), &Worker::query, x, p);
      if (future.is_ready()) {
        query.result = future.move_as_ok();
        callback_->on_result(std::move(query));
      } else {
        future.set_event(EventCreator::raw(actor_id(), query.query_id));
        auto query_id = query.query_id;
        pending_.emplace(query_id, std::make_pair(std::move(future), std::move(query)));
      }
    }
    if (threads_n_ > 1 && Random::fast(0, 9) == 0) {
      migrate(Random::fast(2, threads_n_));
    }
  }

  void raw_event(const Event::Raw &event) override {
    uint32 id = event.u32;
    auto it = pending_.find(id);
    auto future = std::move(it->second.first);
    auto query = std::move(it->second.second);
    pending_.erase(it);
    CHECK(future.is_ready());
    query.result = future.move_as_ok();
    callback_->on_result(std::move(query));
  }

  void close() {
    callback_->on_closed();
    stop();
  }

  void on_start_migrate(int32 sched_id) override {
    for (auto &it : pending_) {
      start_migrate(it.second.first, sched_id);
    }
  }
  void on_finish_migrate() override {
    for (auto &it : pending_) {
      finish_migrate(it.second.first);
    }
  }

 private:
  unique_ptr<Callback> callback_;
  std::map<uint32, std::pair<FutureActor<uint32>, Query>> pending_;
  std::vector<ActorId<Worker>> workers_;
  int threads_n_;
};

class MainQueryActor final : public Actor {
  class QueryActorCallback : public QueryActor::Callback {
   public:
    void on_result(Query &&query) override {
      if (query.ready()) {
        send_closure(parent_id_, &MainQueryActor::on_result, std::move(query));
      } else {
        send_closure(next_solver_, &QueryActor::query, std::move(query));
      }
    }
    void on_closed() override {
      send_closure(parent_id_, &MainQueryActor::on_closed);
    }
    QueryActorCallback(ActorId<MainQueryActor> parent_id, ActorId<QueryActor> next_solver)
        : parent_id_(parent_id), next_solver_(next_solver) {
    }

   private:
    ActorId<MainQueryActor> parent_id_;
    ActorId<QueryActor> next_solver_;
  };

  const int ACTORS_CNT = 10;
  const int WORKERS_CNT = 4;

 public:
  explicit MainQueryActor(int threads_n) : threads_n_(threads_n) {
  }

  void start_up() override {
    actors_.resize(ACTORS_CNT);
    for (auto &actor : actors_) {
      auto actor_ptr = make_unique<QueryActor>(threads_n_);
      actor = register_actor("QueryActor", std::move(actor_ptr), threads_n_ > 1 ? Random::fast(2, threads_n_) : 0)
                  .release();
    }

    workers_.resize(WORKERS_CNT);
    for (auto &worker : workers_) {
      auto actor_ptr = make_unique<Worker>(threads_n_);
      worker =
          register_actor("Worker", std::move(actor_ptr), threads_n_ > 1 ? Random::fast(2, threads_n_) : 0).release();
    }

    for (int i = 0; i < ACTORS_CNT; i++) {
      ref_cnt_++;
      send_closure(actors_[i], &QueryActor::set_callback,
                   make_unique<QueryActorCallback>(actor_id(this), actors_[(i + 1) % ACTORS_CNT]));
      send_closure(actors_[i], &QueryActor::set_workers, workers_);
    }
    yield();
  }

  void on_result(Query &&query) {
    CHECK(query.ready());
    CHECK(query.result == expected_[query.query_id]);
    in_cnt_++;
    wakeup();
  }

  Query create_query() {
    Query q;
    q.query_id = (query_id_ += 2);
    q.result = q.query_id;
    q.todo = {1, 1, 1, 1, 1, 1, 1, 1, 10000};
    expected_[q.query_id] = fast_calc(q);
    return q;
  }

  void on_closed() {
    ref_cnt_--;
    if (ref_cnt_ == 0) {
      Scheduler::instance()->finish();
    }
  }

  void wakeup() override {
    int cnt = 100000;
    while (out_cnt_ < in_cnt_ + 100 && out_cnt_ < cnt) {
      if (Random::fast(0, 1)) {
        send_closure(rand_elem(actors_), &QueryActor::query, create_query());
      } else {
        send_closure_later(rand_elem(actors_), &QueryActor::query, create_query());
      }
      out_cnt_++;
    }
    if (in_cnt_ == cnt) {
      in_cnt_++;
      ref_cnt_--;
      for (auto &actor : actors_) {
        send_closure(actor, &QueryActor::close);
      }
    }
  }

 private:
  std::map<uint32, uint32> expected_;
  std::vector<ActorId<QueryActor>> actors_;
  std::vector<ActorId<Worker>> workers_;
  int out_cnt_ = 0;
  int in_cnt_ = 0;
  int query_id_ = 1;
  int ref_cnt_ = 1;
  int threads_n_;
};

class SimpleActor final : public Actor {
 public:
  explicit SimpleActor(int32 threads_n) : threads_n_(threads_n) {
  }
  void start_up() override {
    auto actor_ptr = make_unique<Worker>(threads_n_);
    worker_ =
        register_actor("Worker", std::move(actor_ptr), threads_n_ > 1 ? Random::fast(2, threads_n_) : 0).release();
    yield();
  }

  void wakeup() override {
    if (q_ == 100000) {
      Scheduler::instance()->finish();
      stop();
      return;
    }
    q_++;
    p_ = Random::fast(0, 1) ? 1 : 10000;
    auto future = Random::fast(0, 3) == 0 ? send_promise<ActorSendType::Immediate>(worker_, &Worker::query, q_, p_)
                                          : send_promise<ActorSendType::Later>(worker_, &Worker::query, q_, p_);
    if (future.is_ready()) {
      auto result = future.move_as_ok();
      CHECK(result == fast_pow_mod_uint32(q_, p_));
      yield();
    } else {
      future.set_event(EventCreator::raw(actor_id(), nullptr));
      future_ = std::move(future);
    }
    // if (threads_n_ > 1 && Random::fast(0, 2) == 0) {
    // migrate(Random::fast(1, threads_n));
    //}
  }
  void raw_event(const Event::Raw &event) override {
    auto result = future_.move_as_ok();
    CHECK(result == fast_pow_mod_uint32(q_, p_));
    yield();
  }

  void on_start_migrate(int32 sched_id) override {
    start_migrate(future_, sched_id);
  }
  void on_finish_migrate() override {
    finish_migrate(future_);
  }

 private:
  int32 threads_n_;
  ActorId<Worker> worker_;
  FutureActor<uint32> future_;
  uint32 q_ = 1;
  uint32 p_ = 0;
};
}  // namespace

class SendToDead : public Actor {
 public:
  class Parent : public Actor {
   public:
    explicit Parent(ActorShared<> parent, int ttl = 3) : parent_(std::move(parent)), ttl_(ttl) {
    }
    void start_up() override {
      set_timeout_in(Random::fast_uint32() % 3 * 0.001);
      if (ttl_ != 0) {
        child_ = create_actor_on_scheduler<Parent>(
            "Child", Random::fast_uint32() % Scheduler::instance()->sched_count(), actor_shared(), ttl_ - 1);
      }
    }
    void timeout_expired() override {
      stop();
    }

   private:
    ActorOwn<Parent> child_;
    ActorShared<> parent_;
    int ttl_;
  };

  void start_up() override {
    for (int i = 0; i < 2000; i++) {
      create_actor_on_scheduler<Parent>("Parent", Random::fast_uint32() % Scheduler::instance()->sched_count(),
                                        create_reference(), 4)
          .release();
    }
  }

  ActorShared<> create_reference() {
    ref_cnt_++;
    return actor_shared();
  }
  void hangup_shared() override {
    ref_cnt_--;
    if (ref_cnt_ == 0) {
      ttl_--;
      if (ttl_ <= 0) {
        Scheduler::instance()->finish();
        stop();
      } else {
        start_up();
      }
    }
  }

  uint32 ttl_{50};
  uint32 ref_cnt_{0};
};

TEST(Actors, send_to_dead) {
  //TODO: fix CHECK(storage_count_.load() == 0)
  return;
  ConcurrentScheduler sched;
  int threads_n = 5;
  sched.init(threads_n);

  sched.create_actor_unsafe<SendToDead>(0, "SendToDead").release();
  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}

TEST(Actors, main_simple) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));

  ConcurrentScheduler sched;
  int threads_n = 3;
  sched.init(threads_n);

  sched.create_actor_unsafe<SimpleActor>(threads_n > 1 ? 1 : 0, "simple", threads_n).release();
  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}

TEST(Actors, main) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));

  ConcurrentScheduler sched;
  int threads_n = 9;
  sched.init(threads_n);

  sched.create_actor_unsafe<MainQueryActor>(threads_n > 1 ? 1 : 0, "MainQuery", threads_n).release();
  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}

class DoAfterStop : public Actor {
 public:
  void loop() override {
    ptr = make_unique<int>(10);
    stop();
    CHECK(*ptr == 10);
    Scheduler::instance()->finish();
  }

 private:
  unique_ptr<int> ptr;
};

TEST(Actors, do_after_stop) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));

  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  sched.create_actor_unsafe<DoAfterStop>(0, "DoAfterStop").release();
  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}

class XContext : public ActorContext {
 public:
  int32 get_id() const override {
    return 123456789;
  }

  void validate() {
    CHECK(x == 1234);
  }
  ~XContext() {
    x = 0;
  }
  int x = 1234;
};

class WithXContext : public Actor {
 public:
  void start_up() override {
    auto old_context = set_context(std::make_shared<XContext>());
  }
  void f(unique_ptr<Guard> guard) {
  }
  void close() {
    stop();
  }
};

static void check_context() {
  auto ptr = static_cast<XContext *>(Scheduler::context());
  CHECK(ptr);
  ptr->validate();
}

TEST(Actors, context_during_destruction) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));

  ConcurrentScheduler sched;
  int threads_n = 0;
  sched.init(threads_n);

  {
    auto guard = sched.get_main_guard();
    auto with_context = create_actor<WithXContext>("WithXContext").release();
    send_closure(with_context, &WithXContext::f, create_lambda_guard([] { check_context(); }));
    send_closure_later(with_context, &WithXContext::close);
    send_closure(with_context, &WithXContext::f, create_lambda_guard([] { check_context(); }));
    send_closure(with_context, &WithXContext::f, create_lambda_guard([] { Scheduler::instance()->finish(); }));
  }
  sched.start();
  while (sched.run_main(10)) {
    // empty
  }
  sched.finish();
}
