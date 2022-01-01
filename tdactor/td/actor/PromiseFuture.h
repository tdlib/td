//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/utils/CancellationToken.h"
#include "td/utils/Closure.h"
#include "td/utils/common.h"
#include "td/utils/invoke.h"
#include "td/utils/MovableValue.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Status.h"

#include <tuple>
#include <type_traits>
#include <utility>

namespace td {

template <class T = Unit>
class PromiseInterface {
 public:
  PromiseInterface() = default;
  PromiseInterface(const PromiseInterface &) = delete;
  PromiseInterface &operator=(const PromiseInterface &) = delete;
  PromiseInterface(PromiseInterface &&) = default;
  PromiseInterface &operator=(PromiseInterface &&) = default;
  virtual ~PromiseInterface() = default;
  virtual void set_value(T &&value) {
    set_result(std::move(value));
  }
  virtual void set_error(Status &&error) {
    set_result(std::move(error));
  }
  virtual void set_result(Result<T> &&result) {
    if (result.is_ok()) {
      set_value(result.move_as_ok());
    } else {
      set_error(result.move_as_error());
    }
  }
  void operator()(T &&value) {
    set_value(std::move(value));
  }
  void operator()(Status &&error) {
    set_error(std::move(error));
  }
  void operator()(Result<T> &&result) {
    set_result(std::move(result));
  }
  virtual bool is_cancellable() const {
    return false;
  }
  virtual bool is_canceled() const {
    return false;
  }

  virtual void start_migrate(int32 sched_id) {
  }
  virtual void finish_migrate() {
  }
};

namespace detail {

template <typename T>
struct GetArg final : public GetArg<decltype(&T::operator())> {};

template <class C, class R, class Arg>
class GetArg<R (C::*)(Arg)> {
 public:
  using type = Arg;
};
template <class C, class R, class Arg>
class GetArg<R (C::*)(Arg) const> {
 public:
  using type = Arg;
};

template <class T>
using get_arg_t = std::decay_t<typename GetArg<T>::type>;

template <class T>
struct DropResult {
  using type = T;
};

template <class T>
struct DropResult<Result<T>> {
  using type = T;
};

template <class T>
using drop_result_t = typename DropResult<T>::type;

struct Ignore {
  void operator()(Status &&error) {
    error.ignore();
  }
};

template <class ValueT, class FunctionOkT, class FunctionFailT = Ignore>
class LambdaPromise : public PromiseInterface<ValueT> {
  enum class OnFail { None, Ok, Fail };

 public:
  void set_value(ValueT &&value) override {
    CHECK(has_lambda_.get());
    do_ok(ok_, std::move(value));
    on_fail_ = OnFail::None;
  }
  void set_error(Status &&error) override {
    CHECK(has_lambda_.get());
    do_error(std::move(error));
  }
  LambdaPromise(const LambdaPromise &other) = delete;
  LambdaPromise &operator=(const LambdaPromise &other) = delete;
  LambdaPromise(LambdaPromise &&other) = default;
  LambdaPromise &operator=(LambdaPromise &&other) = default;
  ~LambdaPromise() override {
    if (has_lambda_.get()) {
      do_error(Status::Error("Lost promise"));
    }
  }

  template <class FromOkT, class FromFailT>
  LambdaPromise(FromOkT &&ok, FromFailT &&fail, bool use_ok_as_fail)
      : ok_(std::forward<FromOkT>(ok))
      , fail_(std::forward<FromFailT>(fail))
      , on_fail_(use_ok_as_fail ? OnFail::Ok : OnFail::Fail)
      , has_lambda_(true) {
  }
  template <class FromOkT, std::enable_if_t<!std::is_same<std::decay_t<FromOkT>, LambdaPromise>::value, int> = 0>
  LambdaPromise(FromOkT &&ok) : LambdaPromise(std::forward<FromOkT>(ok), Ignore(), true) {
  }

 private:
  FunctionOkT ok_;
  FunctionFailT fail_;
  OnFail on_fail_ = OnFail::None;
  MovableValue<bool> has_lambda_{false};

  void do_error(Status &&error) {
    switch (on_fail_) {
      case OnFail::None:
        break;
      case OnFail::Ok:
        do_error(ok_, std::move(error));
        break;
      case OnFail::Fail:
        do_error(fail_, std::move(error));
        break;
    }
    on_fail_ = OnFail::None;
  }

