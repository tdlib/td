//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"

#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/DcOptionsSet.h"
#include "td/telegram/StateManager.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SignalSlot.h"

#include "td/net/NetStats.h"

#include "td/utils/FloodControlStrict.h"
#include "td/utils/logging.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/Time.h"

#include <algorithm>
#include <map>
#include <memory>
#include <utility>

namespace td {
namespace mtproto {
class RawConnection;
}  // namespace mtproto
namespace detail {
class StatsCallback;
}
class GetHostByNameActor;
}  // namespace td

namespace td {

class Proxy {
 public:
  tl_object_ptr<td_api::Proxy> as_td_api() const {
    switch (type_) {
      case Type::None:
        return make_tl_object<td_api::proxyEmpty>();
      case Type::Socks5:
        return make_tl_object<td_api::proxySocks5>(server_, port_, user_, password_);
    }
    UNREACHABLE();
    return nullptr;
  }

  static Proxy from_td_api(const tl_object_ptr<td_api::Proxy> &proxy) {
    if (proxy == nullptr) {
      return Proxy();
    }

    switch (proxy->get_id()) {
      case td_api::proxyEmpty::ID:
        return Proxy();
      case td_api::proxySocks5::ID: {
        auto &socks5_proxy = static_cast<const td_api::proxySocks5 &>(*proxy);
        return Proxy::socks5(socks5_proxy.server_, socks5_proxy.port_, socks5_proxy.username_, socks5_proxy.password_);
      }
    }
    UNREACHABLE();
    return Proxy();
  }

  static Proxy socks5(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::Socks5;
    proxy.server_ = std::move(server);
    proxy.port_ = std::move(port);
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  CSlice server() const {
    return server_;
  }

  int32 port() const {
    return port_;
  }

  CSlice user() const {
    return user_;
  }

  CSlice password() const {
    return password_;
  }

  enum class Type { None, Socks5 };
  Type type() const {
    return type_;
  }

 private:
  Type type_{Type::None};
  string server_;
  int32 port_;
  string user_;
  string password_;

  template <class T>
  friend void parse(Proxy &proxy, T &parser);

  template <class T>
  friend void store(const Proxy &proxy, T &store);
};

class ConnectionCreator : public Actor {
 public:
  explicit ConnectionCreator(ActorShared<> parent);
  ConnectionCreator(ConnectionCreator &&other);
  ConnectionCreator &operator=(ConnectionCreator &&other);
  ~ConnectionCreator() override;
  void on_dc_options(DcOptions new_dc_options);
  void on_dc_update(DcId dc_id, string ip_port, Promise<> promise);
  void on_mtproto_error(size_t hash);
  void request_raw_connection(DcId dc_id, bool allow_media_only, bool is_media,
                              Promise<std::unique_ptr<mtproto::RawConnection>> promise, size_t hash = 0);
  void request_raw_connection_by_ip(IPAddress ip_address, Promise<std::unique_ptr<mtproto::RawConnection>> promise);

  void set_net_stats_callback(std::shared_ptr<NetStatsCallback> common_callback,
                              std::shared_ptr<NetStatsCallback> media_callback);

  void set_proxy(Proxy proxy);
  void get_proxy(Promise<Proxy> promise);

 private:
  ActorShared<> parent_;
  DcOptionsSet dc_options_set_;
  bool network_flag_ = false;
  uint32 network_generation_ = 0;
  bool online_flag_ = false;

  Proxy proxy_;
  ActorOwn<GetHostByNameActor> get_host_by_name_actor_;
  IPAddress proxy_ip_address_;
  Timestamp resolve_proxy_timestamp_;
  uint64 resolve_proxy_query_token_{0};

  struct ClientInfo {
    class Backoff {
#if TD_ANDROID || TD_DARWIN_IOS || TD_DARWIN_WATCH_OS || TD_TIZEN
      static constexpr int32 MAX_BACKOFF = 300;
#else
      static constexpr int32 MAX_BACKOFF = 16;
#endif

     public:
      void add_event(int32 now) {
        wakeup_at_ = now + next_delay_;
        next_delay_ = std::min(MAX_BACKOFF, next_delay_ * 2);
      }
      int32 get_wakeup_at() const {
        return wakeup_at_;
      }
      void clear() {
        *this = {};
      }

     private:
      int32 wakeup_at_{0};
      int32 next_delay_ = 1;
    };
    ClientInfo();

    Backoff backoff;
    FloodControlStrict flood_control;
    FloodControlStrict flood_control_online;
    FloodControlStrict mtproto_error_flood_control;
    Slot slot;
    size_t pending_connections{0};
    size_t checking_connections{0};
    std::vector<std::pair<std::unique_ptr<mtproto::RawConnection>, double>> ready_connections;
    std::vector<Promise<std::unique_ptr<mtproto::RawConnection>>> queries;

    static constexpr double READY_CONNECTIONS_TIMEOUT = 10;

    bool inited{false};
    size_t hash{0};
    DcId dc_id;
    bool allow_media_only;
    bool is_media;
  };
  std::map<size_t, ClientInfo> clients_;

  std::shared_ptr<NetStatsCallback> media_net_stats_callback_;
  std::shared_ptr<NetStatsCallback> common_net_stats_callback_;

  ActorShared<> ref_cnt_guard_;
  int ref_cnt_{0};
  ActorShared<ConnectionCreator> create_reference(int64 token);
  bool close_flag_{false};
  int64 current_token_ = 0;
  std::map<int64, ActorShared<>> children_;

  int64 next_token() {
    return ++current_token_;
  }
  void set_proxy_impl(Proxy proxy, bool from_db);

  void start_up() override;
  void hangup_shared() override;
  void hangup() override;
  void loop() override;

  void save_dc_options();
  Result<SocketFd> do_request_connection(DcId dc_id, bool allow_media_only);
  Result<std::pair<std::unique_ptr<mtproto::RawConnection>, bool>> do_request_raw_connection(DcId dc_id,
                                                                                             bool allow_media_only,
                                                                                             bool is_media,
                                                                                             size_t hash);

  void on_network(bool network_flag, uint32 network_generation);
  void on_online(bool online_flag);

  void client_wakeup(size_t hash);
  void client_loop(ClientInfo &client);
  struct ConnectionData {
    SocketFd socket_fd;
    StateManager::ConnectionToken connection_token;
    std::unique_ptr<detail::StatsCallback> stats_callback;
  };
  void client_create_raw_connection(Result<ConnectionData> r_connection_data, bool check_mode, bool use_http,
                                    size_t hash, string debug_str, uint32 network_generation);
  void client_add_connection(size_t hash, Result<std::unique_ptr<mtproto::RawConnection>> r_raw_connection,
                             bool check_flag);
  void client_set_timeout_at(ClientInfo &client, double wakeup_at);

  void on_proxy_resolved(Result<IPAddress> ip_address, bool dummy);

  static DcOptions get_default_dc_options(bool is_test);
};

}  // namespace td
