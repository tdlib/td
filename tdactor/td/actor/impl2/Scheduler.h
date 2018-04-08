//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/impl2/ActorLocker.h"
#include "td/actor/impl2/SchedulerId.h"

#include "td/utils/Closure.h"
#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/Heap.h"
#include "td/utils/List.h"
#include "td/utils/logging.h"
#include "td/utils/MpmcQueue.h"
#include "td/utils/MpmcWaiter.h"
#include "td/utils/MpscLinkQueue.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/Poll.h"
#include "td/utils/port/thread.h"
#include "td/utils/port/thread_local.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/SharedObjectPool.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"
#include "td/utils/type_traits.h"

#include <atomic>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace td {
namespace actor2 {
class Actor;

template <class Impl>
class Context {
 public:
  static Impl *get() {
    return context_;
  }
  class Guard {
   public:
    explicit Guard(Impl *new_context) {
      old_context_ = context_;
      context_ = new_context;
    }
    ~Guard() {
      context_ = old_context_;
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
    Guard(Guard &&) = delete;
    Guard &operator=(Guard &&) = delete;

   private:
    Impl *old_context_;
  };

 private:
  static TD_THREAD_LOCAL Impl *context_;
};

template <class Impl>
TD_THREAD_LOCAL Impl *Context<Impl>::context_;

enum : uint64 { EmptyLinkToken = std::numeric_limits<uint64>::max() };

class ActorExecuteContext : public Context<ActorExecuteContext> {
 public:
  explicit ActorExecuteContext(Actor *actor, Timestamp alarm_timestamp = Timestamp::never())
      : actor_(actor), alarm_timestamp_(alarm_timestamp) {
  }
  Actor &actor() const {
    CHECK(actor_);
    return *actor_;
  }
  bool has_flags() const {
    return flags_ != 0;
  }
  void set_stop() {
    flags_ |= 1 << Stop;
  }
  bool get_stop() const {
    return (flags_ & (1 << Stop)) != 0;
  }
  void set_pause() {
    flags_ |= 1 << Pause;
  }
  bool get_pause() const {
    return (flags_ & (1 << Pause)) != 0;
  }
  void clear_actor() {
    actor_ = nullptr;
  }
  void set_link_token(uint64 link_token) {
    link_token_ = link_token;
  }
  uint64 get_link_token() const {
    return link_token_;
  }
  Timestamp &alarm_timestamp() {
    flags_ |= 1 << Alarm;
    return alarm_timestamp_;
  }
  bool get_alarm_flag() const {
    return (flags_ & (1 << Alarm)) != 0;
  }
  Timestamp get_alarm_timestamp() const {
    return alarm_timestamp_;
  }

 private:
  Actor *actor_;
  uint32 flags_{0};
  uint64 link_token_{EmptyLinkToken};
  Timestamp alarm_timestamp_;
  enum { Stop, Pause, Alarm };
};

class ActorMessageImpl : private MpscLinkQueueImpl::Node {
 public:
  ActorMessageImpl() = default;
  ActorMessageImpl(const ActorMessageImpl &) = delete;
  ActorMessageImpl &operator=(const ActorMessageImpl &) = delete;
  ActorMessageImpl(ActorMessageImpl &&other) = delete;
  ActorMessageImpl &operator=(ActorMessageImpl &&other) = delete;
  virtual ~ActorMessageImpl() = default;
  virtual void run() = 0;
  //virtual void run_anonymous() = 0;

  // ActorMessage <--> MpscLintQueue::Node
  // Each actor's mailbox will be a queue
  static ActorMessageImpl *from_mpsc_link_queue_node(MpscLinkQueueImpl::Node *node) {
    return static_cast<ActorMessageImpl *>(node);
  }
  MpscLinkQueueImpl::Node *to_mpsc_link_queue_node() {
    return static_cast<MpscLinkQueueImpl::Node *>(this);
  }

  uint64 link_token_{EmptyLinkToken};
  bool is_big_{false};
};

class ActorMessage {
 public:
  ActorMessage() = default;
  explicit ActorMessage(std::unique_ptr<ActorMessageImpl> impl) : impl_(std::move(impl)) {
  }
  void run() {
    CHECK(impl_);
    impl_->run();
  }
  explicit operator bool() {
    return bool(impl_);
  }
  friend class ActorMailbox;

  void set_link_token(uint64 link_token) {
    impl_->link_token_ = link_token;
  }
  uint64 get_link_token() const {
    return impl_->link_token_;
  }
  bool is_big() const {
    return impl_->is_big_;
  }
  void set_big() {
    impl_->is_big_ = true;
  }

 private:
  std::unique_ptr<ActorMessageImpl> impl_;

  template <class T>
  friend class td::MpscLinkQueue;

  static ActorMessage from_mpsc_link_queue_node(MpscLinkQueueImpl::Node *node) {
    return ActorMessage(std::unique_ptr<ActorMessageImpl>(ActorMessageImpl::from_mpsc_link_queue_node(node)));
  }
  MpscLinkQueueImpl::Node *to_mpsc_link_queue_node() {
    return impl_.release()->to_mpsc_link_queue_node();
  }
};

class ActorMailbox {
 public:
  ActorMailbox() = default;
  ActorMailbox(const ActorMailbox &) = delete;
  ActorMailbox &operator=(const ActorMailbox &) = delete;
  ActorMailbox(ActorMailbox &&other) = delete;
  ActorMailbox &operator=(ActorMailbox &&other) = delete;
  ~ActorMailbox() {
    pop_all();
    while (reader_.read()) {
      // skip
    }
  }
  class Reader;
  void push(ActorMessage message) {
    queue_.push(std::move(message));
  }
  void push_unsafe(ActorMessage message) {
    queue_.push_unsafe(std::move(message));
  }

  td::MpscLinkQueue<ActorMessage>::Reader &reader() {
    return reader_;
  }

  void pop_all() {
    queue_.pop_all(reader_);
  }
  void pop_all_unsafe() {
    queue_.pop_all_unsafe(reader_);
  }

