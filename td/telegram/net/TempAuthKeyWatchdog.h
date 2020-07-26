//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"

#include "td/telegram/Global.h"
#include "td/telegram/net/NetQueryCreator.h"
#include "td/telegram/net/NetQueryDispatcher.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/Time.h"

#include <map>

namespace td {

class TempAuthKeyWatchdog : public NetQueryCallback {
  class RegisteredAuthKeyImpl {
   public:
    explicit RegisteredAuthKeyImpl(int64 auth_key_id)
        : watchdog_(G()->temp_auth_key_watchdog()), auth_key_id_(auth_key_id) {
    }
    RegisteredAuthKeyImpl(const RegisteredAuthKeyImpl &other) = delete;
    RegisteredAuthKeyImpl &operator=(const RegisteredAuthKeyImpl &other) = delete;
    RegisteredAuthKeyImpl(RegisteredAuthKeyImpl &&other) = delete;
    RegisteredAuthKeyImpl &operator=(RegisteredAuthKeyImpl &&other) = delete;
    ~RegisteredAuthKeyImpl() {
      send_closure(watchdog_, &TempAuthKeyWatchdog::unregister_auth_key_id_impl, auth_key_id_);
    }

   private:
    ActorId<TempAuthKeyWatchdog> watchdog_;
    int64 auth_key_id_;
  };

 public:
  explicit TempAuthKeyWatchdog(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  using RegisteredAuthKey = unique_ptr<RegisteredAuthKeyImpl>;

  static RegisteredAuthKey register_auth_key_id(int64 id) {
    send_closure(G()->temp_auth_key_watchdog(), &TempAuthKeyWatchdog::register_auth_key_id_impl, id);
    return make_unique<RegisteredAuthKeyImpl>(id);
  }

 private:
  static constexpr double SYNC_WAIT = 0.1;
  static constexpr double SYNC_WAIT_MAX = 1.0;

  ActorShared<> parent_;
  std::map<uint64, uint32> id_count_;
  double sync_at_ = 0;
  bool need_sync_ = false;
  bool run_sync_ = false;

  void register_auth_key_id_impl(int64 id) {
    if (!++id_count_[id]) {
      id_count_.erase(id);
    }
    need_sync();
  }

  void unregister_auth_key_id_impl(int64 id) {
    if (!--id_count_[id]) {
      id_count_.erase(id);
    }
    need_sync();
  }

  void need_sync() {
    need_sync_ = true;
    try_sync();
    LOG(DEBUG) << "Need sync temp auth keys";
  }

  void try_sync() {
    if (run_sync_ || !need_sync_) {
      return;
    }

    auto now = Time::now();
    if (sync_at_ == 0) {
      sync_at_ = now + SYNC_WAIT_MAX;
    }
    LOG(DEBUG) << "Set sync timeout";
    set_timeout_at(min(sync_at_, now + SYNC_WAIT));
  }

  void timeout_expired() override {
    LOG(DEBUG) << "Sync timeout expired";
    CHECK(!run_sync_);
    if (!need_sync_) {
      LOG(ERROR) << "Do not need sync..";
      return;
    }
    need_sync_ = false;
    run_sync_ = true;
    sync_at_ = 0;
    vector<int64> ids;
    for (auto &id_count : id_count_) {
      ids.push_back(id_count.first);
    }
    if (G()->close_flag()) {
      return;
    }
    LOG(WARNING) << "Start auth_dropTempAuthKeys except keys " << format::as_array(ids);
    auto query = G()->net_query_creator().create_unauth(telegram_api::auth_dropTempAuthKeys(std::move(ids)));
    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this));
  }

  void on_result(NetQueryPtr query) final {
    run_sync_ = false;
    if (query->is_error()) {
      if (G()->close_flag()) {
        return;
      }
      LOG(ERROR) << "Receive error for auth_dropTempAuthKeys: " << query->error();
      need_sync_ = true;
    } else {
      LOG(INFO) << "Receive OK for auth_dropTempAuthKeys";
    }
    try_sync();
  }
};

}  // namespace td
