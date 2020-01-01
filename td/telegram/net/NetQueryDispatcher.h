//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/NetQuery.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Status.h"

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace td {

class DcAuthManager;
class NetQueryDelayer;
class PublicRsaKeyShared;
class PublicRsaKeyWatchdog;
class SessionMultiProxy;

// Not just dispatcher.
class NetQueryDispatcher {
 public:
  explicit NetQueryDispatcher(std::function<ActorShared<>()> create_reference);
  NetQueryDispatcher();
  NetQueryDispatcher(const NetQueryDispatcher &) = delete;
  NetQueryDispatcher &operator=(const NetQueryDispatcher &) = delete;
  NetQueryDispatcher(NetQueryDispatcher &&) = delete;
  NetQueryDispatcher &operator=(NetQueryDispatcher &&) = delete;
  ~NetQueryDispatcher();

  void dispatch(NetQueryPtr net_query);
  void dispatch_with_callback(NetQueryPtr net_query, ActorShared<NetQueryCallback> callback);
  void stop();

  void update_session_count();
  void destroy_auth_keys(Promise<> promise);
  void update_use_pfs();
  void update_mtproto_header();

  void update_valid_dc(DcId dc_id);

  DcId main_dc_id() const {
    return DcId::internal(main_dc_id_.load());
  }

  void set_main_dc_id(int32 new_main_dc_id);

 private:
  std::atomic<bool> stop_flag_{false};
  bool need_destroy_auth_key_{false};
  ActorOwn<NetQueryDelayer> delayer_;
  ActorOwn<DcAuthManager> dc_auth_manager_;
  struct Dc {
    DcId id_;
    std::atomic<bool> is_valid_{false};
    std::atomic<bool> is_inited_{false};  // TODO: cache in scheduler local storage :D

    ActorOwn<SessionMultiProxy> main_session_;
    ActorOwn<SessionMultiProxy> download_session_;
    ActorOwn<SessionMultiProxy> download_small_session_;
    ActorOwn<SessionMultiProxy> upload_session_;
  };
  static constexpr size_t MAX_DC_COUNT = 1000;
  std::array<Dc, MAX_DC_COUNT> dcs_;
#if TD_EMSCRIPTEN  // FIXME
  std::atomic<int32> main_dc_id_{2};
#else
  std::atomic<int32> main_dc_id_{1};
#endif
  std::shared_ptr<PublicRsaKeyShared> common_public_rsa_key_;
  ActorOwn<PublicRsaKeyWatchdog> public_rsa_key_watchdog_;
  std::mutex main_dc_id_mutex_;
  std::shared_ptr<Guard> td_guard_;

  Status wait_dc_init(DcId dc_id, bool force);
  bool is_dc_inited(int32 raw_dc_id);

  static int32 get_session_count();
  static bool get_use_pfs();

  static void complete_net_query(NetQueryPtr net_query);

  void try_fix_migrate(NetQueryPtr &net_query);
};

}  // namespace td