 private:
  td::MpscLinkQueue<ActorMessage> queue_;
  td::MpscLinkQueue<ActorMessage>::Reader reader_;
};

class ActorInfo
    : private HeapNode
    , private ListNode {
 public:
  ActorInfo(std::unique_ptr<Actor> actor, ActorState::Flags state_flags, Slice name)
      : actor_(std::move(actor)), name_(name.begin(), name.size()) {
    state_.set_flags_unsafe(state_flags);
  }

  bool has_actor() const {
    return bool(actor_);
  }
  Actor &actor() {
    CHECK(has_actor());
    return *actor_;
  }
  Actor *actor_ptr() const {
    return actor_.get();
  }
  void destroy_actor() {
    actor_.reset();
  }
  ActorState &state() {
    return state_;
  }
  ActorMailbox &mailbox() {
    return mailbox_;
  }
  CSlice get_name() const {
    return name_;
  }

  HeapNode *as_heap_node() {
    return this;
  }
  static ActorInfo *from_heap_node(HeapNode *node) {
    return static_cast<ActorInfo *>(node);
  }

  Timestamp &alarm_timestamp() {
    return alarm_timestamp_;
  }

 private:
  std::unique_ptr<Actor> actor_;
  ActorState state_;
  ActorMailbox mailbox_;
  std::string name_;
  Timestamp alarm_timestamp_;
};

using ActorInfoPtr = SharedObjectPool<ActorInfo>::Ptr;

class Actor {
 public:
  Actor() = default;
  Actor(const Actor &) = delete;
  Actor &operator=(const Actor &) = delete;
  Actor(Actor &&other) = delete;
  Actor &operator=(Actor &&other) = delete;
  virtual ~Actor() = default;

  void set_actor_info_ptr(ActorInfoPtr actor_info_ptr) {
    actor_info_ptr_ = std::move(actor_info_ptr);
  }
  ActorInfoPtr get_actor_info_ptr() {
    return actor_info_ptr_;
  }

 protected:
  // Signal handlers
  virtual void start_up();   // StartUp signal handler
  virtual void tear_down();  // TearDown signal handler (or Kill)
  virtual void hang_up();    // HangUp signal handler
  virtual void wake_up();    // WakeUp signal handler
  virtual void alarm();      // Alarm signal handler

  friend class ActorMessageHangup;

  // Event handlers
  //virtual void hangup_shared();
  // TODO: raw event?

  virtual void loop();  // default handler

  // Useful functions
  void yield();  // send wakeup signal to itself
  void stop();   // send Kill signal to itself
  Timestamp &alarm_timestamp() {
    return ActorExecuteContext::get()->alarm_timestamp();
  }
  Timestamp get_alarm_timestamp() {
    return ActorExecuteContext::get()->get_alarm_timestamp();
  }

  CSlice get_name() {
    return actor_info_ptr_->get_name();
  }

  // Inteface to scheduler
  // Query will be just passed to current scheduler
  // Timeout functions
  //bool has_timeout() const;
  //void set_timeout_in(double timeout_in);
  //void set_timeout_at(double timeout_at);
  //void cancel_timeout();
  //uint64 get_link_token();  // get current request's link_token
  //set context that will be inherited by all childrens
  //void set_context(std::shared_ptr<ActorContext> context);

  //ActorShared<> actor_shared();  // ActorShared to itself
  //template <class SelfT>
  //ActorShared<SelfT> actor_shared(SelfT *self, uint64 id = static_cast<uint64>(-1));  // ActorShared with type

  // Create EventFull to itself
  //template <class FuncT, class... ArgsT>
  //auto self_closure(FuncT &&func, ArgsT &&... args);
  //template <class SelfT, class FuncT, class... ArgsT>
  //auto self_closure(SelfT *self, FuncT &&func, ArgsT &&... args);
  //template <class LambdaT>
  //auto self_lambda(LambdaT &&lambda);

  //void do_stop();  // process Kill signal immediately

 private:
  friend class ActorExecutor;
  ActorInfoPtr actor_info_ptr_;
};
// Signal handlers
inline void Actor::start_up() {
  yield();
}
inline void Actor::tear_down() {
  // noop
}
inline void Actor::hang_up() {
  stop();
}
inline void Actor::wake_up() {
  loop();
}
inline void Actor::alarm() {
  loop();
}

inline void Actor::loop() {
  // noop
}

// Useful functions
inline void Actor::yield() {
  // TODO
}
inline void Actor::stop() {
  ActorExecuteContext::get()->set_stop();
}

class ActorInfoCreator {
 public:
  class Options {
   public:
    Options() = default;

    Options &with_name(Slice new_name) {
      name = new_name;
      return *this;
    }

    Options &on_scheduler(SchedulerId new_scheduler_id) {
      scheduler_id = new_scheduler_id;
      return *this;
    }
    bool has_scheduler() const {
      return scheduler_id.is_valid();
    }
    Options &with_poll() {
      is_shared = false;
      return *this;
    }

   private:
    friend class ActorInfoCreator;
    Slice name;
    SchedulerId scheduler_id;
    bool is_shared{true};
    bool in_queue{true};
    //TODO: rename
  };

  //Create unlocked actor. One must send StartUp signal immediately.
  ActorInfoPtr create(std::unique_ptr<Actor> actor, const Options &args) {
    ActorState::Flags flags;
    flags.set_scheduler_id(args.scheduler_id);
    flags.set_shared(args.is_shared);
    flags.set_in_queue(args.in_queue);
    flags.set_signals(ActorSignals::one(ActorSignals::StartUp));

    auto actor_info_ptr = pool_.alloc(std::move(actor), flags, args.name);
    actor_info_ptr->actor().set_actor_info_ptr(actor_info_ptr);
    return actor_info_ptr;
  }

