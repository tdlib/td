//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/tests.h"

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SleepActor.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/Observer.h"
#include "td/utils/port/detail/PollableFd.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/thread.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#include <memory>
#include <tuple>

REGISTER_TESTS(actors_simple)

namespace {
using namespace td;

static const size_t BUF_SIZE = 1024 * 1024;
static char buf[BUF_SIZE];
static char buf2[BUF_SIZE];
static StringBuilder sb(MutableSlice(buf, BUF_SIZE - 1));
static StringBuilder sb2(MutableSlice(buf2, BUF_SIZE - 1));

static auto create_queue() {
  auto res = std::make_shared<MpscPollableQueue<EventFull>>();
  res->init();
  return res;
}

TEST(Actors, SendLater) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  sb.clear();
  Scheduler scheduler;
  scheduler.init(0, {create_queue()}, nullptr);

  auto guard = scheduler.get_guard();
  class Worker : public Actor {
   public:
    void f() {
      sb << "A";
    }
  };
  auto id = create_actor<Worker>("Worker");
  scheduler.run_no_guard(Timestamp::now());
  send_closure(id, &Worker::f);
  send_closure_later(id, &Worker::f);
  send_closure(id, &Worker::f);
  ASSERT_STREQ("A", sb.as_cslice().c_str());
  scheduler.run_no_guard(Timestamp::now());
  ASSERT_STREQ("AAA", sb.as_cslice().c_str());
}

class X {
 public:
  X() {
    sb << "[cnstr_default]";
  }
  X(const X &) {
    sb << "[cnstr_copy]";
  }
  X(X &&) {
    sb << "[cnstr_move]";
  }
  X &operator=(const X &) {
    sb << "[set_copy]";
    return *this;
  }
  X &operator=(X &&) {
    sb << "[set_move]";
    return *this;
  }
  ~X() = default;
};

class XReceiver final : public Actor {
 public:
  void by_const_ref(const X &) {
    sb << "[by_const_ref]";
  }
  void by_lvalue_ref(const X &) {
    sb << "[by_lvalue_ref]";
  }
  void by_value(X) {
    sb << "[by_value]";
  }
};

TEST(Actors, simple_pass_event_arguments) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  Scheduler scheduler;
  scheduler.init(0, {create_queue()}, nullptr);

  auto guard = scheduler.get_guard();
  auto id = create_actor<XReceiver>("XR").release();
  scheduler.run_no_guard(Timestamp::now());

  X x;

  // check tuple
  // std::tuple<X> tx;
  // sb.clear();
  // std::tuple<X> ty(std::move(tx));
  // tx = std::move(ty);
  // ASSERT_STREQ("[cnstr_move]", sb.as_cslice().c_str());

  // Send temporary object

  // Tmp-->ConstRef
  sb.clear();
  send_closure(id, &XReceiver::by_const_ref, X());
  ASSERT_STREQ("[cnstr_default][by_const_ref]", sb.as_cslice().c_str());

  // Tmp-->ConstRef (Delayed)
  sb.clear();
  send_closure_later(id, &XReceiver::by_const_ref, X());
  scheduler.run_no_guard(Timestamp::now());
  // LOG(ERROR) << sb.as_cslice();
  ASSERT_STREQ("[cnstr_default][cnstr_move][by_const_ref]", sb.as_cslice().c_str());

  // Tmp-->LvalueRef
  sb.clear();
  send_closure(id, &XReceiver::by_lvalue_ref, X());
  ASSERT_STREQ("[cnstr_default][by_lvalue_ref]", sb.as_cslice().c_str());

  // Tmp-->LvalueRef (Delayed)
  sb.clear();
  send_closure_later(id, &XReceiver::by_lvalue_ref, X());
  scheduler.run_no_guard(Timestamp::now());
  ASSERT_STREQ("[cnstr_default][cnstr_move][by_lvalue_ref]", sb.as_cslice().c_str());

  // Tmp-->Value
  sb.clear();
  send_closure(id, &XReceiver::by_value, X());
  ASSERT_STREQ("[cnstr_default][cnstr_move][by_value]", sb.as_cslice().c_str());

  // Tmp-->Value (Delayed)
  sb.clear();
  send_closure_later(id, &XReceiver::by_value, X());
  scheduler.run_no_guard(Timestamp::now());
  ASSERT_STREQ("[cnstr_default][cnstr_move][cnstr_move][by_value]", sb.as_cslice().c_str());

  // Var-->ConstRef
  sb.clear();
  send_closure(id, &XReceiver::by_const_ref, x);
  ASSERT_STREQ("[by_const_ref]", sb.as_cslice().c_str());

  // Var-->ConstRef (Delayed)
  sb.clear();
  send_closure_later(id, &XReceiver::by_const_ref, x);
  scheduler.run_no_guard(Timestamp::now());
  ASSERT_STREQ("[cnstr_copy][by_const_ref]", sb.as_cslice().c_str());

  // Var-->LvalueRef
  // Var-->LvalueRef (Delayed)
  // CE or stange behaviour

  // Var-->Value
  sb.clear();
  send_closure(id, &XReceiver::by_value, x);
  ASSERT_STREQ("[cnstr_copy][by_value]", sb.as_cslice().c_str());

  // Var-->Value (Delayed)
  sb.clear();
  send_closure_later(id, &XReceiver::by_value, x);
  scheduler.run_no_guard(Timestamp::now());
  ASSERT_STREQ("[cnstr_copy][cnstr_move][by_value]", sb.as_cslice().c_str());
}

