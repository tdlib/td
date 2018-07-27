//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/DcOptionsSet.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/StateManager.h"

#include "td/mtproto/IStreamTransport.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SignalSlot.h"

#include "td/net/NetStats.h"

#include "td/utils/FloodControlStrict.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/port/SocketFd.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/Time.h"

#include <map>
#include <memory>
#include <unordered_map>
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
  static Proxy socks5(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::Socks5;
    proxy.server_ = std::move(server);
    proxy.port_ = std::move(port);
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  static Proxy http_tcp(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::HttpTcp;
    proxy.server_ = std::move(server);
    proxy.port_ = std::move(port);
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  static Proxy http_caching(string server, int32 port, string user, string password) {
    Proxy proxy;
    proxy.type_ = Type::HttpCaching;
    proxy.server_ = std::move(server);
    proxy.port_ = std::move(port);
    proxy.user_ = std::move(user);
    proxy.password_ = std::move(password);
    return proxy;
  }

  static Proxy mtproto(string server, int32 port, string secret) {
    Proxy proxy;
    proxy.type_ = Type::Mtproto;
    proxy.server_ = std::move(server);
    proxy.port_ = std::move(port);
    proxy.secret_ = std::move(secret);
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

  CSlice secret() const {
    return secret_;
  }

  enum class Type : int32 { None, Socks5, Mtproto, HttpTcp, HttpCaching };
  Type type() const {
    return type_;
  }

  template <class T>
  void parse(T &parser);

  template <class T>
  void store(T &storer) const;

 private:
  Type type_{Type::None};
  string server_;
  int32 port_ = 0;
  string user_;
  string password_;
  string secret_;
};

inline bool operator==(const Proxy &lhs, const Proxy &rhs) {
  return lhs.type() == rhs.type() && lhs.server() == rhs.server() && lhs.port() == rhs.port() &&
         lhs.user() == rhs.user() && lhs.password() == rhs.password() && lhs.secret() == rhs.secret();
}

inline bool operator!=(const Proxy &lhs, const Proxy &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const Proxy &proxy);

class ConnectionCreator : public NetQueryCallback {
 public:
  explicit ConnectionCreator(ActorShared<> parent);
  ConnectionCreator(ConnectionCreator &&other);
  ConnectionCreator &operator=(ConnectionCreator &&other);
  ~ConnectionCreator() override;

  void on_dc_options(DcOptions new_dc_options);
  void on_dc_update(DcId dc_id, string ip_port, Promise<> promise);
  void on_pong(size_t hash);
  void on_mtproto_error(size_t hash);
  void request_raw_connection(DcId dc_id, bool allow_media_only, bool is_media,
                              Promise<std::unique_ptr<mtproto::RawConnection>> promise, size_t hash = 0);
  void request_raw_connection_by_ip(IPAddress ip_address, Promise<std::unique_ptr<mtproto::RawConnection>> promise);

  void set_net_stats_callback(std::shared_ptr<NetStatsCallback> common_callback,
                              std::shared_ptr<NetStatsCallback> media_callback);

  void add_proxy(int32 old_proxy_id, string server, int32 port, bool enable,
                 td_api::object_ptr<td_api::ProxyType> proxy_type, Promise<td_api::object_ptr<td_api::proxy>> promise);
  void enable_proxy(int32 proxy_id, Promise<Unit> promise);
  void disable_proxy(Promise<Unit> promise);
  void remove_proxy(int32 proxy_id, Promise<Unit> promise);
  void get_proxies(Promise<td_api::object_ptr<td_api::proxies>> promise);
  void get_proxy_link(int32 proxy_id, Promise<string> promise);
  void ping_proxy(int32 proxy_id, Promise<double> promise);

 private:
  ActorShared<> parent_;
  DcOptionsSet dc_options_set_;
  bool network_flag_ = false;
  uint32 network_generation_ = 0;
  bool online_flag_ = false;
  bool is_inited_ = false;

  static constexpr int32 MAX_PROXY_LAST_USED_SAVE_DELAY = 60;
  std::map<int32, Proxy> proxies_;
  std::unordered_map<int32, int32> proxy_last_used_date_;
  std::unordered_map<int32, int32> proxy_last_used_saved_date_;
  int32 max_proxy_id_ = 0;
  int32 active_proxy_id_ = 0;
  ActorOwn<GetHostByNameActor> get_host_by_name_actor_;
  IPAddress proxy_ip_address_;
  Timestamp resolve_proxy_timestamp_;
  uint64 resolve_proxy_query_token_{0};

  uint64 get_proxy_info_query_token_{0};
  Timestamp get_proxy_info_timestamp_;

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
        next_delay_ = min(MAX_BACKOFF, next_delay_ * 2);
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
  uint64 current_token_ = 0;
  std::map<uint64, std::pair<bool, ActorShared<>>> children_;

  struct PingMainDcRequest {
    Promise<double> promise;
    size_t left_queries = 0;
    Result<double> result;
  };
  std::map<uint64, PingMainDcRequest> ping_main_dc_requests_;

  uint64 next_token() {
    return ++current_token_;
  }

  void enable_proxy_impl(int32 proxy_id);
  void disable_proxy_impl();
  void on_proxy_changed(bool from_db);
  static string get_proxy_database_key(int32 proxy_id);
  static string get_proxy_used_database_key(int32 proxy_id);
  void save_proxy_last_used_date(int32 delay);
  td_api::object_ptr<td_api::proxy> get_proxy_object(int32 proxy_id) const;

  void start_up() override;
  void hangup_shared() override;
  void hangup() override;
  void loop() override;

  void on_result(NetQueryPtr query) override;

  void save_dc_options();
  Result<SocketFd> do_request_connection(DcId dc_id, bool allow_media_only);
  Result<std::pair<std::unique_ptr<mtproto::RawConnection>, bool>> do_request_raw_connection(DcId dc_id,
                                                                                             bool allow_media_only,
                                                                                             bool is_media,
                                                                                             size_t hash);

  void on_network(bool network_flag, uint32 network_generation);
  void on_online(bool online_flag);

  static void update_mtproto_header(const Proxy &proxy);

  void client_wakeup(size_t hash);
  void client_loop(ClientInfo &client);
  struct ConnectionData {
    SocketFd socket_fd;
    StateManager::ConnectionToken connection_token;
    std::unique_ptr<detail::StatsCallback> stats_callback;
  };
  void client_create_raw_connection(Result<ConnectionData> r_connection_data, bool check_mode,
                                    mtproto::TransportType transport_type, size_t hash, string debug_str,
                                    uint32 network_generation);
  void client_add_connection(size_t hash, Result<std::unique_ptr<mtproto::RawConnection>> r_raw_connection,
                             bool check_flag);
  void client_set_timeout_at(ClientInfo &client, double wakeup_at);

  void on_get_proxy_info(telegram_api::object_ptr<telegram_api::help_ProxyData> proxy_data_ptr);

  void schedule_get_proxy_info(int32 expires);

  void on_proxy_resolved(Result<IPAddress> ip_address, bool dummy);

  static DcOptions get_default_dc_options(bool is_test);

  struct FindConnectionExtra {
    DcOptionsSet::Stat *stat{nullptr};
    mtproto::TransportType transport_type;
    string debug_str;
    IPAddress mtproto_ip;
    bool check_mode{false};
  };
  class ProxyInfo;

  static Result<mtproto::TransportType> get_transport_type(const ProxyInfo &proxy,
                                                           const DcOptionsSet::ConnectionInfo &info);

  Result<SocketFd> find_connection(const ProxyInfo &proxy, DcId dc_id, bool allow_media_only,
                                   FindConnectionExtra &extra);

  void ping_proxy_resolved(int32 proxy_id, IPAddress ip_address, Promise<double> promise);

  void ping_proxy_socket_fd(SocketFd socket_fd, mtproto::TransportType transport_type, Promise<double> promise);

  void on_ping_main_dc_result(uint64 token, Result<double> result);
};

}  // namespace td