  ActorInfoCreator() = default;
  ActorInfoCreator(const ActorInfoCreator &) = delete;
  ActorInfoCreator &operator=(const ActorInfoCreator &) = delete;
  ActorInfoCreator(ActorInfoCreator &&other) = delete;
  ActorInfoCreator &operator=(ActorInfoCreator &&other) = delete;
  ~ActorInfoCreator() {
    pool_.for_each([](auto &actor_info) { actor_info.destroy_actor(); });
  }

 private:
  SharedObjectPool<ActorInfo> pool_;
};

using ActorOptions = ActorInfoCreator::Options;

class SchedulerDispatcher {
 public:
  virtual SchedulerId get_scheduler_id() const = 0;
  virtual void add_to_queue(ActorInfoPtr actor_info_ptr, SchedulerId scheduler_id, bool need_poll) = 0;
  virtual void set_alarm_timestamp(const ActorInfoPtr &actor_info_ptr, Timestamp timestamp) = 0;

  SchedulerDispatcher() = default;
  SchedulerDispatcher(const SchedulerDispatcher &) = delete;
  SchedulerDispatcher &operator=(const SchedulerDispatcher &) = delete;
  SchedulerDispatcher(SchedulerDispatcher &&other) = delete;
  SchedulerDispatcher &operator=(SchedulerDispatcher &&other) = delete;
  virtual ~SchedulerDispatcher() = default;
};

class ActorExecutor {
 public:
  struct Options {
    Options &with_from_queue() {
      from_queue = true;
      return *this;
    }
    Options &with_has_poll(bool new_has_poll) {
      this->has_poll = new_has_poll;
      return *this;
    }
    bool from_queue{false};
    bool has_poll{false};
  };
  ActorExecutor(ActorInfo &actor_info, SchedulerDispatcher &dispatcher, Options options)
      : actor_info_(actor_info), dispatcher_(dispatcher), options_(options) {
    //LOG(ERROR) << "START " << actor_info_.get_name() << " " << tag("from_queue", from_queue);
    start();
  }
  ActorExecutor(const ActorExecutor &) = delete;
  ActorExecutor &operator=(const ActorExecutor &) = delete;
  ActorExecutor(ActorExecutor &&other) = delete;
  ActorExecutor &operator=(ActorExecutor &&other) = delete;
  ~ActorExecutor() {
    //LOG(ERROR) << "FINISH " << actor_info_.get_name() << " " << tag("own_lock", actor_locker_.own_lock());
    finish();
  }

  // our best guess if actor is closed or not
  bool can_send() {
    return !flags().is_closed();
  }

  bool can_send_immediate() {
    return actor_locker_.own_lock() && !actor_execute_context_.has_flags() && actor_locker_.can_execute();
  }

  template <class F>
  void send_immediate(F &&f, uint64 link_token) {
    CHECK(can_send_immediate());
    if (!can_send()) {
      return;
    }
    actor_execute_context_.set_link_token(link_token);
    f();
  }
  void send_immediate(ActorMessage message) {
    CHECK(can_send_immediate());
    if (message.is_big()) {
      actor_info_.mailbox().reader().delay(std::move(message));
      pending_signals_.add_signal(ActorSignals::Message);
      actor_execute_context_.set_pause();
      return;
    }
    actor_execute_context_.set_link_token(message.get_link_token());
    message.run();
  }
  void send_immediate(ActorSignals signals) {
    CHECK(can_send_immediate());
    while (flush_one_signal(signals) && can_send_immediate()) {
    }
    pending_signals_.add_signals(signals);
  }

  void send(ActorMessage message) {
    if (!can_send()) {
      return;
    }
    if (can_send_immediate()) {
      return send_immediate(std::move(message));
    }
    actor_info_.mailbox().push(std::move(message));
    pending_signals_.add_signal(ActorSignals::Message);
  }

  void send(ActorSignals signals) {
    if (!can_send()) {
      return;
    }

    pending_signals_.add_signals(signals);
  }

 private:
  ActorInfo &actor_info_;
  SchedulerDispatcher &dispatcher_;
  Options options_;
  ActorLocker actor_locker_{
      &actor_info_.state(),
      ActorLocker::Options().with_can_execute_paused(options_.from_queue).with_is_shared(!options_.has_poll)};

  ActorExecuteContext actor_execute_context_{actor_info_.actor_ptr(), actor_info_.alarm_timestamp()};
  ActorExecuteContext::Guard guard{&actor_execute_context_};

  ActorState::Flags flags_;
  ActorSignals pending_signals_;

  ActorState::Flags &flags() {
    return flags_;
  }

  void start() {
    if (!can_send()) {
      return;
    }

    ActorSignals signals;
    SCOPE_EXIT {
      pending_signals_.add_signals(signals);
    };

    if (options_.from_queue) {
      signals.add_signal(ActorSignals::Pop);
    }

    actor_locker_.try_lock();
    flags_ = actor_locker_.flags();

    if (!actor_locker_.own_lock()) {
      return;
    }

    if (options_.from_queue) {
      flags().set_pause(false);
    }
    if (!actor_locker_.can_execute()) {
      CHECK(!options_.from_queue);
      return;
    }

    signals.add_signals(flags().get_signals());
    actor_info_.mailbox().pop_all();

    while (!actor_execute_context_.has_flags() && flush_one(signals)) {
    }
  }