class PrintChar final : public Actor {
 public:
  PrintChar(char c, int cnt) : char_(c), cnt_(cnt) {
  }
  void start_up() override {
    yield();
  }
  void wakeup() override {
    if (cnt_ == 0) {
      stop();
    } else {
      sb << char_;
      cnt_--;
      yield();
    }
  }

 private:
  char char_;
  int cnt_;
};
}  // namespace

//
// Yield must add actor to the end of queue
//
TEST(Actors, simple_hand_yield) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  Scheduler scheduler;
  scheduler.init(0, {create_queue()}, nullptr);
  sb.clear();
  int cnt = 1000;
  {
    auto guard = scheduler.get_guard();
    create_actor<PrintChar>("PrintA", 'A', cnt).release();
    create_actor<PrintChar>("PrintB", 'B', cnt).release();
    create_actor<PrintChar>("PrintC", 'C', cnt).release();
  }
  scheduler.run(Timestamp::now());
  std::string expected;
  for (int i = 0; i < cnt; i++) {
    expected += "ABC";
  }
  ASSERT_STREQ(expected.c_str(), sb.as_cslice().c_str());
}

class Ball {
 public:
  friend void start_migrate(Ball &ball, int32 sched_id) {
    sb << "start";
  }
  friend void finish_migrate(Ball &ball) {
    sb2 << "finish";
  }
};

class Pong final : public Actor {
 public:
  void pong(Ball ball) {
    Scheduler::instance()->finish();
  }
};

class Ping final : public Actor {
 public:
  explicit Ping(ActorId<Pong> pong) : pong_(pong) {
  }
  void start_up() override {
    send_closure(pong_, &Pong::pong, Ball());
  }

 private:
  ActorId<Pong> pong_;
};

TEST(Actors, simple_migrate) {
  sb.clear();
  sb2.clear();

  ConcurrentScheduler scheduler;
  scheduler.init(2);
  auto pong = scheduler.create_actor_unsafe<Pong>(2, "Pong").release();
  scheduler.create_actor_unsafe<Ping>(1, "Ping", pong).release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
  ASSERT_STREQ("", sb.as_cslice().c_str());
  ASSERT_STREQ("", sb2.as_cslice().c_str());
#else
  ASSERT_STREQ("start", sb.as_cslice().c_str());
  ASSERT_STREQ("finish", sb2.as_cslice().c_str());
#endif
}

class OpenClose final : public Actor {
 public:
  explicit OpenClose(int cnt) : cnt_(cnt) {
  }
  void start_up() override {
    yield();
  }
  void wakeup() override {
    ObserverBase *observer = reinterpret_cast<ObserverBase *>(123);
    if (cnt_ > 0) {
      auto r_file_fd = FileFd::open("server", FileFd::Read | FileFd::Create);
      LOG_CHECK(r_file_fd.is_ok()) << r_file_fd.error();
      auto file_fd = r_file_fd.move_as_ok();
      { PollableFd pollable_fd = file_fd.get_poll_info().extract_pollable_fd(observer); }
      file_fd.close();
      cnt_--;
      yield();
    } else {
      Scheduler::instance()->finish();
    }
  }

 private:
  int cnt_;
};

TEST(Actors, open_close) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler scheduler;
  scheduler.init(2);
  int cnt = 1000000;
#if TD_WINDOWS || TD_ANDROID
  // TODO(perf) optimize
  cnt = 100;
#endif
  scheduler.create_actor_unsafe<OpenClose>(1, "A", cnt).release();
  scheduler.create_actor_unsafe<OpenClose>(2, "B", cnt).release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
}