  template <class F>
  std::enable_if_t<is_callable<F, Result<ValueT>>::value, void> do_error(F &&f, Status &&status) {
    f(Result<ValueT>(std::move(status)));
  }
  template <class Y, class F>
  std::enable_if_t<!is_callable<F, Result<ValueT>>::value, void> do_error(F &&f, Y &&status) {
    f(Auto());
  }
  template <class F>
  std::enable_if_t<is_callable<F, Result<ValueT>>::value, void> do_ok(F &&f, ValueT &&result) {
    f(Result<ValueT>(std::move(result)));
  }
  template <class F>
  std::enable_if_t<!is_callable<F, Result<ValueT>>::value, void> do_ok(F &&f, ValueT &&result) {
    f(std::move(result));
  }
};
}  // namespace detail

template <class T>
class SafePromise;

template <class T = Unit>
class Promise;

template <class T>
struct is_promise_interface : std::false_type {};

template <class U>
struct is_promise_interface<PromiseInterface<U>> : std::true_type {};

template <class U>
struct is_promise_interface<Promise<U>> : std::true_type {};

template <class T>
struct is_promise_interface_ptr : std::false_type {};

template <class U>
struct is_promise_interface_ptr<unique_ptr<U>> : std::true_type {};

template <class T = void, class F = void, std::enable_if_t<std::is_same<T, void>::value, bool> has_t = false>
auto lambda_promise(F &&f) {
  return detail::LambdaPromise<detail::drop_result_t<detail::get_arg_t<std::decay_t<F>>>, std::decay_t<F>>(
      std::forward<F>(f));
}
template <class T = void, class F = void, std::enable_if_t<!std::is_same<T, void>::value, bool> has_t = true>
auto lambda_promise(F &&f) {
  return detail::LambdaPromise<T, std::decay_t<F>>(std::forward<F>(f));
}

template <class T, class F,
          std::enable_if_t<is_promise_interface<std::decay_t<F>>::value, bool> from_promise_interface = true>
auto &&promise_interface(F &&f) {
  return std::forward<F>(f);
}

template <class T, class F,
          std::enable_if_t<!is_promise_interface<std::decay_t<F>>::value, bool> from_promise_interface = false>
auto promise_interface(F &&f) {
  return lambda_promise<T>(std::forward<F>(f));
}

template <class T, class F,
          std::enable_if_t<is_promise_interface_ptr<std::decay_t<F>>::value, bool> from_promise_interface = true>
auto promise_interface_ptr(F &&f) {
  return std::forward<F>(f);
}

template <class T, class F,
          std::enable_if_t<!is_promise_interface_ptr<std::decay_t<F>>::value, bool> from_promise_interface = false>
auto promise_interface_ptr(F &&f) {
  return td::make_unique<std::decay_t<decltype(promise_interface<T>(std::forward<F>(f)))>>(
      promise_interface<T>(std::forward<F>(f)));
}

template <class T>
class Promise {
 public:
  void set_value(T &&value) {
    if (!promise_) {
      return;
    }
    promise_->set_value(std::move(value));
    promise_.reset();
  }
  void set_error(Status &&error) {
    if (!promise_) {
      return;
    }
    promise_->set_error(std::move(error));
    promise_.reset();
  }
  void set_result(Result<T> &&result) {
    if (!promise_) {
      return;
    }
    promise_->set_result(std::move(result));
    promise_.reset();
  }
  template <class S>
  void operator()(S &&result) {
    if (!promise_) {
      return;
    }
    promise_->operator()(std::forward<S>(result));
    promise_.reset();
  }
  void reset() {
    promise_.reset();
  }
  void start_migrate(int32 sched_id) {
    if (!promise_) {
      return;
    }
    promise_->start_migrate(sched_id);
  }
  void finish_migrate() {
    if (!promise_) {
      return;
    }
    promise_->finish_migrate();
  }
  bool is_cancellable() const {
    if (!promise_) {
      return false;
    }
    return promise_->is_cancellable();
  }
  bool is_canceled() const {
    if (!promise_) {
      return false;
    }
    return promise_->is_canceled();
  }
  unique_ptr<PromiseInterface<T>> release() {
    return std::move(promise_);
  }

