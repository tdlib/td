//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Client.h"

#include "td/telegram/Td.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/port/thread.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>

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

class MultiTd : public Actor {
 public:
  void create(int32 td_id, unique_ptr<TdCallback> callback) {
    auto &td = tds_[td_id];
    CHECK(td.empty());

    string name = "Td";
    class TdActorContext : public ActorContext {
     public:
      explicit TdActorContext(std::string tag) : tag_(std::move(tag)) {
      }
      int32 get_id() const override {
        return 0x172ae58d;
      }
      std::string tag_;
    };
    auto context = std::make_shared<TdActorContext>(to_string(td_id));
    auto old_context = set_context(context);
    auto old_tag = set_tag(context->tag_);
    td = create_actor<Td>("Td", std::move(callback));
    set_context(old_context);
    set_tag(old_tag);
  }
  void send(int32 td_id, Client::Request request) {
    auto &td = tds_[td_id];
    CHECK(!td.empty());
    send_closure(td, &Td::request, request.id, std::move(request.function));
  }
  void destroy(int32 td_id) {
    auto size = tds_.erase(td_id);
    CHECK(size == 1);
  }

 private:
  std::unordered_map<int32, ActorOwn<Td> > tds_;
};

class MultiImpl {
 public:
  static std::shared_ptr<MultiImpl> get() {
    static std::mutex mutex;
    static std::vector<std::weak_ptr<MultiImpl> > impls;
    std::unique_lock<std::mutex> lock(mutex);
    if (impls.size() == 0) {
      impls.resize(clamp(thread::hardware_concurrency(), 8u, 1000u) * 5 / 4);
    }
    auto &impl = *std::min_element(impls.begin(), impls.end(),
                                   [](auto &a, auto &b) { return a.lock().use_count() < b.lock().use_count(); });
    auto res = impl.lock();
    if (!res) {
      res = std::make_shared<MultiImpl>();
      impl = res;
    }
    return res;
  }

  MultiImpl() {
    concurrent_scheduler_ = std::make_shared<ConcurrentScheduler>();
    concurrent_scheduler_->init(3);
    concurrent_scheduler_->start();

    {
      auto guard = concurrent_scheduler_->get_main_guard();
      multi_td_ = create_actor<MultiTd>("MultiTd");
    }

    scheduler_thread_ = thread([concurrent_scheduler = concurrent_scheduler_] {
      while (concurrent_scheduler->run_main(10)) {
      }
    });
  }
  MultiImpl(const MultiImpl &) = delete;
  MultiImpl &operator=(const MultiImpl &) = delete;
  MultiImpl(MultiImpl &&) = delete;
  MultiImpl &operator=(MultiImpl &&) = delete;

  int32 create_id() {
    static std::atomic<int32> id_{0};
    return id_.fetch_add(1) + 1;
  }

  void create(int32 td_id, unique_ptr<TdCallback> callback) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::create, td_id, std::move(callback));
  }

  void send(int32 td_id, Client::Request request) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::send, td_id, std::move(request));
  }

  void destroy(int32 td_id) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::destroy, td_id);
  }

  ~MultiImpl() {
    {
      auto guard = concurrent_scheduler_->get_send_guard();
      multi_td_.reset();
      Scheduler::instance()->finish();
    }
    scheduler_thread_.join();
    concurrent_scheduler_->finish();
  }

 private:
  std::shared_ptr<ConcurrentScheduler> concurrent_scheduler_;
  thread scheduler_thread_;
  ActorOwn<MultiTd> multi_td_;
};

class Client::Impl final {
 public:
  using OutputQueue = MpscPollableQueue<Client::Response>;
  Impl() {
    multi_impl_ = MultiImpl::get();
    td_id_ = multi_impl_->create_id();
    output_queue_ = std::make_shared<OutputQueue>();
    output_queue_->init();

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
        output_queue_->writer_put({0, nullptr});
      }

     private:
      std::shared_ptr<OutputQueue> output_queue_;
    };

    multi_impl_->create(td_id_, td::make_unique<Callback>(output_queue_));
  }

  void send(Client::Request request) {
    if (request.id == 0 || request.function == nullptr) {
      LOG(ERROR) << "Drop wrong request " << request.id;
      return;
    }

    multi_impl_->send(td_id_, std::move(request));
  }

  Client::Response receive(double timeout) {
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
    multi_impl_->destroy(td_id_);
    while (!is_closed_) {
      receive(10);
    }
  }

 private:
  std::shared_ptr<MultiImpl> multi_impl_;

  std::shared_ptr<OutputQueue> output_queue_;
  int output_queue_ready_cnt_{0};
  std::atomic<bool> receive_lock_{false};
  bool is_closed_{false};
  int32 td_id_;

  Client::Response receive_unlocked(double timeout) {
    if (output_queue_ready_cnt_ == 0) {
      output_queue_ready_cnt_ = output_queue_->reader_wait_nonblock();
    }
    if (output_queue_ready_cnt_ > 0) {
      output_queue_ready_cnt_--;
      auto res = output_queue_->reader_get_unsafe();
      if (res.object == nullptr && res.id == 0) {
        is_closed_ = true;
      }
      return res;
    }
    if (timeout != 0) {
      output_queue_->reader_get_event_fd().wait(static_cast<int>(timeout * 1000));
      return receive_unlocked(0);
    }
    return {0, nullptr};
  }
};
#endif

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
