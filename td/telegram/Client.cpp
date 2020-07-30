//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Client.h"

#include "td/telegram/Td.h"
#include "td/telegram/TdCallback.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/MpscPollableQueue.h"
#include "td/utils/port/RwMutex.h"
#include "td/utils/port/thread.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

namespace td {

class MultiTd : public Actor {
 public:
  explicit MultiTd(Td::Options options) : options_(std::move(options)) {
  }
  void create(int32 td_id, unique_ptr<TdCallback> callback) {
    auto &td = tds_[td_id];
    CHECK(td.empty());

    string name = "Td";
    class TdActorContext : public ActorContext {
     public:
      explicit TdActorContext(string tag) : tag_(std::move(tag)) {
      }
      int32 get_id() const override {
        return 0x172ae58d;
      }
      string tag_;
    };
    auto context = std::make_shared<TdActorContext>(to_string(td_id));
    auto old_context = set_context(context);
    auto old_tag = set_tag(context->tag_);
    td = create_actor<Td>("Td", std::move(callback), options_);
    set_context(old_context);
    set_tag(old_tag);
  }

  void send(MultiClient::ClientId client_id, MultiClient::RequestId request_id, MultiClient::Function function) {
    auto &td = tds_[client_id];
    CHECK(!td.empty());
    send_closure(td, &Td::request, request_id, std::move(function));
  }

  void destroy(int32 td_id) {
    auto size = tds_.erase(td_id);
    CHECK(size == 1);
  }

 private:
  Td::Options options_;
  std::unordered_map<int32, ActorOwn<Td>> tds_;
};

#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
class TdReceiver {
 public:
  MultiClient::Response receive(double timeout) {
    if (!responses_.empty()) {
      auto result = std::move(responses_.front());
      responses_.pop_front();
      return result;
    }
    return {0, 0, nullptr};
  }

  unique_ptr<TdCallback> create_callback(MultiClient::ClientId client_id) {
    class Callback : public TdCallback {
     public:
      Callback(MultiClient::ClientId client_id, TdReceiver *impl) : client_id_(client_id), impl_(impl) {
      }
      void on_result(uint64 id, td_api::object_ptr<td_api::Object> result) override {
        impl_->responses_.push_back({client_id_, id, std::move(result)});
      }
      void on_error(uint64 id, td_api::object_ptr<td_api::error> error) override {
        impl_->responses_.push_back({client_id_, id, std::move(error)});
      }
      Callback(const Callback &) = delete;
      Callback &operator=(const Callback &) = delete;
      Callback(Callback &&) = delete;
      Callback &operator=(Callback &&) = delete;
      ~Callback() override {
        impl_->responses_.push_back({client_id_, 0, nullptr});
      }

     private:
      MultiClient::ClientId client_id_;
      TdReceiver *impl_;
    };
    return td::make_unique<Callback>(client_id, this);
  }

 private:
  std::queue<MultiClient::Response> responses_;
};

class MultiClient::Impl final {
 public:
  Impl() {
    options_.net_query_stats = std::make_shared<NetQueryStats>();
    concurrent_scheduler_ = make_unique<ConcurrentScheduler>();
    concurrent_scheduler_->init(0);
    receiver_ = make_unique<TdReceiver>();
    concurrent_scheduler_->start();
  }

  ClientId create_client() {
    auto client_id = ++client_id_;
    tds_[client_id] =
        concurrent_scheduler_->create_actor_unsafe<Td>(0, "Td", receiver_->create_callback(client_id), options_);
    return client_id;
  }

  void send(ClientId client_id, RequestId request_id, Function function) {
    Request request;
    request.client_id = client_id;
    request.id = request_id;
    request.function = std::move(function);
    requests_.push_back(std::move(request));
  }

  Response receive(double timeout) {
    if (!requests_.empty()) {
      auto guard = concurrent_scheduler_->get_main_guard();
      for (auto &request : requests_) {
        auto &td = tds_[request.client_id];
        CHECK(!td.empty());
        send_closure_later(td, &Td::request, request.id, std::move(request.function));
      }
      requests_.clear();
    }

    auto response = receiver_->receive(0);
    if (response.client_id == 0) {
      concurrent_scheduler_->run_main(0);
      response = receiver_->receive(0);
    } else {
      ConcurrentScheduler::emscripten_clear_main_timeout();
    }
    if (response.client_id != 0 && !response.object) {
      auto guard = concurrent_scheduler_->get_main_guard();
      tds_.erase(response.client_id);
    }
    return response;
  }

