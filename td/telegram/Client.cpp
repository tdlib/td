//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Client.h"

#include "td/telegram/Td.h"

#include "td/actor/actor.h"

#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/port/Fd.h"
#include "td/utils/port/Poll.h"
#include "td/utils/port/thread.h"

#include <atomic>
#include <deque>

namespace td {

#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED

class Client::Impl final {
 public:
  Impl() {
    init();
  }

  void send(Request request) {
    if (request.id == 0 || request.function == nullptr) {
      LOG(ERROR) << "Drop wrong request " << request.id;
      return;
    }

    requests_.push_back(std::move(request));
  }

  Response receive(double timeout) {
    if (!requests_.empty()) {
      auto guard = scheduler_->get_current_guard();
      for (auto &request : requests_) {
        send_closure_later(td_, &Td::request, request.id, std::move(request.function));
      }
      requests_.clear();
    }

    if (responses_.empty()) {
      scheduler_->run_main(0);
    }
    if (!responses_.empty()) {
      auto result = std::move(responses_.front());
      responses_.pop_front();
      return result;
    }
    return {0, nullptr};
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    {
      auto guard = scheduler_->get_current_guard();
      td_.reset();
    }
    while (!closed_) {
      scheduler_->run_main(0);
    }
    scheduler_.reset();
  }

 private:
  std::deque<Response> responses_;
  std::vector<Request> requests_;
  std::unique_ptr<ConcurrentScheduler> scheduler_;
  ActorOwn<Td> td_;
  bool closed_ = false;

  void init() {
    scheduler_ = std::make_unique<ConcurrentScheduler>();
    scheduler_->init(0);
    class Callback : public TdCallback {
     public:
      explicit Callback(Impl *client) : client_(client) {
      }
      void on_result(std::uint64_t id, td_api::object_ptr<td_api::Object> result) override {
        client_->responses_.push_back({id, std::move(result)});
      }
      void on_error(std::uint64_t id, td_api::object_ptr<td_api::error> error) override {
        client_->responses_.push_back({id, std::move(error)});
      }
      void on_closed() override {
        client_->closed_ = true;
        Scheduler::instance()->yield();
      }

     private:
      Impl *client_;
    };
    td_ = scheduler_->create_actor_unsafe<Td>(0, "Td", make_unique<Callback>(this));
    scheduler_->start();
  }
};

#else

/*** TdProxy ***/
using InputQueue = MpscPollableQueue<Client::Request>;
using OutputQueue = MpscPollableQueue<Client::Response>;
class TdProxy : public Actor {
 public:
  TdProxy(std::shared_ptr<InputQueue> input_queue, std::shared_ptr<OutputQueue> output_queue)
      : input_queue_(std::move(input_queue)), output_queue_(std::move(output_queue)) {
  }

 private:
  std::shared_ptr<InputQueue> input_queue_;
  std::shared_ptr<OutputQueue> output_queue_;
  bool is_td_closed_ = false;
  bool was_hangup_ = false;
  ActorOwn<Td> td_;

  void start_up() override {
    auto &fd = input_queue_->reader_get_event_fd();
    fd.get_fd().set_observer(this);
    ::td::subscribe(fd.get_fd(), Fd::Read);

    class Callback : public TdCallback {
     public:
      Callback(ActorId<TdProxy> parent, std::shared_ptr<OutputQueue> output_queue)
          : parent_(parent), output_queue_(std::move(output_queue)) {
      }
      void on_result(std::uint64_t id, td_api::object_ptr<td_api::Object> result) override {
        output_queue_->writer_put({id, std::move(result)});
      }
      void on_error(std::uint64_t id, td_api::object_ptr<td_api::error> error) override {
        output_queue_->writer_put({id, std::move(error)});
      }
      void on_closed() override {
        send_closure(parent_, &TdProxy::on_closed);
      }

     private:
      ActorId<TdProxy> parent_;
      std::shared_ptr<OutputQueue> output_queue_;
    };
    td_ = create_actor<Td>("Td", make_unique<Callback>(actor_id(this), std::move(output_queue_)));
    yield();
  }