namespace {
class MsgActor : public Actor {
 public:
  virtual void msg() = 0;
};

class Slave : public Actor {
 public:
  ActorId<MsgActor> msg;
  explicit Slave(ActorId<MsgActor> msg) : msg(msg) {
  }
  void hangup() override {
    send_closure(msg, &MsgActor::msg);
  }
};

class MasterActor : public MsgActor {
 public:
  void loop() override {
    alive_ = true;
    slave = create_actor<Slave>("slave", static_cast<ActorId<MsgActor>>(actor_id(this)));
    stop();
  }
  ActorOwn<Slave> slave;

  MasterActor() = default;
  MasterActor(const MasterActor &) = delete;
  MasterActor &operator=(const MasterActor &) = delete;
  MasterActor(MasterActor &&) = delete;
  MasterActor &operator=(MasterActor &&) = delete;
  ~MasterActor() override {
    alive_ = 987654321;
  }
  void msg() override {
    CHECK(alive_ == 123456789);
  }
  uint64 alive_ = 123456789;
};
}  // namespace

TEST(Actors, call_after_destruct) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  Scheduler scheduler;
  scheduler.init(0, {create_queue()}, nullptr);
  {
    auto guard = scheduler.get_guard();
    create_actor<MasterActor>("Master").release();
  }
  scheduler.run(Timestamp::now());
}

class LinkTokenSlave : public Actor {
 public:
  explicit LinkTokenSlave(ActorShared<> parent) : parent_(std::move(parent)) {
  }
  void add(uint64 link_token) {
    CHECK(link_token == get_link_token());
  }
  void close() {
    stop();
  }

 private:
  ActorShared<> parent_;
};

class LinkTokenMasterActor : public Actor {
 public:
  explicit LinkTokenMasterActor(int cnt) : cnt_(cnt) {
  }
  void start_up() override {
    child_ = create_actor<LinkTokenSlave>("Slave", actor_shared(this, 123)).release();
    yield();
  }
  void loop() override {
    for (int i = 0; i < 100 && cnt_ > 0; cnt_--, i++) {
      auto token = static_cast<uint64>(cnt_) + 1;
      switch (i % 4) {
        case 0: {
          send_closure(ActorShared<LinkTokenSlave>(child_, token), &LinkTokenSlave::add, token);
          break;
        }
        case 1: {
          send_closure_later(ActorShared<LinkTokenSlave>(child_, token), &LinkTokenSlave::add, token);
          break;
        }
        case 2: {
          EventCreator::closure(ActorShared<LinkTokenSlave>(child_, token), &LinkTokenSlave::add, token).try_emit();
          break;
        }
        case 3: {
          EventCreator::closure(ActorShared<LinkTokenSlave>(child_, token), &LinkTokenSlave::add, token)
              .try_emit_later();
          break;
        }
      }
    }
    if (cnt_ == 0) {
      send_closure(child_, &LinkTokenSlave::close);
    } else {
      yield();
    }
  }

  void hangup_shared() override {
    CHECK(get_link_token() == 123);
    Scheduler::instance()->finish();
    stop();
  }

 private:
  int cnt_;
  ActorId<LinkTokenSlave> child_;
};

TEST(Actors, link_token) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler scheduler;
  scheduler.init(0);
  auto cnt = 100000;
  scheduler.create_actor_unsafe<LinkTokenMasterActor>(0, "A", cnt).release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
}

TEST(Actors, promise) {
  int value = -1;
  Promise<int> p1 = PromiseCreator::lambda([&](int x) { value = x; });
  p1.set_error(Status::Error("Test error"));
  ASSERT_EQ(0, value);
  Promise<int32> p2 = PromiseCreator::lambda([&](Result<int32> x) { value = 1; });
  p2.set_error(Status::Error("Test error"));
  ASSERT_EQ(1, value);
}

class LaterSlave : public Actor {
 public:
  explicit LaterSlave(ActorShared<> parent) : parent_(std::move(parent)) {
  }

 private:
  ActorShared<> parent_;

  void hangup() override {
    sb << "A";
    send_closure(actor_id(this), &LaterSlave::finish);
  }
  void finish() {
    sb << "B";
    stop();
  }
};

class LaterMasterActor : public Actor {
  int cnt_ = 3;
  std::vector<ActorOwn<LaterSlave>> children_;
  void start_up() override {
    for (int i = 0; i < cnt_; i++) {
      children_.push_back(create_actor<LaterSlave>("B", actor_shared()));
    }
    yield();
  }
  void loop() override {
    children_.clear();
  }
  void hangup_shared() override {
    if (!--cnt_) {
      Scheduler::instance()->finish();
      stop();
    }
  }
};