  Impl() = default;
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    {
      auto guard = concurrent_scheduler_->get_main_guard();
      for (auto &td : tds_) {
        td.second = {};
      }
    }
    while (!tds_.empty()) {
      receive(10);
    }
    concurrent_scheduler_->finish();
  }

 private:
  unique_ptr<TdReceiver> receiver_;
  struct Request {
    ClientId client_id;
    RequestId id;
    Function function;
  };
  std::vector<Request> requests_;
  unique_ptr<ConcurrentScheduler> concurrent_scheduler_;
  ClientId client_id_{0};
  Td::Options options_;
  std::unordered_map<int32, ActorOwn<Td>> tds_;
};

class Client::Impl final {
 public:
  Impl() {
    client_id_ = impl_.create_client();
  }

  void send(Request request) {
    impl_.send(client_id_, request.id, std::move(request.function));
  }

  Response receive(double timeout) {
    auto response = impl_.receive(timeout);
    Response old_response;
    old_response.id = response.id;
    old_response.object = std::move(response.object);
    return old_response;
  }

 private:
  MultiClient::Impl impl_;
  MultiClient::ClientId client_id_;
};

#else

class TdReceiver {
 public:
  TdReceiver() {
    output_queue_ = std::make_shared<OutputQueue>();
    output_queue_->init();
  }

  MultiClient::Response receive(double timeout) {
    VLOG(td_requests) << "Begin to wait for updates with timeout " << timeout;
    auto is_locked = receive_lock_.exchange(true);
    CHECK(!is_locked);
    auto response = receive_unlocked(timeout);
    is_locked = receive_lock_.exchange(false);
    CHECK(is_locked);
    VLOG(td_requests) << "End to wait for updates, returning object " << response.id << ' ' << response.object.get();
    return response;
  }

  unique_ptr<TdCallback> create_callback(MultiClient::ClientId client_id) {
    class Callback : public TdCallback {
     public:
      explicit Callback(MultiClient::ClientId client_id, std::shared_ptr<OutputQueue> output_queue)
          : client_id_(client_id), output_queue_(std::move(output_queue)) {
      }
      void on_result(uint64 id, td_api::object_ptr<td_api::Object> result) override {
        output_queue_->writer_put({client_id_, id, std::move(result)});
      }
      void on_error(uint64 id, td_api::object_ptr<td_api::error> error) override {
        output_queue_->writer_put({client_id_, id, std::move(error)});
      }
      Callback(const Callback &) = delete;
      Callback &operator=(const Callback &) = delete;
      Callback(Callback &&) = delete;
      Callback &operator=(Callback &&) = delete;
      ~Callback() override {
        output_queue_->writer_put({client_id_, 0, nullptr});
      }

     private:
      MultiClient::ClientId client_id_;
      std::shared_ptr<OutputQueue> output_queue_;
    };
    return td::make_unique<Callback>(client_id, output_queue_);
  }

 private:
  using OutputQueue = MpscPollableQueue<MultiClient::Response>;
  std::shared_ptr<OutputQueue> output_queue_;
  int output_queue_ready_cnt_{0};
  std::atomic<bool> receive_lock_{false};

  MultiClient::Response receive_unlocked(double timeout) {
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
    return {0, 0, nullptr};
  }
};

class MultiImpl {
 public:
  explicit MultiImpl(std::shared_ptr<NetQueryStats> net_query_stats) {
    concurrent_scheduler_ = std::make_shared<ConcurrentScheduler>();
    concurrent_scheduler_->init(3);
    concurrent_scheduler_->start();

    {
      auto guard = concurrent_scheduler_->get_main_guard();
      Td::Options options;
      options.net_query_stats = std::move(net_query_stats);
      multi_td_ = create_actor<MultiTd>("MultiTd", std::move(options));
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

  int32 create(TdReceiver &receiver) {
    auto id = create_id();
    create(id, receiver.create_callback(id));
    return id;
  }

  void send(MultiClient::ClientId client_id, MultiClient::RequestId request_id, MultiClient::Function function) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::send, client_id, request_id, std::move(function));
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

  static int32 create_id() {
    static std::atomic<int32> current_id{1};
    return current_id.fetch_add(1);
  }

  void create(int32 td_id, unique_ptr<TdCallback> callback) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::create, td_id, std::move(callback));
  }
};