  void finish() {
    if (!actor_locker_.own_lock()) {
      if (!pending_signals_.empty() && actor_locker_.add_signals(pending_signals_)) {
        flags_ = actor_locker_.flags();
      } else {
        return;
      }
    }
    CHECK(actor_locker_.own_lock());

    if (actor_execute_context_.has_flags()) {
      if (actor_execute_context_.get_stop()) {
        if (actor_info_.alarm_timestamp()) {
          dispatcher_.set_alarm_timestamp(actor_info_.actor().get_actor_info_ptr(), Timestamp::never());
        }
        flags_.set_closed(true);
        actor_info_.actor().tear_down();
        actor_info_.destroy_actor();
        return;
      }
      if (actor_execute_context_.get_pause()) {
        flags_.set_pause(true);
      }
      if (actor_execute_context_.get_alarm_flag()) {
        auto old_timestamp = actor_info_.alarm_timestamp();
        auto new_timestamp = actor_execute_context_.get_alarm_timestamp();
        if (!(old_timestamp == new_timestamp)) {
          actor_info_.alarm_timestamp() = new_timestamp;
          dispatcher_.set_alarm_timestamp(actor_info_.actor().get_actor_info_ptr(), new_timestamp);
        }
      }
    }
    flags_.set_signals(pending_signals_);

    bool add_to_queue = false;
    while (true) {
      // Drop InQueue flag if has pop signal
      // Can't delay this signal
      auto signals = flags().get_signals();
      if (signals.has_signal(ActorSignals::Pop)) {
        signals.clear_signal(ActorSignals::Pop);
        flags().set_signals(signals);
        flags().set_in_queue(false);
      }

      if (flags().has_signals() && !flags().is_in_queue()) {
        add_to_queue = true;
        flags().set_in_queue(true);
      }
      if (actor_locker_.try_unlock(flags())) {
        if (add_to_queue) {
          dispatcher_.add_to_queue(actor_info_.actor().get_actor_info_ptr(), flags().get_scheduler_id(),
                                   !flags().is_shared());
        }
        break;
      }
      flags_ = actor_locker_.flags();
    }
  }

  bool flush_one(ActorSignals &signals) {
    return flush_one_signal(signals) || flush_one_message();
  }

  bool flush_one_signal(ActorSignals &signals) {
    auto signal = signals.first_signal();
    if (!signal) {
      return false;
    }
    switch (signal) {
      case ActorSignals::Wakeup:
        actor_info_.actor().wake_up();
        break;
      case ActorSignals::Alarm:
        if (actor_execute_context_.get_alarm_timestamp().is_in_past()) {
          actor_execute_context_.alarm_timestamp() = Timestamp::never();
          actor_info_.actor().alarm();
        }
        break;
      case ActorSignals::Kill:
        actor_execute_context_.set_stop();
        break;
      case ActorSignals::StartUp:
        actor_info_.actor().start_up();
        break;
      case ActorSignals::TearDown:
        actor_info_.actor().tear_down();
        break;
      case ActorSignals::Pop:
        flags().set_in_queue(false);
        break;

      case ActorSignals::Message:
        break;
      case ActorSignals::Io:
      case ActorSignals::Cpu:
        LOG(FATAL) << "TODO";
      default:
        UNREACHABLE();
    }
    signals.clear_signal(signal);
    return true;
  }

  bool flush_one_message() {
    auto message = actor_info_.mailbox().reader().read();
    if (!message) {
      return false;
    }
    if (message.is_big() && !options_.from_queue) {
      actor_info_.mailbox().reader().delay(std::move(message));
      pending_signals_.add_signal(ActorSignals::Message);
      actor_execute_context_.set_pause();
      return false;
    }

    actor_execute_context_.set_link_token(message.get_link_token());
    message.run();
    return true;
  }
};

using SchedulerMessage = ActorInfoPtr;

struct WorkerInfo {
  enum class Type { Io, Cpu } type{Type::Io};
  WorkerInfo() = default;
  explicit WorkerInfo(Type type) : type(type) {
  }
  ActorInfoCreator actor_info_creator;
};

struct SchedulerInfo {
  SchedulerId id;
  // will be read by all workers is any thread
  std::unique_ptr<MpmcQueue<SchedulerMessage>> cpu_queue;
  std::unique_ptr<MpmcWaiter> cpu_queue_waiter;
  // only scheduler itself may read from io_queue_
  std::unique_ptr<MpscPollableQueue<SchedulerMessage>> io_queue;
  size_t cpu_threads_count{0};

  std::unique_ptr<WorkerInfo> io_worker;
  std::vector<std::unique_ptr<WorkerInfo>> cpu_workers;
};

struct SchedulerGroupInfo {
  explicit SchedulerGroupInfo(size_t n) : schedulers(n) {
  }
  std::atomic<bool> is_stop_requested{false};

  int active_scheduler_count{0};
  std::mutex active_scheduler_count_mutex;
  std::condition_variable active_scheduler_count_condition_variable;

  std::vector<SchedulerInfo> schedulers;
};

class SchedulerContext
    : public Context<SchedulerContext>
    , public SchedulerDispatcher {
 public:
  // DispatcherInterface
  SchedulerDispatcher &dispatcher() {
    return *this;
  }

  // ActorCreator Interface
  virtual ActorInfoCreator &get_actor_info_creator() = 0;

  // Poll interface
  virtual bool has_poll() = 0;
  virtual Poll &get_poll() = 0;

  // Timeout interface
  virtual bool has_heap() = 0;
  virtual KHeap<double> &get_heap() = 0;

  // Stop all schedulers
  virtual bool is_stop_requested() = 0;
  virtual void stop() = 0;
};

#if !TD_THREAD_UNSUPPORTED
class Scheduler {
 public:
  Scheduler(std::shared_ptr<SchedulerGroupInfo> scheduler_group_info, SchedulerId id, size_t cpu_threads_count)
      : scheduler_group_info_(std::move(scheduler_group_info)), cpu_threads_(cpu_threads_count) {
    scheduler_group_info_->active_scheduler_count++;
    info_ = &scheduler_group_info_->schedulers.at(id.value());
    info_->id = id;
    if (cpu_threads_count != 0) {
      info_->cpu_threads_count = cpu_threads_count;
      info_->cpu_queue = std::make_unique<MpmcQueue<SchedulerMessage>>(1024, max_thread_count());
      info_->cpu_queue_waiter = std::make_unique<MpmcWaiter>();
    }
    info_->io_queue = std::make_unique<MpscPollableQueue<SchedulerMessage>>();
    info_->io_queue->init();

    info_->cpu_workers.resize(cpu_threads_count);
    for (auto &worker : info_->cpu_workers) {
      worker = std::make_unique<WorkerInfo>(WorkerInfo::Type::Cpu);
    }
    info_->io_worker = std::make_unique<WorkerInfo>(WorkerInfo::Type::Io);

    poll_.init();
    io_worker_ = std::make_unique<IoWorker>(*info_->io_queue);
  }

