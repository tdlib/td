//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/telegram/net/NetType.h"

#include "td/utils/common.h"

namespace td {

class StateManager final : public Actor {
 public:
  enum class State : int32 { WaitingForNetwork, ConnectingToProxy, Connecting, Updating, Ready, Empty };

  class Callback {
   public:
    Callback() = default;
    Callback(const Callback &) = delete;
    Callback &operator=(const Callback &) = delete;
    virtual ~Callback() = default;
    virtual bool on_state(State state) {
      return true;
    }
    virtual bool on_network(NetType network_type, uint32 generation) {
      return true;
    }
    virtual bool on_online(bool is_online) {
      return true;
    }
  };

  explicit StateManager(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void on_synchronized(bool is_synchronized);

  void on_network_updated();

  void on_network(NetType new_network_type);

  void on_online(bool is_online);

  void on_proxy(bool use_proxy);

  void add_callback(unique_ptr<Callback> net_callback);

  void wait_first_sync(Promise<> promise);

  void close();

  class ConnectionToken {
   public:
    ConnectionToken() = default;
    explicit ConnectionToken(ActorShared<StateManager> state_manager) : state_manager_(std::move(state_manager)) {
    }
    ConnectionToken(const ConnectionToken &) = delete;
    ConnectionToken &operator=(const ConnectionToken &) = delete;
    ConnectionToken(ConnectionToken &&) = default;
    ConnectionToken &operator=(ConnectionToken &&other) {
      reset();
      state_manager_ = std::move(other.state_manager_);
      return *this;
    }
    ~ConnectionToken() {
      reset();
    }

    void reset() {
      if (!state_manager_.empty()) {
        send_closure(state_manager_, &StateManager::dec_connect);
        state_manager_.reset();
      }
    }

    bool empty() const {
      return state_manager_.empty();
    }

   private:
    ActorShared<StateManager> state_manager_;
  };

  static ConnectionToken connection(ActorId<StateManager> state_manager) {
    return connection_impl(state_manager, 1);
  }
  static ConnectionToken connection_proxy(ActorId<StateManager> state_manager) {
    return connection_impl(state_manager, 2);
  }

 private:
  ActorShared<> parent_;
  uint32 connect_cnt_ = 0;
  uint32 connect_proxy_cnt_ = 0;
  bool sync_flag_ = true;
  bool network_flag_ = true;
  NetType network_type_ = NetType::Unknown;
  uint32 network_generation_ = 1;
  bool online_flag_ = false;
  bool use_proxy_ = false;

  static constexpr double UP_DELAY = 0.05;
  static constexpr double DOWN_DELAY = 0.3;

  State pending_state_ = State::Empty;
  bool has_timestamp_ = false;
  double pending_timestamp_ = 0;
  State flush_state_ = State::Empty;

  vector<unique_ptr<Callback>> callbacks_;

  bool was_sync_ = false;
  std::vector<Promise<>> wait_first_sync_;

  void inc_connect();
  void dec_connect();

  enum class Flag : int32 { Online, State, Network };
  void notify_flag(Flag flag);

  void start_up() override;
  void loop() override;

  void on_network_soft();
  void do_on_network(NetType new_network_type, bool inc_generation);

  State get_real_state() const;

  static ConnectionToken connection_impl(ActorId<StateManager> state_manager, int mode) {
    auto actor = ActorShared<StateManager>(state_manager, mode);
    send_closure(actor, &StateManager::inc_connect);
    return ConnectionToken(std::move(actor));
  }
};

}  // namespace td
