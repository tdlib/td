//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"

namespace td {

class MultiPromiseInterface {
 public:
  virtual void add_promise(Promise<> &&promise) = 0;
  virtual Promise<> get_promise() = 0;

  // deprecated?
  virtual size_t promise_count() const = 0;
  virtual void set_ignore_errors(bool ignore_errors) = 0;

  MultiPromiseInterface() = default;
  MultiPromiseInterface(const MultiPromiseInterface &) = delete;
  MultiPromiseInterface &operator=(const MultiPromiseInterface &) = delete;
  MultiPromiseInterface(MultiPromiseInterface &&) = default;
  MultiPromiseInterface &operator=(MultiPromiseInterface &&) = default;
  virtual ~MultiPromiseInterface() = default;
};

class MultiPromise : public MultiPromiseInterface {
 public:
  void add_promise(Promise<> &&promise) override {
    impl_->add_promise(std::move(promise));
  }
  Promise<> get_promise() override {
    return impl_->get_promise();
  }

  // deprecated?
  size_t promise_count() const override {
    return impl_->promise_count();
  }
  void set_ignore_errors(bool ignore_errors) override {
    impl_->set_ignore_errors(ignore_errors);
  }

  MultiPromise() = default;
  explicit MultiPromise(std::unique_ptr<MultiPromiseInterface> impl) : impl_(std::move(impl)) {
  }

 private:
  std::unique_ptr<MultiPromiseInterface> impl_;
};

class MultiPromiseActor final
    : public Actor
    , public MultiPromiseInterface {
 public:
  MultiPromiseActor() = default;

  void add_promise(Promise<Unit> &&promise) override;

  Promise<Unit> get_promise() override;

  void set_ignore_errors(bool ignore_errors) override;

  size_t promise_count() const override;

 private:
  void set_result(Result<Unit> &&result);

  vector<Promise<Unit>> promises_;     // promises waiting for result
  vector<FutureActor<Unit>> futures_;  // futures waiting for result of the queries
  size_t received_results_ = 0;
  bool ignore_errors_ = false;

  void raw_event(const Event::Raw &event) override;

  void on_start_migrate(int32) override {
    UNREACHABLE();
  }
  void on_finish_migrate() override {
    UNREACHABLE();
  }
};

class MultiPromiseActorSafe : public MultiPromiseInterface {
 public:
  void add_promise(Promise<Unit> &&promise) override;
  Promise<Unit> get_promise() override;
  void set_ignore_errors(bool ignore_errors) override;
  size_t promise_count() const override;
  MultiPromiseActorSafe() = default;
  MultiPromiseActorSafe(const MultiPromiseActorSafe &other) = delete;
  MultiPromiseActorSafe &operator=(const MultiPromiseActorSafe &other) = delete;
  MultiPromiseActorSafe(MultiPromiseActorSafe &&other) = delete;
  MultiPromiseActorSafe &operator=(MultiPromiseActorSafe &&other) = delete;
  ~MultiPromiseActorSafe() override;

 private:
  std::unique_ptr<MultiPromiseActor> multi_promise_ = std::make_unique<MultiPromiseActor>();
};

class MultiPromiseCreator {
 public:
  static MultiPromise create() {
    return MultiPromise(std::make_unique<MultiPromiseActor>());
  }
};

}  // namespace td
