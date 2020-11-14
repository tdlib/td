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
#include "td/utils/ExitGuard.h"
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

#if TD_THREAD_UNSUPPORTED || TD_EVENTFD_UNSUPPORTED
class TdReceiver {
 public:
  ClientManager::Response receive(double timeout) {
    if (!responses_.empty()) {
      auto result = std::move(responses_.front());
      responses_.pop();
      return result;
    }
    return {0, 0, nullptr};
  }

  unique_ptr<TdCallback> create_callback(ClientManager::ClientId client_id) {
    class Callback : public TdCallback {
     public:
      Callback(ClientManager::ClientId client_id, TdReceiver *impl) : client_id_(client_id), impl_(impl) {
      }
      void on_result(uint64 id, td_api::object_ptr<td_api::Object> result) override {
        impl_->responses_.push({client_id_, id, std::move(result)});
      }
      void on_error(uint64 id, td_api::object_ptr<td_api::error> error) override {
        impl_->responses_.push({client_id_, id, std::move(error)});
      }
      Callback(const Callback &) = delete;
      Callback &operator=(const Callback &) = delete;
      Callback(Callback &&) = delete;
      Callback &operator=(Callback &&) = delete;
      ~Callback() override {
        impl_->responses_.push({client_id_, 0, nullptr});
      }

     private:
      ClientManager::ClientId client_id_;
      TdReceiver *impl_;
    };
    return td::make_unique<Callback>(client_id, this);
  }

  void add_response(ClientManager::ClientId client_id, uint64 id, td_api::object_ptr<td_api::Object> result) {
    responses_.push({client_id, id, std::move(result)});
  }

 private:
  std::queue<ClientManager::Response> responses_;
};

class ClientManager::Impl final {
 public:
  ClientId create_client_id() {
    CHECK(client_id_ != std::numeric_limits<ClientId>::max());
    auto client_id = ++client_id_;
    pending_clients_.insert(client_id);
    return client_id;
  }

  void send(ClientId client_id, RequestId request_id, td_api::object_ptr<td_api::Function> &&request) {
    if (pending_clients_.erase(client_id) != 0) {
      if (tds_.empty()) {
        CHECK(concurrent_scheduler_ == nullptr);
        CHECK(options_.net_query_stats == nullptr);
        options_.net_query_stats = std::make_shared<NetQueryStats>();
        concurrent_scheduler_ = make_unique<ConcurrentScheduler>();
        concurrent_scheduler_->init(0);
        concurrent_scheduler_->start();
      }
      tds_[client_id] =
          concurrent_scheduler_->create_actor_unsafe<Td>(0, "Td", receiver_.create_callback(client_id), options_);
    }
    requests_.push_back({client_id, request_id, std::move(request)});
  }

  Response receive(double timeout) {
    if (!requests_.empty()) {
      for (size_t i = 0; i < requests_.size(); i++) {
        auto &request = requests_[i];
        if (request.client_id <= 0 || request.client_id > client_id_) {
          receiver_.add_response(request.client_id, request.id,
                                 td_api::make_object<td_api::error>(400, "Invalid TDLib instance specified"));
          continue;
        }
        auto it = tds_.find(request.client_id);
        if (it == tds_.end() || it->second.empty()) {
          receiver_.add_response(request.client_id, request.id,
                                 td_api::make_object<td_api::error>(500, "Request aborted"));
          continue;
        }

        CHECK(concurrent_scheduler_ != nullptr);
        auto guard = concurrent_scheduler_->get_main_guard();
        send_closure_later(it->second, &Td::request, request.id, std::move(request.request));
      }
      requests_.clear();
    }

    auto response = receiver_.receive(0);
    if (response.client_id == 0 && concurrent_scheduler_ != nullptr) {
      concurrent_scheduler_->run_main(0);
      response = receiver_.receive(0);
    } else {
      ConcurrentScheduler::emscripten_clear_main_timeout();
    }
    if (response.request_id == 0 && response.object != nullptr &&
        response.object->get_id() == td_api::updateAuthorizationState::ID &&
        static_cast<const td_api::updateAuthorizationState *>(response.object.get())->authorization_state_->get_id() ==
            td_api::authorizationStateClosed::ID) {
      CHECK(concurrent_scheduler_ != nullptr);
      auto guard = concurrent_scheduler_->get_main_guard();
      auto it = tds_.find(response.client_id);
      CHECK(it != tds_.end());
      it->second.reset();

      response.client_id = 0;
      response.object = nullptr;
    }
    if (response.object == nullptr && response.client_id != 0 && response.request_id == 0) {
      auto it = tds_.find(response.client_id);
      CHECK(it != tds_.end());
      CHECK(it->second.empty());
      tds_.erase(it);

      response.object = td_api::make_object<td_api::updateAuthorizationState>(
          td_api::make_object<td_api::authorizationStateClosed>());

      if (tds_.empty()) {
        CHECK(options_.net_query_stats.use_count() == 1);
        CHECK(options_.net_query_stats->get_count() == 0);
        options_.net_query_stats = nullptr;
        concurrent_scheduler_->finish();
        concurrent_scheduler_ = nullptr;
        reset_to_empty(tds_);
      }
    }
    return response;
  }