  Scheduler(const Scheduler &) = delete;
  Scheduler &operator=(const Scheduler &) = delete;
  Scheduler(Scheduler &&other) = delete;
  Scheduler &operator=(Scheduler &&other) = delete;
  ~Scheduler() {
    // should stop
    stop();
    do_stop();
  }

  void start() {
    for (size_t i = 0; i < cpu_threads_.size(); i++) {
      cpu_threads_[i] = td::thread([this, i] {
        this->run_in_context_impl(*this->info_->cpu_workers[i],
                                  [this] { CpuWorker(*info_->cpu_queue, *info_->cpu_queue_waiter).run(); });
      });
    }
    this->run_in_context([this] { this->io_worker_->start_up(); });
  }

  template <class F>
  void run_in_context(F &&f) {
    run_in_context_impl(*info_->io_worker, std::forward<F>(f));
  }

  bool run(double timeout) {
    bool res;
    run_in_context_impl(*info_->io_worker, [this, timeout, &res] {
      if (SchedulerContext::get()->is_stop_requested()) {
        res = false;
      } else {
        res = io_worker_->run_once(timeout);
      }
      if (!res) {
        io_worker_->tear_down();
      }
    });
    if (!res) {
      do_stop();
    }
    return res;
  }

  // Just syntactic sugar
  void stop() {
    run_in_context([] { SchedulerContext::get()->stop(); });
  }

  SchedulerId get_scheduler_id() const {
    return info_->id;
  }

 private:
  std::shared_ptr<SchedulerGroupInfo> scheduler_group_info_;
  SchedulerInfo *info_;
  std::vector<td::thread> cpu_threads_;
  bool is_stopped_{false};
  Poll poll_;
  KHeap<double> heap_;
  class IoWorker;
  std::unique_ptr<IoWorker> io_worker_;

  class SchedulerContextImpl : public SchedulerContext {
   public:
    SchedulerContextImpl(WorkerInfo *worker, SchedulerInfo *scheduler, SchedulerGroupInfo *scheduler_group, Poll *poll,
                         KHeap<double> *heap)
        : worker_(worker), scheduler_(scheduler), scheduler_group_(scheduler_group), poll_(poll), heap_(heap) {
    }

    SchedulerId get_scheduler_id() const override {
      return scheduler()->id;
    }
    void add_to_queue(ActorInfoPtr actor_info_ptr, SchedulerId scheduler_id, bool need_poll) override {
      if (!scheduler_id.is_valid()) {
        scheduler_id = scheduler()->id;
      }
      auto &info = scheduler_group()->schedulers.at(scheduler_id.value());
      if (need_poll) {
        info.io_queue->writer_put(std::move(actor_info_ptr));
      } else {
        info.cpu_queue->push(std::move(actor_info_ptr), get_thread_id());
        info.cpu_queue_waiter->notify();
      }
    }

    ActorInfoCreator &get_actor_info_creator() override {
      return worker()->actor_info_creator;
    }

    bool has_poll() override {
      return poll_ != nullptr;
    }
    Poll &get_poll() override {
      CHECK(has_poll());
      return *poll_;
    }

    bool has_heap() override {
      return heap_ != nullptr;
    }
    KHeap<double> &get_heap() override {
      CHECK(has_heap());
      return *heap_;
    }

    void set_alarm_timestamp(const ActorInfoPtr &actor_info_ptr, Timestamp timestamp) override {
      // we are in PollWorker
      CHECK(has_heap());
      auto &heap = get_heap();
      auto *heap_node = actor_info_ptr->as_heap_node();
      if (timestamp) {
        if (heap_node->in_heap()) {
          heap.fix(timestamp.at(), heap_node);
        } else {
          heap.insert(timestamp.at(), heap_node);
        }
      } else {
        if (heap_node->in_heap()) {
          heap.erase(heap_node);
        }
      }

      // TODO: do something in plain worker
    }

    bool is_stop_requested() override {
      return scheduler_group()->is_stop_requested;
    }

    void stop() override {
      bool expect_false = false;
      // Trying to set close_flag_ to true with CAS
      auto &group = *scheduler_group();
      if (!group.is_stop_requested.compare_exchange_strong(expect_false, true)) {
        return;
      }

      // Notify all workers of all schedulers
      for (auto &scheduler_info : group.schedulers) {
        scheduler_info.io_queue->writer_put({});
        for (size_t i = 0; i < scheduler_info.cpu_threads_count; i++) {
          scheduler_info.cpu_queue->push({}, get_thread_id());
          scheduler_info.cpu_queue_waiter->notify();
        }
      }
    }

   private:
    WorkerInfo *worker() const {
      return worker_;
    }
    SchedulerInfo *scheduler() const {
      return scheduler_;
    }
    SchedulerGroupInfo *scheduler_group() const {
      return scheduler_group_;
    }

    WorkerInfo *worker_;
    SchedulerInfo *scheduler_;
    SchedulerGroupInfo *scheduler_group_;
    Poll *poll_;

    KHeap<double> *heap_;
  };

  template <class F>
  void run_in_context_impl(WorkerInfo &worker_info, F &&f) {
    bool is_io_worker = worker_info.type == WorkerInfo::Type::Io;
    SchedulerContextImpl context(&worker_info, info_, scheduler_group_info_.get(), is_io_worker ? &poll_ : nullptr,
                                 is_io_worker ? &heap_ : nullptr);
    SchedulerContext::Guard guard(&context);
    f();
  }

