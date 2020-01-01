//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StateManager.h"

#include "td/actor/PromiseFuture.h"
#include "td/actor/SleepActor.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Time.h"

namespace td {

void StateManager::inc_connect() {
  auto &cnt = get_link_token() == 1 ? connect_cnt_ : connect_proxy_cnt_;
  cnt++;
  if (cnt == 1) {
    loop();
  }
}
void StateManager::dec_connect() {
  auto &cnt = get_link_token() == 1 ? connect_cnt_ : connect_proxy_cnt_;
  CHECK(cnt > 0);
  cnt--;
  if (cnt == 0) {
    loop();
  }
}

void StateManager::on_synchronized(bool is_synchronized) {
  if (sync_flag_ != is_synchronized) {
    sync_flag_ = is_synchronized;
    loop();
  }
  if (sync_flag_ && !was_sync_) {
    was_sync_ = true;
    auto promises = std::move(wait_first_sync_);
    reset_to_empty(wait_first_sync_);
    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
  }
}

void StateManager::on_network_updated() {
  do_on_network(network_type_, true /*inc_generation*/);
}

void StateManager::on_network(NetType new_network_type) {
  do_on_network(new_network_type, true /*inc_generation*/);
}

void StateManager::do_on_network(NetType new_network_type, bool inc_generation) {
  bool new_network_flag = new_network_type != NetType::None;
  if (network_flag_ != new_network_flag) {
    network_flag_ = new_network_flag;
    loop();
  }
  network_type_ = new_network_type;
  if (inc_generation) {
    network_generation_++;
  }
  notify_flag(Flag::Network);
}

void StateManager::on_online(bool is_online) {
  online_flag_ = is_online;
  notify_flag(Flag::Online);
}

void StateManager::on_proxy(bool use_proxy) {
  use_proxy_ = use_proxy;
  on_network(network_type_);
  loop();
}

void StateManager::add_callback(unique_ptr<Callback> callback) {
  if (callback->on_network(network_type_, network_generation_) && callback->on_online(online_flag_) &&
      callback->on_state(get_real_state())) {
    callbacks_.push_back(std::move(callback));
  }
}
void StateManager::wait_first_sync(Promise<> promise) {
  if (was_sync_) {
    return promise.set_value(Unit());
  }
  wait_first_sync_.push_back(std::move(promise));
}

void StateManager::close() {
  stop();
}

StateManager::State StateManager::get_real_state() const {
  if (!network_flag_) {
    return State::WaitingForNetwork;
  }
  if (!connect_cnt_) {
    if (use_proxy_ && !connect_proxy_cnt_) {
      return State::ConnectingToProxy;
    }
    return State::Connecting;
  }
  if (!sync_flag_) {
    return State::Updating;
  }
  return State::Ready;
}

void StateManager::notify_flag(Flag flag) {
  for (auto it = callbacks_.begin(); it != callbacks_.end();) {
    bool ok = [&] {
      switch (flag) {
        case Flag::Online:
          return (*it)->on_online(online_flag_);
        case Flag::State:
          return (*it)->on_state(flush_state_);
        case Flag::Network:
          return (*it)->on_network(network_type_, network_generation_);
        default:
          UNREACHABLE();
          return true;
      }
    }();
    if (ok) {
      ++it;
    } else {
      it = callbacks_.erase(it);
    }
  }
}

void StateManager::on_network_soft() {
  if (network_type_ == NetType::Unknown) {
    LOG(INFO) << "Auto set net_type = Other";
    do_on_network(NetType::Other, false /*inc_generation*/);
  }
}

void StateManager::start_up() {
  create_actor<SleepActor>("SleepActor", 1, PromiseCreator::event(self_closure(this, &StateManager::on_network_soft)))
      .release();
  loop();
}

void StateManager::loop() {
  auto now = Time::now();
  auto state = get_real_state();
  if (state != pending_state_) {
    pending_state_ = state;
    if (!has_timestamp_) {
      pending_timestamp_ = now;
      has_timestamp_ = true;
    }
  }
  if (pending_state_ != flush_state_) {
    double delay = 0;
    if (flush_state_ != State::Empty) {
      if (static_cast<int32>(pending_state_) > static_cast<int32>(flush_state_)) {
        delay = UP_DELAY;
      } else {
        delay = DOWN_DELAY;
      }
      if (network_type_ == NetType::Unknown) {
        delay = 0;
      }
    }

    CHECK(has_timestamp_);
    if (now >= pending_timestamp_ + delay) {
      has_timestamp_ = false;
      flush_state_ = pending_state_;
      notify_flag(Flag::State);
    } else {
      set_timeout_at(pending_timestamp_ + delay);
    }
  } else {
    has_timestamp_ = false;
  }
}

}  // namespace td
