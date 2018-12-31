//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
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
#include "td/utils/port/thread.h"

#include <atomic>
#include <deque>

namespace td {

#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED

class Client::Impl final {
 public:
  Impl() {
    concurrent_scheduler_ = make_unique<ConcurrentScheduler>();
    concurrent_scheduler_->init(0);
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

      Callback(const Callback &) = delete;
      Callback &operator=(const Callback &) = delete;
      Callback(Callback &&) = delete;
      Callback &operator=(Callback &&) = delete;
      ~Callback() override {
        client_->closed_ = true;
        Scheduler::instance()->yield();
      }

     private:
      Impl *client_;
    };
    td_ = concurrent_scheduler_->create_actor_unsafe<Td>(0, "Td", make_unique<Callback>(this));
    concurrent_scheduler_->start();
  }

  void send(Request request) {
    requests_.push_back(std::move(request));
  }

  Response receive(double timeout) {
    if (!requests_.empty()) {
      auto guard = concurrent_scheduler_->get_main_guard();
      for (auto &request : requests_) {
        send_closure_later(td_, &Td::request, request.id, std::move(request.function));
      }
      requests_.clear();
    }

    if (responses_.empty()) {
      concurrent_scheduler_->run_main(0);
    } else {
      ConcurrentScheduler::emscripten_clear_main_timeout();
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
      auto guard = concurrent_scheduler_->get_main_guard();
      td_.reset();
    }
    while (!closed_) {
      concurrent_scheduler_->run_main(0);
    }
    concurrent_scheduler_.reset();
  }

 private:
  std::deque<Response> responses_;
  std::vector<Request> requests_;
  unique_ptr<ConcurrentScheduler> concurrent_scheduler_;
  ActorOwn<Td> td_;
  bool closed_ = false;
};

#else

using OutputQueue = MpscPollableQueue<Client::Response>;

/*** Client::Impl ***/
class Client::Impl final {
 public:
  Impl() {
    output_queue_ = std::make_shared<OutputQueue>();
    output_queue_->init();
    concurrent_scheduler_ = std::make_shared<ConcurrentScheduler>();
    concurrent_scheduler_->init(3);
    class Callback : public TdCallback {
     public:
      explicit Callback(std::shared_ptr<OutputQueue> output_queue) : output_queue_(std::move(output_queue)) {
      }
      void on_result(std::uint64_t id, td_api::object_ptr<td_api::Object> result) override {
        output_queue_->writer_put({id, std::move(result)});
      }
      void on_error(std::uint64_t id, td_api::object_ptr<td_api::error> error) override {
        output_queue_->writer_put({id, std::move(error)});
      }
      Callback(const Callback &) = delete;
      Callback &operator=(const Callback &) = delete;
      Callback(Callback &&) = delete;
      Callback &operator=(Callback &&) = delete;
      ~Callback() override {
        Scheduler::instance()->finish();
      }

     private:
      std::shared_ptr<OutputQueue> output_queue_;
    };
    td_ = concurrent_scheduler_->create_actor_unsafe<Td>(0, "Td", td::make_unique<Callback>(output_queue_));
    concurrent_scheduler_->start();

    scheduler_thread_ = thread([concurrent_scheduler = concurrent_scheduler_] {
      while (concurrent_scheduler->run_main(10)) {
      }
      concurrent_scheduler->finish();
    });
  }

  void send(Request request) {
    if (request.id == 0 || request.function == nullptr) {
      LOG(ERROR) << "Drop wrong request " << request.id;
      return;
    }

    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(td_, &Td::request, request.id, std::move(request.function));
  }

  Response receive(double timeout) {
    VLOG(td_requests) << "Begin to wait for updates with timeout " << timeout;
    auto is_locked = receive_lock_.exchange(true);
    CHECK(!is_locked);
    auto response = receive_unlocked(timeout);
    is_locked = receive_lock_.exchange(false);
    CHECK(is_locked);
    VLOG(td_requests) << "End to wait for updates, returning object " << response.id << ' ' << response.object.get();
    return response;
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    auto guard = concurrent_scheduler_->get_send_guard();
    td_.reset();
    scheduler_thread_.join();
  }

 private:
  std::shared_ptr<OutputQueue> output_queue_;
  std::shared_ptr<ConcurrentScheduler> concurrent_scheduler_;
  int output_queue_ready_cnt_{0};
  thread scheduler_thread_;
  std::atomic<bool> receive_lock_{false};
  ActorOwn<Td> td_;

  Response receive_unlocked(double timeout) {
    if (output_queue_ready_cnt_ == 0) {
      output_queue_ready_cnt_ = output_queue_->reader_wait_nonblock();
    }
    if (output_queue_ready_cnt_ > 0) {
      output_queue_ready_cnt_--;
      return output_queue_->reader_get_unsafe();
    }
    if (timeout != 0) {
      output_queue_->reader_get_event_fd().wait(static_cast<int>(timeout * 1000));
      return receive_unlocked(0);
    }
    return {0, nullptr};
  }
};
#endif

/*** Client ***/
Client::Client() : impl_(std::make_unique<Impl>()) {
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