  class CpuWorker {
   public:
    CpuWorker(MpmcQueue<SchedulerMessage> &queue, MpmcWaiter &waiter) : queue_(queue), waiter_(waiter) {
    }
    void run() {
      auto thread_id = get_thread_id();
      auto &dispatcher = SchedulerContext::get()->dispatcher();

      int yields = 0;
      while (true) {
        SchedulerMessage message;
        if (queue_.try_pop(message, thread_id)) {
          if (!message) {
            return;
          }
          ActorExecutor executor(*message, dispatcher, ActorExecutor::Options().with_from_queue());
          yields = waiter_.stop_wait(yields, thread_id);
        } else {
          yields = waiter_.wait(yields, thread_id);
        }
      }
    }

   private:
    MpmcQueue<SchedulerMessage> &queue_;
    MpmcWaiter &waiter_;
  };

  class IoWorker {
   public:
    explicit IoWorker(MpscPollableQueue<SchedulerMessage> &queue) : queue_(queue) {
    }

    void start_up() {
      auto &poll = SchedulerContext::get()->get_poll();
      poll.subscribe(queue_.reader_get_event_fd().get_fd(), Fd::Flag::Read);
    }
    void tear_down() {
      auto &poll = SchedulerContext::get()->get_poll();
      poll.unsubscribe(queue_.reader_get_event_fd().get_fd());
    }

    bool run_once(double timeout) {
      auto &dispatcher = SchedulerContext::get()->dispatcher();
      auto &poll = SchedulerContext::get()->get_poll();
      auto &heap = SchedulerContext::get()->get_heap();

      auto now = Time::now();  // update Time::now_cached()
      while (!heap.empty() && heap.top_key() <= now) {
        auto *heap_node = heap.pop();
        auto *actor_info = ActorInfo::from_heap_node(heap_node);

        ActorExecutor executor(*actor_info, dispatcher, ActorExecutor::Options().with_has_poll(true));
        if (executor.can_send_immediate()) {
          executor.send_immediate(ActorSignals::one(ActorSignals::Alarm));
        } else {
          executor.send(ActorSignals::one(ActorSignals::Alarm));
        }
      }

      const int size = queue_.reader_wait_nonblock();
      for (int i = 0; i < size; i++) {
        auto message = queue_.reader_get_unsafe();
        if (!message) {
          return false;
        }
        ActorExecutor executor(*message, dispatcher, ActorExecutor::Options().with_from_queue().with_has_poll(true));
      }
      queue_.reader_flush();

      bool can_sleep = size == 0 && timeout != 0;
      int32 timeout_ms = 0;
      if (can_sleep) {
        auto wakeup_timestamp = Timestamp::in(timeout);
        if (!heap.empty()) {
          wakeup_timestamp.relax(Timestamp::at(heap.top_key()));
        }
        timeout_ms = static_cast<int>(wakeup_timestamp.in() * 1000) + 1;
        if (timeout_ms < 0) {
          timeout_ms = 0;
        }
        //const int thirty_seconds = 30 * 1000;
        //if (timeout_ms > thirty_seconds) {
        //timeout_ms = thirty_seconds;
        //}
      }
      poll.run(timeout_ms);
      return true;
    }

   private:
    MpscPollableQueue<SchedulerMessage> &queue_;
  };

  void do_stop() {
    if (is_stopped_) {
      return;
    }
    // wait other threads to finish
    for (auto &thread : cpu_threads_) {
      thread.join();
    }
    // Can't do anything else, other schedulers may send queries to this one.
    // Must wait till every scheduler is stopped first..
    is_stopped_ = true;

    io_worker_.reset();
    poll_.clear();

    std::unique_lock<std::mutex> lock(scheduler_group_info_->active_scheduler_count_mutex);
    scheduler_group_info_->active_scheduler_count--;
    scheduler_group_info_->active_scheduler_count_condition_variable.notify_all();
  }

 public:
  static void close_scheduler_group(SchedulerGroupInfo &group_info) {
    LOG(ERROR) << "close scheduler group";
    // Cannot close scheduler group before somebody asked to stop them
    CHECK(group_info.is_stop_requested);
    {
      std::unique_lock<std::mutex> lock(group_info.active_scheduler_count_mutex);
      group_info.active_scheduler_count_condition_variable.wait(lock,
                                                                [&] { return group_info.active_scheduler_count == 0; });
    }

    // Drain all queues
    // Just to destroy all elements should be ok.
    for (auto &scheduler_info : group_info.schedulers) {
      // Drain io queue
      auto &io_queue = *scheduler_info.io_queue;
      while (true) {
        int n = io_queue.reader_wait_nonblock();
        if (n == 0) {
          break;
        }
        while (n-- > 0) {
          auto message = io_queue.reader_get_unsafe();
          // message's destructor is called
        }
      }
      scheduler_info.io_queue.reset();

      // Drain cpu queue
      auto &cpu_queue = *scheduler_info.cpu_queue;
      while (true) {
        SchedulerMessage message;
        if (!cpu_queue.try_pop(message, get_thread_id())) {
          break;
        }
        // message's destructor is called
      }
      scheduler_info.cpu_queue.reset();

      // Do not destroy worker infos. run_in_context will crash if they are empty
    }
  }
};

// Actor messages
template <class LambdaT>
class ActorMessageLambda : public ActorMessageImpl {
 public:
  template <class FromLambdaT>
  explicit ActorMessageLambda(FromLambdaT &&lambda) : lambda_(std::forward<FromLambdaT>(lambda)) {
  }
  void run() override {
    lambda_();
  }

 private:
  LambdaT lambda_;
};

class ActorMessageHangup : public ActorMessageImpl {
 public:
  void run() override {
    ActorExecuteContext::get()->actor().hang_up();
  }
};

class ActorMessageCreator {
 public:
  template <class F>
  static ActorMessage lambda(F &&f) {
    return ActorMessage(std::make_unique<ActorMessageLambda<F>>(std::forward<F>(f)));
  }

  static ActorMessage hangup() {
    return ActorMessage(std::make_unique<ActorMessageHangup>());
  }

  // Use faster allocation?
};

// SYNTAX SHUGAR
namespace detail {
struct ActorRef {
  ActorRef(ActorInfo &actor_info, uint64 link_token = EmptyLinkToken) : actor_info(actor_info), link_token(link_token) {
  }