class MultiImplPool {
 public:
  std::shared_ptr<MultiImpl> get() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (impls_.empty()) {
      init_openssl_threads();

      impls_.resize(clamp(thread::hardware_concurrency(), 8u, 1000u) * 5 / 4);
    }
    auto &impl = *std::min_element(impls_.begin(), impls_.end(),
                                   [](auto &a, auto &b) { return a.lock().use_count() < b.lock().use_count(); });
    auto res = impl.lock();
    if (!res) {
      res = std::make_shared<MultiImpl>(net_query_stats_);
      impl = res;
    }
    return res;
  }

 private:
  std::mutex mutex_;
  std::vector<std::weak_ptr<MultiImpl>> impls_;
  std::shared_ptr<NetQueryStats> net_query_stats_ = std::make_shared<NetQueryStats>();
};

class MultiClient::Impl final {
 public:
  ClientId create_client() {
    auto impl = pool_.get();
    auto client_id = impl->create(*receiver_);
    {
      auto lock = impls_mutex_.lock_write().move_as_ok();
      impls_[client_id] = std::move(impl);
    }
    return client_id;
  }

  void send(ClientId client_id, RequestId request_id, Function function) {
    auto lock = impls_mutex_.lock_read().move_as_ok();
    auto it = impls_.find(client_id);
    CHECK(it != impls_.end());
    it->second->send(client_id, request_id, std::move(function));
  }

  Response receive(double timeout) {
    auto res = receiver_->receive(timeout);
    if (res.client_id != 0 && !res.object) {
      auto lock = impls_mutex_.lock_write().move_as_ok();
      impls_.erase(res.client_id);
    }
    return res;
  }

  Impl() = default;
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    for (auto &it : impls_) {
      it.second->destroy(it.first);
    }
    while (!impls_.empty()) {
      receive(10);
    }
  }

 private:
  MultiImplPool pool_;
  RwMutex impls_mutex_;
  std::unordered_map<ClientId, std::shared_ptr<MultiImpl>> impls_;
  unique_ptr<TdReceiver> receiver_{make_unique<TdReceiver>()};
};

class Client::Impl final {
 public:
  Impl() {
    static MultiImplPool pool;
    multi_impl_ = pool.get();
    receiver_ = make_unique<TdReceiver>();
    td_id_ = multi_impl_->create(*receiver_);
  }

  void send(Client::Request request) {
    if (request.id == 0 || request.function == nullptr) {
      LOG(ERROR) << "Drop wrong request " << request.id;
      return;
    }

    multi_impl_->send(td_id_, request.id, std::move(request.function));
  }

  Client::Response receive(double timeout) {
    auto res = receiver_->receive(timeout);

    if (res.client_id != 0 && !res.object) {
      is_closed_ = true;
    }

    Client::Response old_res;
    old_res.id = res.id;
    old_res.object = std::move(res.object);
    return old_res;
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
  unique_ptr<TdReceiver> receiver_;

  bool is_closed_{false};
  int32 td_id_;
};
#endif

Client::Client() : impl_(std::make_unique<Impl>()) {
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

MultiClient::MultiClient() : impl_(std::make_unique<Impl>()) {
}

MultiClient::ClientId MultiClient::create_client() {
  return impl_->create_client();
}

void MultiClient::send(ClientId client_id, RequestId request_id, Function &&function) {
  impl_->send(client_id, request_id, std::move(function));
}

MultiClient::Response MultiClient::receive(double timeout) {
  return impl_->receive(timeout);
}

MultiClient::Object MultiClient::execute(Function &&function) {
  return Td::static_request(std::move(function));
}

MultiClient::~MultiClient() = default;
MultiClient::MultiClient(MultiClient &&other) = default;
MultiClient &MultiClient::operator=(MultiClient &&other) = default;

}  // namespace td