  Promise() = default;
  explicit Promise(unique_ptr<PromiseInterface<T>> promise) : promise_(std::move(promise)) {
  }
  Promise(Auto) {
  }
  Promise(SafePromise<T> &&other);
  Promise &operator=(SafePromise<T> &&other);
  template <class F, std::enable_if_t<!std::is_same<std::decay_t<F>, Promise>::value, int> = 0>
  Promise(F &&f) : promise_(promise_interface_ptr<T>(std::forward<F>(f))) {
  }

  explicit operator bool() {
    return static_cast<bool>(promise_);
  }

 private:
  unique_ptr<PromiseInterface<T>> promise_;
};

template <class T>
void start_migrate(Promise<T> &promise, int32 sched_id) {
  // promise.start_migrate(sched_id);
}
template <class T>
void finish_migrate(Promise<T> &promise) {
  // promise.finish_migrate();
}

template <class T = Unit>
class SafePromise {
 public:
  SafePromise(Promise<T> promise, Result<T> result) : promise_(std::move(promise)), result_(std::move(result)) {
  }
  SafePromise(const SafePromise &other) = delete;
  SafePromise &operator=(const SafePromise &other) = delete;
  SafePromise(SafePromise &&other) = default;
  SafePromise &operator=(SafePromise &&other) = default;
  ~SafePromise() {
    if (promise_) {
      promise_.set_result(std::move(result_));
    }
  }
  Promise<T> release() {
    return std::move(promise_);
  }

 private:
  Promise<T> promise_;
  Result<T> result_;
};

template <class T>
Promise<T>::Promise(SafePromise<T> &&other) : Promise(other.release()) {
}
template <class T>
Promise<T> &Promise<T>::operator=(SafePromise<T> &&other) {
  *this = other.release();
  return *this;
}

namespace detail {

class EventPromise final : public PromiseInterface<Unit> {
 public:
  void set_value(Unit &&) final {
    ok_.try_emit();
    fail_.clear();
  }
  void set_error(Status &&) final {
    do_set_error();
  }

  EventPromise(const EventPromise &other) = delete;
  EventPromise &operator=(const EventPromise &other) = delete;
  EventPromise(EventPromise &&other) = delete;
  EventPromise &operator=(EventPromise &&other) = delete;
  ~EventPromise() final {
    do_set_error();
  }

  EventPromise() = default;
  explicit EventPromise(EventFull ok) : ok_(std::move(ok)), use_ok_as_fail_(true) {
  }
  EventPromise(EventFull ok, EventFull fail) : ok_(std::move(ok)), fail_(std::move(fail)), use_ok_as_fail_(false) {
  }

 private:
  EventFull ok_;
  EventFull fail_;
  bool use_ok_as_fail_ = false;
  void do_set_error() {
    if (use_ok_as_fail_) {
      ok_.try_emit();
    } else {
      ok_.clear();
      fail_.try_emit();
    }
  }
};

template <class PromiseT>
class CancellablePromise final : public PromiseT {
 public:
  template <class... ArgsT>
  CancellablePromise(CancellationToken cancellation_token, ArgsT &&...args)
      : PromiseT(std::forward<ArgsT>(args)...), cancellation_token_(std::move(cancellation_token)) {
  }
  bool is_cancellable() const final {
    return true;
  }
  bool is_canceled() const final {
    return static_cast<bool>(cancellation_token_);
  }

 private:
  CancellationToken cancellation_token_;
};

template <class... ArgsT>
class JoinPromise final : public PromiseInterface<Unit> {
 public:
  explicit JoinPromise(ArgsT &&...arg) : promises_(std::forward<ArgsT>(arg)...) {
  }
  void set_value(Unit &&) final {
    tuple_for_each(promises_, [](auto &promise) { promise.set_value(Unit()); });
  }
  void set_error(Status &&error) final {
    tuple_for_each(promises_, [&error](auto &promise) { promise.set_error(error.clone()); });
  }