  Impl() = default;
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    if (concurrent_scheduler_ == nullptr) {
      return;
    }

    {
      auto guard = concurrent_scheduler_->get_main_guard();
      for (auto &td : tds_) {
        td.second.reset();
      }
    }
    while (!tds_.empty() && !ExitGuard::is_exited()) {
      receive(0.1);
    }
    if (!ExitGuard::is_exited()) {  // prevent closing of schedulers from already killed by OS threads
      concurrent_scheduler_->finish();
    }
  }

 private:
  TdReceiver receiver_;
  struct Request {
    ClientId client_id;
    RequestId id;
    td_api::object_ptr<td_api::Function> request;
  };
  vector<Request> requests_;
  unique_ptr<ConcurrentScheduler> concurrent_scheduler_;
  ClientId client_id_{0};
  Td::Options options_;
  std::unordered_set<int32> pending_clients_;
  std::unordered_map<int32, ActorOwn<Td>> tds_;
};

class Client::Impl final {
 public:
  Impl() : client_id_(impl_.create_client_id()) {
  }

  void send(Request request) {
    impl_.send(client_id_, request.id, std::move(request.function));
  }

  Response receive(double timeout) {
    auto response = impl_.receive(timeout);

    Response old_response;
    old_response.id = response.request_id;
    old_response.object = std::move(response.object);
    return old_response;
  }

 private:
  ClientManager::Impl impl_;
  ClientManager::ClientId client_id_;
};

#else

class MultiTd : public Actor {
 public:
  explicit MultiTd(Td::Options options) : options_(std::move(options)) {
  }
  void create(int32 td_id, unique_ptr<TdCallback> callback) {
    auto &td = tds_[td_id];
    CHECK(td.empty());

    string name = "Td";
    auto context = std::make_shared<ActorContext>();
    auto old_context = set_context(context);
    auto old_tag = set_tag(to_string(td_id));
    td = create_actor<Td>("Td", std::move(callback), options_);
    set_context(old_context);
    set_tag(old_tag);
  }

  void send(ClientManager::ClientId client_id, ClientManager::RequestId request_id,
            td_api::object_ptr<td_api::Function> &&request) {
    auto &td = tds_[client_id];
    CHECK(!td.empty());
    send_closure(td, &Td::request, request_id, std::move(request));
  }

  void close(int32 td_id) {
    size_t erased_count = tds_.erase(td_id);
    CHECK(erased_count > 0);
  }

 private:
  Td::Options options_;
  std::unordered_map<int32, ActorOwn<Td>> tds_;
};

class TdReceiver {
 public:
  TdReceiver() {
    output_queue_ = std::make_shared<OutputQueue>();
    output_queue_->init();
  }

  ClientManager::Response receive(double timeout) {
    VLOG(td_requests) << "Begin to wait for updates with timeout " << timeout;
    auto is_locked = receive_lock_.exchange(true);
    if (is_locked) {
      LOG(FATAL) << "Receive is called after Client destroy, or simultaneously from different threads";
    }
    auto response = receive_unlocked(timeout);
    is_locked = receive_lock_.exchange(false);
    CHECK(is_locked);
    VLOG(td_requests) << "End to wait for updates, returning object " << response.request_id << ' '
                      << response.object.get();
    return response;
  }

  unique_ptr<TdCallback> create_callback(ClientManager::ClientId client_id) {
    class Callback : public TdCallback {
     public:
      explicit Callback(ClientManager::ClientId client_id, std::shared_ptr<OutputQueue> output_queue)
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
      ClientManager::ClientId client_id_;
      std::shared_ptr<OutputQueue> output_queue_;
    };
    return td::make_unique<Callback>(client_id, output_queue_);
  }