  void on_closed() {
    is_td_closed_ = true;
    try_stop();
  }

  void try_stop() {
    if (!is_td_closed_ || !was_hangup_) {
      return;
    }
    Scheduler::instance()->finish();
    stop();
  }

  void loop() override {
    while (true) {
      int size = input_queue_->reader_wait_nonblock();
      if (size == 0) {
        return;
      }
      for (int i = 0; i < size; i++) {
        auto request = input_queue_->reader_get_unsafe();
        if (request.id == 0 && request.function == nullptr) {
          was_hangup_ = true;
          td_.reset();
          return try_stop();
        }
        send_closure_later(td_, &Td::request, request.id, std::move(request.function));
      }
    }
  }

  void hangup() override {
    UNREACHABLE();
  }

  void tear_down() override {
    auto &fd = input_queue_->reader_get_event_fd();
    ::td::unsubscribe(fd.get_fd());
    fd.get_fd().set_observer(nullptr);
  }
};

/*** Client::Impl ***/
class Client::Impl final {
 public:
  Impl() {
    init();
  }

  void send(Request request) {
    if (request.id == 0 || request.function == nullptr) {
      LOG(ERROR) << "Drop wrong request " << request.id;
      return;
    }

    input_queue_->writer_put(std::move(request));
  }

  Response receive(double timeout) {
    auto is_locked = receive_lock_.exchange(true);
    CHECK(!is_locked);
    auto response = receive_unlocked(timeout);
    is_locked = receive_lock_.exchange(false);
    CHECK(is_locked);
    return response;
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    input_queue_->writer_put({0, nullptr});
    scheduler_thread_.join();
  }

 private:
  Poll poll_;
  std::shared_ptr<InputQueue> input_queue_;
  std::shared_ptr<OutputQueue> output_queue_;
  std::shared_ptr<ConcurrentScheduler> scheduler_;
  int output_queue_ready_cnt_{0};
  thread scheduler_thread_;
  std::atomic<bool> receive_lock_{false};

  void init() {
    input_queue_ = std::make_shared<InputQueue>();
    input_queue_->init();
    output_queue_ = std::make_shared<OutputQueue>();
    output_queue_->init();
    scheduler_ = std::make_shared<ConcurrentScheduler>();
    scheduler_->init(3);
    scheduler_->create_actor_unsafe<TdProxy>(0, "TdProxy", input_queue_, output_queue_).release();
    scheduler_->start();

    scheduler_thread_ = thread([scheduler = scheduler_] {
      while (scheduler->run_main(10)) {
      }
      scheduler->finish();
    });

    poll_.init();
    auto &event_fd = output_queue_->reader_get_event_fd();
    poll_.subscribe(event_fd.get_fd(), Fd::Read);
  }

  Response receive_unlocked(double timeout) {
    if (output_queue_ready_cnt_ == 0) {
      output_queue_ready_cnt_ = output_queue_->reader_wait_nonblock();
    }
    if (output_queue_ready_cnt_ > 0) {
      output_queue_ready_cnt_--;
      return output_queue_->reader_get_unsafe();
    }
    if (timeout != 0) {
      poll_.run(static_cast<int>(timeout * 1000));
      return receive_unlocked(0);
    }
    return {0, nullptr};
  }
};
#endif

/*** Client ***/
Client::Client() : impl_(make_unique<Impl>()) {
  // At least it should be enough for everybody who uses TDLib
  init_openssl_threads();
}

void Client::send(Request &&request) {
  impl_->send(std::move(request));
}

Client::Response Client::receive(double timeout) {
  return impl_->receive(timeout);
}

Client::Response Client::execute(Request &&request) {
  Response response;
  response.id = request.id;
  response.object = Td::static_request(std::move(request.function));
  return response;
}

Client::~Client() = default;
Client::Client(Client &&other) = default;
Client &Client::operator=(Client &&other) = default;

}  // namespace td