TEST(Actors, later) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  sb.clear();
  ConcurrentScheduler scheduler;
  scheduler.init(0);
  scheduler.create_actor_unsafe<LaterMasterActor>(0, "A").release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
  ASSERT_STREQ(sb.as_cslice().c_str(), "AAABBB");
}

class MultiPromise2 : public Actor {
 public:
  void start_up() override {
    auto promise = PromiseCreator::lambda([](Result<Unit> result) {
      result.ensure();
      Scheduler::instance()->finish();
    });

    MultiPromiseActorSafe multi_promise{"MultiPromiseActor2"};
    multi_promise.add_promise(std::move(promise));
    for (int i = 0; i < 10; i++) {
      create_actor<SleepActor>("Sleep", 0.1, multi_promise.get_promise()).release();
    }
  }
};

class MultiPromise1 : public Actor {
 public:
  void start_up() override {
    auto promise = PromiseCreator::lambda([](Result<Unit> result) {
      CHECK(result.is_error());
      create_actor<MultiPromise2>("B").release();
    });
    MultiPromiseActorSafe multi_promise{"MultiPromiseActor1"};
    multi_promise.add_promise(std::move(promise));
  }
};

TEST(Actors, MultiPromise) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  sb.clear();
  ConcurrentScheduler scheduler;
  scheduler.init(0);
  scheduler.create_actor_unsafe<MultiPromise1>(0, "A").release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
}

class FastPromise : public Actor {
 public:
  void start_up() override {
    PromiseFuture<int> pf;
    auto promise = pf.move_promise();
    auto future = pf.move_future();
    promise.set_value(123);
    CHECK(future.move_as_ok() == 123);
    Scheduler::instance()->finish();
  }
};

TEST(Actors, FastPromise) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  sb.clear();
  ConcurrentScheduler scheduler;
  scheduler.init(0);
  scheduler.create_actor_unsafe<FastPromise>(0, "A").release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
}

class StopInTeardown : public Actor {
  void loop() override {
    stop();
  }
  void tear_down() override {
    stop();
    Scheduler::instance()->finish();
  }
};

TEST(Actors, stop_in_teardown) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  sb.clear();
  ConcurrentScheduler scheduler;
  scheduler.init(0);
  scheduler.create_actor_unsafe<StopInTeardown>(0, "A").release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
}

class AlwaysWaitForMailbox : public Actor {
 public:
  void start_up() override {
    always_wait_for_mailbox();
    create_actor<SleepActor>("Sleep", 0.1, PromiseCreator::lambda([actor_id = actor_id(this), ptr = this](Unit) {
                               send_closure(actor_id, &AlwaysWaitForMailbox::g);
                               send_closure(actor_id, &AlwaysWaitForMailbox::g);
                               CHECK(!ptr->was_f_);
                             }))
        .release();
  }

  void f() {
    was_f_ = true;
    Scheduler::instance()->finish();
  }
  void g() {
    send_closure(actor_id(this), &AlwaysWaitForMailbox::f);
  }

 private:
  Timeout timeout_;
  bool was_f_{false};
};

TEST(Actors, always_wait_for_mailbox) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler scheduler;
  scheduler.init(0);
  scheduler.create_actor_unsafe<AlwaysWaitForMailbox>(0, "A").release();
  scheduler.start();
  while (scheduler.run_main(10)) {
  }
  scheduler.finish();
}

#if !TD_THREAD_UNSUPPORTED && !TD_EVENTFD_UNSUPPORTED
TEST(Actors, send_from_other_threads) {
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(ERROR));
  ConcurrentScheduler scheduler;
  scheduler.init(1);
  int thread_n = 10;
  class Listener : public Actor {
   public:
    explicit Listener(int cnt) : cnt_(cnt) {
    }
    void dec() {
      if (--cnt_ == 0) {
        Scheduler::instance()->finish();
      }
    }

   private:
    int cnt_;
  };

  auto A = scheduler.create_actor_unsafe<Listener>(1, "A", thread_n).release();
  scheduler.start();
  std::vector<td::thread> threads(thread_n);
  for (auto &thread : threads) {
    thread = td::thread([&A, &scheduler] {
      auto guard = scheduler.get_send_guard();
      send_closure(A, &Listener::dec);
    });
  }
  while (scheduler.run_main(10)) {
  }
  for (auto &thread : threads) {
    thread.join();
  }
  scheduler.finish();
}
#endif