  void add_response(ClientManager::ClientId client_id, uint64 id, td_api::object_ptr<td_api::Object> result) {
    output_queue_->writer_put({client_id, id, std::move(result)});
  }

 private:
  using OutputQueue = MpscPollableQueue<ClientManager::Response>;
  std::shared_ptr<OutputQueue> output_queue_;
  int output_queue_ready_cnt_{0};
  std::atomic<bool> receive_lock_{false};

  ClientManager::Response receive_unlocked(double timeout) {
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

  static int32 create_id() {
    auto result = current_id_.fetch_add(1);
    CHECK(result <= static_cast<uint32>(std::numeric_limits<int32>::max()));
    return static_cast<int32>(result);
  }

  void create(int32 td_id, unique_ptr<TdCallback> callback) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::create, td_id, std::move(callback));
  }

  static bool is_valid_client_id(int32 client_id) {
    return client_id > 0 && static_cast<uint32>(client_id) < current_id_.load();
  }

  void send(ClientManager::ClientId client_id, ClientManager::RequestId request_id,
            td_api::object_ptr<td_api::Function> &&request) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::send, client_id, request_id, std::move(request));
  }

  void close(ClientManager::ClientId client_id) {
    auto guard = concurrent_scheduler_->get_send_guard();
    send_closure(multi_td_, &MultiTd::close, client_id);
  }

  ~MultiImpl() {
    {
      auto guard = concurrent_scheduler_->get_send_guard();
      multi_td_.reset();
      Scheduler::instance()->finish();
    }
    scheduler_thread_.join();
    if (!ExitGuard::is_exited()) {  // prevent closing of schedulers from already killed by OS threads
      concurrent_scheduler_->finish();
    }
  }

 private:
  std::shared_ptr<ConcurrentScheduler> concurrent_scheduler_;
  thread scheduler_thread_;
  ActorOwn<MultiTd> multi_td_;

  static std::atomic<uint32> current_id_;
};

std::atomic<uint32> MultiImpl::current_id_{1};

class MultiImplPool {
 public:
  std::shared_ptr<MultiImpl> get() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (impls_.empty()) {
      init_openssl_threads();

      impls_.resize(clamp(thread::hardware_concurrency(), 8u, 1000u) * 5 / 4);

      net_query_stats_ = std::make_shared<NetQueryStats>();
    }
    auto &impl = *std::min_element(impls_.begin(), impls_.end(),
                                   [](auto &a, auto &b) { return a.lock().use_count() < b.lock().use_count(); });
    auto result = impl.lock();
    if (!result) {
      result = std::make_shared<MultiImpl>(net_query_stats_);
      impl = result;
    }
    return result;
  }

  void try_clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    if (impls_.empty()) {
      return;
    }

    for (auto &impl : impls_) {
      if (impl.lock().use_count() != 0) {
        return;
      }
    }
    reset_to_empty(impls_);

    CHECK(net_query_stats_.use_count() == 1);
    CHECK(net_query_stats_->get_count() == 0);
    net_query_stats_ = nullptr;
  }

 private:
  std::mutex mutex_;
  std::vector<std::weak_ptr<MultiImpl>> impls_;
  std::shared_ptr<NetQueryStats> net_query_stats_;
};

class ClientManager::Impl final {
 public:
  ClientId create_client_id() {
    auto client_id = MultiImpl::create_id();
    {
      auto lock = impls_mutex_.lock_write().move_as_ok();
      impls_[client_id];  // create empty MultiImplInfo
    }
    return client_id;
  }

  void send(ClientId client_id, RequestId request_id, td_api::object_ptr<td_api::Function> &&request) {
    auto lock = impls_mutex_.lock_read().move_as_ok();
    if (!MultiImpl::is_valid_client_id(client_id)) {
      receiver_.add_response(client_id, request_id,
                             td_api::make_object<td_api::error>(400, "Invalid TDLib instance specified"));
      return;
    }

    auto it = impls_.find(client_id);
    if (it != impls_.end() && it->second.impl == nullptr) {
      lock.reset();

      auto write_lock = impls_mutex_.lock_write().move_as_ok();
      it = impls_.find(client_id);
      if (it != impls_.end() && it->second.impl == nullptr) {
        it->second.impl = pool_.get();
        it->second.impl->create(client_id, receiver_.create_callback(client_id));
      }
      write_lock.reset();

      lock = impls_mutex_.lock_read().move_as_ok();
      it = impls_.find(client_id);
    }
    if (it == impls_.end() || it->second.is_closed) {
      receiver_.add_response(client_id, request_id, td_api::make_object<td_api::error>(500, "Request aborted"));
      return;
    }
    it->second.impl->send(client_id, request_id, std::move(request));
  }