 private:
  std::tuple<std::decay_t<ArgsT>...> promises_;
};
}  // namespace detail

class SendClosure {
 public:
  template <class... ArgsT>
  void operator()(ArgsT &&...args) const {
    send_closure(std::forward<ArgsT>(args)...);
  }
};

//template <class T>
//template <class... ArgsT>
//auto Promise<T>::send_closure(ArgsT &&... args) {
//  return [promise = std::move(*this), t = std::make_tuple(std::forward<ArgsT>(args)...)](auto &&r_res) mutable {
//    TRY_RESULT_PROMISE(promise, res, std::move(r_res));
//    td2::call_tuple(SendClosure(), std::tuple_cat(std::move(t), std::make_tuple(std::move(res), std::move(promise))));
//  };
//}

template <class... ArgsT>
auto promise_send_closure(ArgsT &&...args) {
  return [t = std::make_tuple(std::forward<ArgsT>(args)...)](auto &&res) mutable {
    call_tuple(SendClosure(), std::tuple_cat(std::move(t), std::make_tuple(std::forward<decltype(res)>(res))));
  };
}

/*** FutureActor and PromiseActor ***/
template <class T>
class FutureActor;

template <class T>
class PromiseActor;

template <class T>
class ActorTraits<FutureActor<T>> {
 public:
  static constexpr bool need_context = false;
  static constexpr bool need_start_up = false;
};

template <class T>
class PromiseActor final : public PromiseInterface<T> {
  friend class FutureActor<T>;
  enum State { Waiting, Hangup };

 public:
  PromiseActor() = default;
  PromiseActor(const PromiseActor &other) = delete;
  PromiseActor &operator=(const PromiseActor &other) = delete;
  PromiseActor(PromiseActor &&) = default;
  PromiseActor &operator=(PromiseActor &&) = default;
  ~PromiseActor() final {
    close();
  }

  void set_value(T &&value) final;
  void set_error(Status &&error) final;

  void close() {
    future_id_.reset();
  }

  // NB: if true is returned no further events will be sent
  bool is_hangup() {
    if (state_ == State::Hangup) {
      return true;
    }
    if (!future_id_.is_alive()) {
      state_ = State::Hangup;
      future_id_.release();
      event_.clear();
      return true;
    }
    return false;
  }

  template <class S>
  friend void init_promise_future(PromiseActor<S> *promise, FutureActor<S> *future);

  bool empty_promise() {
    return future_id_.empty();
  }
  bool empty() {
    return future_id_.empty();
  }

 private:
  ActorOwn<FutureActor<T>> future_id_;
  EventFull event_;
  State state_ = State::Hangup;

  void init() {
    state_ = State::Waiting;
    event_.clear();
  }
};

template <class T>
class FutureActor final : public Actor {
  friend class PromiseActor<T>;

 public:
  enum State { Waiting, Ready };

  static constexpr int HANGUP_ERROR_CODE = 426487;

  FutureActor() = default;

  FutureActor(const FutureActor &other) = delete;
  FutureActor &operator=(const FutureActor &other) = delete;

  FutureActor(FutureActor &&other) = default;
  FutureActor &operator=(FutureActor &&other) = default;

  ~FutureActor() final = default;

  bool is_ok() const {
    return is_ready() && result_.is_ok();
  }
  bool is_error() const {
    CHECK(is_ready());
    return is_ready() && result_.is_error();
  }
  T move_as_ok() {
    return move_as_result().move_as_ok();
  }
  Status move_as_error() TD_WARN_UNUSED_RESULT {
    return move_as_result().move_as_error();
  }
  Result<T> move_as_result() TD_WARN_UNUSED_RESULT {
    CHECK(is_ready());
    SCOPE_EXIT {
      do_stop();
    };
    return std::move(result_);
  }
  bool is_ready() const {
    return !empty() && state_ == State::Ready;
  }

  void close() {
    event_.clear();
    result_.clear();
    do_stop();
  }

  void set_event(EventFull &&event) {
    CHECK(!empty());
    event_ = std::move(event);
    if (state_ != State::Waiting) {
      event_.try_emit_later();
    }
  }

  State get_state() const {
    return state_;
  }

  template <class S>
  friend void init_promise_future(PromiseActor<S> *promise, FutureActor<S> *future);

 private:
  EventFull event_;
  Result<T> result_ = Status::Error(500, "Empty FutureActor");
  State state_ = State::Waiting;

  void set_value(T &&value) {
    set_result(std::move(value));
  }

  void set_error(Status &&error) {
    set_result(std::move(error));
  }