  ActorInfo &actor_info;
  uint64 link_token;
};

template <class T>
T &current_actor() {
  return static_cast<T &>(ActorExecuteContext::get()->actor());
}

void send_message(ActorInfo &actor_info, ActorMessage message) {
  ActorExecutor executor(actor_info, SchedulerContext::get()->dispatcher(), ActorExecutor::Options());
  executor.send(std::move(message));
}

void send_message(ActorRef actor_ref, ActorMessage message) {
  message.set_link_token(actor_ref.link_token);
  send_message(actor_ref.actor_info, std::move(message));
}
void send_message_later(ActorInfo &actor_info, ActorMessage message) {
  ActorExecutor executor(actor_info, SchedulerContext::get()->dispatcher(), ActorExecutor::Options());
  executor.send(std::move(message));
}

void send_message_later(ActorRef actor_ref, ActorMessage message) {
  message.set_link_token(actor_ref.link_token);
  send_message_later(actor_ref.actor_info, std::move(message));
}

template <class ExecuteF, class ToMessageF>
void send_immediate(ActorRef actor_ref, ExecuteF &&execute, ToMessageF &&to_message) {
  auto &scheduler_context = *SchedulerContext::get();
  ActorExecutor executor(actor_ref.actor_info, scheduler_context.dispatcher(),
                         ActorExecutor::Options().with_has_poll(scheduler_context.has_poll()));
  if (executor.can_send_immediate()) {
    return executor.send_immediate(execute, actor_ref.link_token);
  }
  auto message = to_message();
  message.set_link_token(actor_ref.link_token);
  executor.send(std::move(message));
}

template <class F>
void send_lambda(ActorRef actor_ref, F &&lambda) {
  send_immediate(actor_ref, lambda, [&lambda]() mutable { return ActorMessageCreator::lambda(std::move(lambda)); });
}
template <class F>
void send_lambda_later(ActorRef actor_ref, F &&lambda) {
  send_message_later(actor_ref, ActorMessageCreator::lambda(std::move(lambda)));
}

template <class ClosureT>
void send_closure_impl(ActorRef actor_ref, ClosureT &&closure) {
  using ActorType = typename ClosureT::ActorType;
  send_immediate(actor_ref, [&closure]() mutable { closure.run(&current_actor<ActorType>()); },
                 [&closure]() mutable {
                   return ActorMessageCreator::lambda([closure = to_delayed_closure(std::move(closure))]() mutable {
                     closure.run(&current_actor<ActorType>());
                   });
                 });
}

template <class... ArgsT>
void send_closure(ActorRef actor_ref, ArgsT &&... args) {
  send_closure_impl(actor_ref, create_immediate_closure(std::forward<ArgsT>(args)...));
}

template <class ClosureT>
void send_closure_later_impl(ActorRef actor_ref, ClosureT &&closure) {
  using ActorType = typename ClosureT::ActorType;
  send_message_later(actor_ref, ActorMessageCreator::lambda([closure = std::move(closure)]() mutable {
                       closure.run(&current_actor<ActorType>());
                     }));
}

template <class... ArgsT>
void send_closure_later(ActorRef actor_ref, ArgsT &&... args) {
  send_closure_later_impl(actor_ref, create_delayed_closure(std::forward<ArgsT>(args)...));
}

void register_actor_info_ptr(ActorInfoPtr actor_info_ptr) {
  auto state = actor_info_ptr->state().get_flags_unsafe();
  SchedulerContext::get()->add_to_queue(std::move(actor_info_ptr), state.get_scheduler_id(), !state.is_shared());
}

template <class T, class... ArgsT>
ActorInfoPtr create_actor(ActorOptions &options, ArgsT &&... args) {
  auto *scheduler_context = SchedulerContext::get();
  if (!options.has_scheduler()) {
    options.on_scheduler(scheduler_context->get_scheduler_id());
  }
  auto res =
      scheduler_context->get_actor_info_creator().create(std::make_unique<T>(std::forward<ArgsT>(args)...), options);
  register_actor_info_ptr(res);
  return res;
}
}  // namespace detail

// Essentially ActorInfoWeakPtr with Type
template <class ActorType = Actor>
class ActorId {
 public:
  using ActorT = ActorType;
  ActorId() = default;
  ActorId(const ActorId &) = default;
  ActorId &operator=(const ActorId &) = default;
  ActorId(ActorId &&other) = default;
  ActorId &operator=(ActorId &&other) = default;

  // allow only conversion from child to parent
  template <class ToActorType, class = std::enable_if_t<std::is_base_of<ToActorType, ActorType>::value>>
  explicit operator ActorId<ToActorType>() const {
    return ActorId<ToActorType>(ptr_);
  }

  const ActorInfoPtr &actor_info_ptr() const {
    return ptr_;
  }

  ActorInfo &actor_info() const {
    CHECK(ptr_);
    return *ptr_;
  }
  bool empty() const {
    return !ptr_;
  }

  template <class... ArgsT>
  static ActorId<ActorType> create(ActorOptions &options, ArgsT &&... args) {
    return ActorId<ActorType>(detail::create_actor<ActorType>(options, std::forward<ArgsT>(args)...));
  }

  detail::ActorRef as_actor_ref() const {
    CHECK(!empty());
    return detail::ActorRef(*actor_info_ptr());
  }

 private:
  ActorInfoPtr ptr_;

  explicit ActorId(ActorInfoPtr ptr) : ptr_(std::move(ptr)) {
  }