  Response receive(double timeout) {
    auto response = receiver_.receive(timeout);
    if (response.request_id == 0 && response.object != nullptr &&
        response.object->get_id() == td_api::updateAuthorizationState::ID &&
        static_cast<const td_api::updateAuthorizationState *>(response.object.get())->authorization_state_->get_id() ==
            td_api::authorizationStateClosed::ID) {
      auto lock = impls_mutex_.lock_write().move_as_ok();
      close_impl(response.client_id);

      response.client_id = 0;
      response.object = nullptr;
    }
    if (response.object == nullptr && response.client_id != 0 && response.request_id == 0) {
      auto lock = impls_mutex_.lock_write().move_as_ok();
      auto it = impls_.find(response.client_id);
      CHECK(it != impls_.end());
      CHECK(it->second.is_closed);
      impls_.erase(it);

      response.object = td_api::make_object<td_api::updateAuthorizationState>(
          td_api::make_object<td_api::authorizationStateClosed>());

      if (impls_.empty()) {
        reset_to_empty(impls_);
        pool_.try_clear();
      }
    }
    return response;
  }

  void close_impl(ClientId client_id) {
    auto it = impls_.find(client_id);
    CHECK(it != impls_.end());
    if (!it->second.is_closed) {
      it->second.is_closed = true;
      if (it->second.impl == nullptr) {
        receiver_.add_response(client_id, 0, nullptr);
      } else {
        it->second.impl->close(client_id);
      }
    }
  }

  Impl() = default;
  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    if (ExitGuard::is_exited()) {
      return;
    }
    for (auto &it : impls_) {
      close_impl(it.first);
    }
    while (!impls_.empty() && !ExitGuard::is_exited()) {
      receive(0.1);
    }
  }

 private:
  MultiImplPool pool_;
  RwMutex impls_mutex_;
  struct MultiImplInfo {
    std::shared_ptr<MultiImpl> impl;
    bool is_closed = false;
  };
  std::unordered_map<ClientId, MultiImplInfo> impls_;
  TdReceiver receiver_;
};

class Client::Impl final {
 public:
  Impl() {
    static MultiImplPool pool;
    multi_impl_ = pool.get();
    td_id_ = MultiImpl::create_id();
    multi_impl_->create(td_id_, receiver_.create_callback(td_id_));
  }

  void send(Request request) {
    if (request.id == 0 || request.function == nullptr) {
      LOG(ERROR) << "Drop wrong request " << request.id;
      return;
    }

    multi_impl_->send(td_id_, request.id, std::move(request.function));
  }

  Response receive(double timeout) {
    auto response = receiver_.receive(timeout);

    Response old_response;
    old_response.id = response.request_id;
    old_response.object = std::move(response.object);
    return old_response;
  }

  Impl(const Impl &) = delete;
  Impl &operator=(const Impl &) = delete;
  Impl(Impl &&) = delete;
  Impl &operator=(Impl &&) = delete;
  ~Impl() {
    multi_impl_->close(td_id_);
    while (!ExitGuard::is_exited()) {
      auto response = receiver_.receive(0.1);
      if (response.object == nullptr && response.client_id != 0 && response.request_id == 0) {
        break;
      }
    }
  }

 private:
  std::shared_ptr<MultiImpl> multi_impl_;
  TdReceiver receiver_;

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

ClientManager::ClientManager() : impl_(std::make_unique<Impl>()) {
}

ClientManager::ClientId ClientManager::create_client_id() {
  return impl_->create_client_id();
}

void ClientManager::send(ClientId client_id, RequestId request_id, td_api::object_ptr<td_api::Function> &&request) {
  impl_->send(client_id, request_id, std::move(request));
}

ClientManager::Response ClientManager::receive(double timeout) {
  return impl_->receive(timeout);
}

td_api::object_ptr<td_api::Object> ClientManager::execute(td_api::object_ptr<td_api::Function> &&request) {
  return Td::static_request(std::move(request));
}

ClientManager::~ClientManager() = default;
ClientManager::ClientManager(ClientManager &&other) = default;
ClientManager &ClientManager::operator=(ClientManager &&other) = default;

ClientManager *ClientManager::get_manager_singleton() {
  static ClientManager client_manager;
  static ExitGuard exit_guard;
  return &client_manager;
}

}  // namespace td