  void set_result(Result<T> &&result) {
    CHECK(state_ == State::Waiting);
    result_ = std::move(result);
    state_ = State::Ready;

    event_.try_emit_later();
  }

  void hangup() final {
    set_error(Status::Error<HANGUP_ERROR_CODE>());
  }

  void start_up() final {
    // empty
  }

  void init() {
    CHECK(empty());
    state_ = State::Waiting;
    event_.clear();
  }
};

template <class T>
void PromiseActor<T>::set_value(T &&value) {
  if (state_ == State::Waiting && !future_id_.empty()) {
    send_closure(std::move(future_id_), &FutureActor<T>::set_value, std::move(value));
  }
}
template <class T>
void PromiseActor<T>::set_error(Status &&error) {
  if (state_ == State::Waiting && !future_id_.empty()) {
    send_closure(std::move(future_id_), &FutureActor<T>::set_error, std::move(error));
  }
}

template <class S>
void init_promise_future(PromiseActor<S> *promise, FutureActor<S> *future) {
  promise->init();
  future->init();
  promise->future_id_ = register_actor("FutureActor", future);

  CHECK(future->get_info() != nullptr);
}

template <class T>
class PromiseFuture {
 public:
  PromiseFuture() {
    init_promise_future(&promise_, &future_);
  }
  PromiseActor<T> &promise() {
    return promise_;
  }
  FutureActor<T> &future() {
    return future_;
  }
  PromiseActor<T> &&move_promise() {
    return std::move(promise_);
  }
  FutureActor<T> &&move_future() {
    return std::move(future_);
  }

 private:
  PromiseActor<T> promise_;
  FutureActor<T> future_;
};

template <ActorSendType send_type, class T, class ActorAT, class ActorBT, class ResultT, class... DestArgsT,
          class... ArgsT>
FutureActor<T> send_promise(ActorId<ActorAT> actor_id, ResultT (ActorBT::*func)(PromiseActor<T> &&, DestArgsT...),
                            ArgsT &&...args) {
  PromiseFuture<T> pf;
  Scheduler::instance()->send_closure<send_type>(
      std::move(actor_id), create_immediate_closure(func, pf.move_promise(), std::forward<ArgsT>(args)...));
  return pf.move_future();
}

class PromiseCreator {
 public:
  using Ignore = detail::Ignore;

  template <class OkT, class ArgT = detail::drop_result_t<detail::get_arg_t<OkT>>>
  static Promise<ArgT> lambda(OkT &&ok) {
    return Promise<ArgT>(td::make_unique<detail::LambdaPromise<ArgT, std::decay_t<OkT>>>(std::forward<OkT>(ok)));
  }

  template <class OkT, class FailT, class ArgT = detail::get_arg_t<OkT>>
  static Promise<ArgT> lambda(OkT &&ok, FailT &&fail) {
    return Promise<ArgT>(td::make_unique<detail::LambdaPromise<ArgT, std::decay_t<OkT>, std::decay_t<FailT>>>(
        std::forward<OkT>(ok), std::forward<FailT>(fail), false));
  }

  template <class OkT, class ArgT = detail::drop_result_t<detail::get_arg_t<OkT>>>
  static auto cancellable_lambda(CancellationToken cancellation_token, OkT &&ok) {
    return Promise<ArgT>(td::make_unique<detail::CancellablePromise<detail::LambdaPromise<ArgT, std::decay_t<OkT>>>>(
        std::move(cancellation_token), std::forward<OkT>(ok)));
  }

  static Promise<> event(EventFull &&ok) {
    return Promise<>(td::make_unique<detail::EventPromise>(std::move(ok)));
  }

  static Promise<> event(EventFull ok, EventFull fail) {
    return Promise<>(td::make_unique<detail::EventPromise>(std::move(ok), std::move(fail)));
  }

  template <class... ArgsT>
  static Promise<> join(ArgsT &&...args) {
    return Promise<>(td::make_unique<detail::JoinPromise<ArgsT...>>(std::forward<ArgsT>(args)...));
  }

  template <class T>
  static Promise<T> from_promise_actor(PromiseActor<T> &&from) {
    return Promise<T>(td::make_unique<PromiseActor<T>>(std::move(from)));
  }
};

}  // namespace td