  template <class SelfT>
  friend ActorId<SelfT> actor_id(SelfT *self);
};

template <class ActorType = Actor>
class ActorOwn {
 public:
  using ActorT = ActorType;
  ActorOwn() = default;
  explicit ActorOwn(ActorId<ActorType> id) : id_(std::move(id)) {
  }
  template <class OtherActorType>
  explicit ActorOwn(ActorId<OtherActorType> id) : id_(std::move(id)) {
  }
  template <class OtherActorType>
  explicit ActorOwn(ActorOwn<OtherActorType> &&other) : id_(other.release()) {
  }
  template <class OtherActorType>
  ActorOwn &operator=(ActorOwn<OtherActorType> &&other) {
    reset(other.release());
  }
  ActorOwn(ActorOwn &&other) : id_(other.release()) {
  }
  ActorOwn &operator=(ActorOwn &&other) {
    reset(other.release());
  }
  ActorOwn(const ActorOwn &) = delete;
  ActorOwn &operator=(const ActorOwn &) = delete;
  ~ActorOwn() {
    reset();
  }

  bool empty() const {
    return id_.empty();
  }
  bool is_alive() const {
    return id_.is_alive();
  }
  ActorId<ActorType> get() const {
    return id_;
  }
  ActorId<ActorType> release() {
    return std::move(id_);
  }
  void reset(ActorId<ActorType> other = ActorId<ActorType>()) {
    static_assert(sizeof(ActorType) > 0, "Can't use ActorOwn with incomplete type");
    hangup();
    id_ = std::move(other);
  }
  const ActorId<ActorType> *operator->() const {
    return &id_;
  }

  detail::ActorRef as_actor_ref() const {
    CHECK(!empty());
    return detail::ActorRef(*id_.actor_info_ptr(), 0);
  }

 private:
  ActorId<ActorType> id_;
  void hangup() const {
    if (empty()) {
      return;
    }
    detail::send_message(as_actor_ref(), ActorMessageCreator::hangup());
  }
};

template <class ActorType = Actor>
class ActorShared {
 public:
  using ActorT = ActorType;
  ActorShared() = default;
  template <class OtherActorType>
  ActorShared(ActorId<OtherActorType> id, uint64 token) : id_(std::move(id)), token_(token) {
    CHECK(token_ != 0);
  }
  template <class OtherActorType>
  ActorShared(ActorShared<OtherActorType> &&other) : id_(other.release()), token_(other.token()) {
  }
  template <class OtherActorType>
  ActorShared(ActorOwn<OtherActorType> &&other) : id_(other.release()), token_(other.token()) {
  }
  template <class OtherActorType>
  ActorShared &operator=(ActorShared<OtherActorType> &&other) {
    reset(other.release(), other.token());
  }
  ActorShared(ActorShared &&other) : id_(other.release()), token_(other.token()) {
  }
  ActorShared &operator=(ActorShared &&other) {
    reset(other.release(), other.token());
  }
  ActorShared(const ActorShared &) = delete;
  ActorShared &operator=(const ActorShared &) = delete;
  ~ActorShared() {
    reset();
  }

  uint64 token() const {
    return token_;
  }
  bool empty() const {
    return id_.empty();
  }
  bool is_alive() const {
    return id_.is_alive();
  }
  ActorId<ActorType> get() const {
    return id_;
  }
  ActorId<ActorType> release();
  void reset(ActorId<ActorType> other = ActorId<ActorType>(), uint64 link_token = EmptyLinkToken) {
    static_assert(sizeof(ActorType) > 0, "Can't use ActorShared with incomplete type");
    hangup();
    id_ = other;
    token_ = link_token;
  }
  const ActorId<ActorType> *operator->() const {
    return &id_;
  }

  detail::ActorRef as_actor_ref() const {
    CHECK(!empty());
    return detail::ActorRef(*id_.actor_info_ptr(), token_);
  }

 private:
  ActorId<ActorType> id_;
  uint64 token_;

  void hangup() const {
  }
};

// common interface
template <class SelfT>
ActorId<SelfT> actor_id(SelfT *self) {
  CHECK(self);
  CHECK(static_cast<Actor *>(self) == &ActorExecuteContext::get()->actor());
  return ActorId<SelfT>(ActorExecuteContext::get()->actor().get_actor_info_ptr());
}

inline ActorId<> actor_id() {
  return actor_id(&ActorExecuteContext::get()->actor());
}

template <class T, class... ArgsT>
ActorOwn<T> create_actor(ActorOptions options, ArgsT &&... args) {
  return ActorOwn<T>(ActorId<T>::create(options, std::forward<ArgsT>(args)...));
}

template <class T, class... ArgsT>
ActorOwn<T> create_actor(Slice name, ArgsT &&... args) {
  return ActorOwn<T>(ActorId<T>::create(ActorOptions().with_name(name), std::forward<ArgsT>(args)...));
}

template <class ActorIdT, class FunctionT, class... ArgsT>
void send_closure(ActorIdT &&actor_id, FunctionT function, ArgsT &&... args) {
  using ActorT = typename std::decay_t<ActorIdT>::ActorT;
  using FunctionClassT = member_function_class_t<FunctionT>;
  static_assert(std::is_base_of<FunctionClassT, ActorT>::value, "unsafe send_closure");

  ActorIdT id = std::forward<ActorIdT>(actor_id);
  detail::send_closure(id.as_actor_ref(), function, std::forward<ArgsT>(args)...);
}

template <class ActorIdT, class FunctionT, class... ArgsT>
void send_closure_later(ActorIdT &&actor_id, FunctionT function, ArgsT &&... args) {
  using ActorT = typename std::decay_t<ActorIdT>::ActorT;
  using FunctionClassT = member_function_class_t<FunctionT>;
  static_assert(std::is_base_of<FunctionClassT, ActorT>::value, "unsafe send_closure");

  ActorIdT id = std::forward<ActorIdT>(actor_id);
  detail::send_closure_later(id.as_actor_ref(), function, std::forward<ArgsT>(args)...);
}

template <class ActorIdT, class... ArgsT>
void send_lambda(ActorIdT &&actor_id, ArgsT &&... args) {
  ActorIdT id = std::forward<ActorIdT>(actor_id);
  detail::send_lambda(id.as_actor_ref(), std::forward<ArgsT>(args)...);
}

#endif  //!TD_THREAD_UNSUPPORTED
}  // namespace actor2
}  // namespace td
