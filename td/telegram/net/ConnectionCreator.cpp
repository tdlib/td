//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/net/ConnectionCreator.h"

#include "td/telegram/telegram_api.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/StateManager.h"

#include "td/mtproto/IStreamTransport.h"
#include "td/mtproto/PingConnection.h"
#include "td/mtproto/RawConnection.h"

#include "td/net/GetHostByNameActor.h"
#include "td/net/HttpProxy.h"
#include "td/net/Socks5.h"
#include "td/net/TransparentProxy.h"

#include "td/utils/base64.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/IPAddress.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>
#include <utility>

namespace td {

static int VERBOSITY_NAME(connections) = VERBOSITY_NAME(DEBUG) + 8;

namespace detail {

class StatsCallback final : public mtproto::RawConnection::StatsCallback {
 public:
  StatsCallback(std::shared_ptr<NetStatsCallback> net_stats_callback, ActorId<ConnectionCreator> connection_creator,
                size_t hash, DcOptionsSet::Stat *option_stat)
      : net_stats_callback_(std::move(net_stats_callback))
      , connection_creator_(std::move(connection_creator))
      , hash_(hash)
      , option_stat_(option_stat) {
  }

  void on_read(uint64 bytes) final {
    net_stats_callback_->on_read(bytes);
  }
  void on_write(uint64 bytes) final {
    net_stats_callback_->on_write(bytes);
  }

  void on_pong() final {
    if (option_stat_) {
      send_lambda(connection_creator_, [stat = option_stat_] { stat->on_ok(); });
    }
    send_closure(connection_creator_, &ConnectionCreator::on_pong, hash_);
  }

  void on_error() final {
    if (option_stat_) {
      send_lambda(connection_creator_, [stat = option_stat_] { stat->on_error(); });
    }
  }

  void on_mtproto_error() final {
    send_closure(connection_creator_, &ConnectionCreator::on_mtproto_error, hash_);
  }

 private:
  std::shared_ptr<NetStatsCallback> net_stats_callback_;
  ActorId<ConnectionCreator> connection_creator_;
  size_t hash_;
  DcOptionsSet::Stat *option_stat_;
};

class PingActor : public Actor {
 public:
  PingActor(std::unique_ptr<mtproto::RawConnection> raw_connection,
            Promise<std::unique_ptr<mtproto::RawConnection>> promise, ActorShared<> parent)
      : promise_(std::move(promise)), parent_(std::move(parent)) {
    ping_connection_ = std::make_unique<mtproto::PingConnection>(std::move(raw_connection), 2);
  }

 private:
  std::unique_ptr<mtproto::PingConnection> ping_connection_;
  Promise<std::unique_ptr<mtproto::RawConnection>> promise_;
  ActorShared<> parent_;

  void start_up() override {
    ping_connection_->get_pollable().set_observer(this);
    subscribe(ping_connection_->get_pollable());
    set_timeout_in(10);
    yield();
  }

  void hangup() override {
    finish(Status::Error("Cancelled"));
    stop();
  }

  void tear_down() override {
    finish(Status::OK());
  }

  void loop() override {
    auto status = ping_connection_->flush();
    if (status.is_error()) {
      finish(std::move(status));
      return stop();
    }
    if (ping_connection_->was_pong()) {
      finish(Status::OK());
      return stop();
    }
  }

  void timeout_expired() override {
    finish(Status::Error("Pong timeout expired"));
    stop();
  }

  void finish(Status status) {
    auto raw_connection = ping_connection_->move_as_raw_connection();
    if (!raw_connection) {
      CHECK(!promise_);
      return;
    }
    unsubscribe(raw_connection->get_pollable());
    raw_connection->get_pollable().set_observer(nullptr);
    if (promise_) {
      if (status.is_error()) {
        if (raw_connection->stats_callback()) {
          raw_connection->stats_callback()->on_error();
        }
        raw_connection->close();
        promise_.set_error(std::move(status));
      } else {
        raw_connection->rtt_ = ping_connection_->rtt();
        if (raw_connection->stats_callback()) {
          raw_connection->stats_callback()->on_pong();
        }
        promise_.set_value(std::move(raw_connection));
      }
    } else {
      if (raw_connection->stats_callback()) {
        raw_connection->stats_callback()->on_error();
      }
      raw_connection->close();
    }
  }
};

}  // namespace detail

class ConnectionCreator::ProxyInfo {
 public:
  ProxyInfo(Proxy *proxy, IPAddress ip_address) : proxy_(proxy), ip_address_(std::move(ip_address)) {
  }
  bool use_proxy() const {
    return proxy_ != nullptr;
  }
  Proxy::Type proxy_type() const {
    return proxy_ == nullptr ? Proxy::Type::None : proxy_->type();
  }
  bool use_socks5_proxy() const {
    return proxy_type() == Proxy::Type::Socks5;
  }
  bool use_http_tcp_proxy() const {
    return proxy_type() == Proxy::Type::HttpTcp;
  }
  bool use_http_caching_proxy() const {
    return proxy_type() == Proxy::Type::HttpCaching;
  }
  bool use_mtproto_proxy() const {
    return proxy_type() == Proxy::Type::Mtproto;
  }
  const Proxy &proxy() const {
    CHECK(use_proxy());
    return *proxy_;
  }
  const IPAddress &ip_address() const {
    return ip_address_;
  }

 private:
  Proxy *proxy_;
  IPAddress ip_address_;
};

template <class T>
void Proxy::parse(T &parser) {
  using td::parse;
  parse(type_, parser);
  if (type_ == Proxy::Type::Socks5 || type_ == Proxy::Type::HttpTcp || type_ == Proxy::Type::HttpCaching) {
    parse(server_, parser);
    parse(port_, parser);
    parse(user_, parser);
    parse(password_, parser);
  } else if (type_ == Proxy::Type::Mtproto) {
    parse(server_, parser);
    parse(port_, parser);
    parse(secret_, parser);
  } else {
    CHECK(type_ == Proxy::Type::None) << static_cast<int32>(type_);
  }
}

template <class T>
void Proxy::store(T &storer) const {
  using td::store;
  store(type_, storer);
  if (type_ == Proxy::Type::Socks5 || type_ == Proxy::Type::HttpTcp || type_ == Proxy::Type::HttpCaching) {
    store(server_, storer);
    store(port_, storer);
    store(user_, storer);
    store(password_, storer);
  } else if (type_ == Proxy::Type::Mtproto) {
    store(server_, storer);
    store(port_, storer);
    store(secret_, storer);
  } else {
    CHECK(type_ == Proxy::Type::None);
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const Proxy &proxy) {
  switch (proxy.type()) {
    case Proxy::Type::Socks5:
      return string_builder << "ProxySocks5 " << proxy.server() << ":" << proxy.port();
    case Proxy::Type::HttpTcp:
      return string_builder << "ProxyHttpTcp " << proxy.server() << ":" << proxy.port();
    case Proxy::Type::HttpCaching:
      return string_builder << "ProxyHttpCaching " << proxy.server() << ":" << proxy.port();
    case Proxy::Type::Mtproto:
      return string_builder << "ProxyMtproto " << proxy.server() << ":" << proxy.port() << "/" << proxy.secret();
    case Proxy::Type::None:
      return string_builder << "ProxyEmpty";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

ConnectionCreator::ClientInfo::ClientInfo() {
  flood_control.add_limit(1, 1);
  flood_control.add_limit(4, 2);
  flood_control.add_limit(8, 3);

  flood_control_online.add_limit(1, 4);
  flood_control_online.add_limit(5, 5);

  mtproto_error_flood_control.add_limit(1, 1);
  mtproto_error_flood_control.add_limit(4, 2);
  mtproto_error_flood_control.add_limit(8, 3);
}

ConnectionCreator::ConnectionCreator(ActorShared<> parent) : parent_(std::move(parent)) {
}

ConnectionCreator::ConnectionCreator(ConnectionCreator &&other) = default;

ConnectionCreator &ConnectionCreator::operator=(ConnectionCreator &&other) = default;

ConnectionCreator::~ConnectionCreator() = default;

void ConnectionCreator::set_net_stats_callback(std::shared_ptr<NetStatsCallback> common_callback,
                                               std::shared_ptr<NetStatsCallback> media_callback) {
  common_net_stats_callback_ = std::move(common_callback);
  media_net_stats_callback_ = std::move(media_callback);
}

void ConnectionCreator::add_proxy(int32 old_proxy_id, string server, int32 port, bool enable,
                                  td_api::object_ptr<td_api::ProxyType> proxy_type,
                                  Promise<td_api::object_ptr<td_api::proxy>> promise) {
  if (proxy_type == nullptr) {
    return promise.set_error(Status::Error(400, "Proxy type should not be empty"));
  }
  if (server.empty()) {
    return promise.set_error(Status::Error(400, "Server name can't be empty"));
  }
  if (port <= 0 || port > 65535) {
    return promise.set_error(Status::Error(400, "Wrong port number"));
  }

  auto is_secret_supported = [](Slice secret) {
    if (secret.size() == 32) {
      return true;
    }
    if (secret.size() == 34) {
      return begins_with(secret, "dd");
    }
    return false;
  };

  Proxy new_proxy;
  switch (proxy_type->get_id()) {
    case td_api::proxyTypeSocks5::ID: {
      auto type = td_api::move_object_as<td_api::proxyTypeSocks5>(proxy_type);
      new_proxy = Proxy::socks5(server, port, type->username_, type->password_);
      break;
    }
    case td_api::proxyTypeHttp::ID: {
      auto type = td_api::move_object_as<td_api::proxyTypeHttp>(proxy_type);
      if (type->http_only_) {
        new_proxy = Proxy::http_caching(server, port, type->username_, type->password_);
      } else {
        new_proxy = Proxy::http_tcp(server, port, type->username_, type->password_);
      }
      break;
    }
    case td_api::proxyTypeMtproto::ID: {
      auto type = td_api::move_object_as<td_api::proxyTypeMtproto>(proxy_type);
      if (hex_decode(type->secret_).is_error()) {
        return promise.set_error(Status::Error(400, "Wrong secret"));
      }
      if (!is_secret_supported(type->secret_)) {
        return promise.set_error(Status::Error(400, "Unsupported secret"));
      }
      new_proxy = Proxy::mtproto(server, port, type->secret_);
      break;
    }
    default:
      UNREACHABLE();
  }
  if (old_proxy_id >= 0) {
    if (proxies_.count(old_proxy_id) == 0) {
      return promise.set_error(Status::Error(400, "Proxy not found"));
    }
    auto &old_proxy = proxies_[old_proxy_id];
    if (old_proxy == new_proxy) {
      if (enable) {
        enable_proxy_impl(old_proxy_id);
      }
      return promise.set_value(get_proxy_object(old_proxy_id));
    }
    if (old_proxy_id == active_proxy_id_) {
      enable = true;
      disable_proxy_impl();
    }

    proxies_.erase(old_proxy_id);
    G()->td_db()->get_binlog_pmc()->erase(get_proxy_used_database_key(old_proxy_id));
    proxy_last_used_date_.erase(old_proxy_id);
    proxy_last_used_saved_date_.erase(old_proxy_id);
  }

  auto proxy_id = [&] {
    for (auto &proxy : proxies_) {
      if (proxy.second == new_proxy) {
        return proxy.first;
      }
    }

    int32 proxy_id = old_proxy_id;
    if (proxy_id < 0) {
      CHECK(max_proxy_id_ >= 2);
      proxy_id = max_proxy_id_++;
      G()->td_db()->get_binlog_pmc()->set("proxy_max_id", to_string(max_proxy_id_));
    }
    CHECK(proxies_.count(proxy_id) == 0);
    proxies_.emplace(proxy_id, std::move(new_proxy));
    G()->td_db()->get_binlog_pmc()->set(get_proxy_database_key(proxy_id),
                                        log_event_store(proxies_[proxy_id]).as_slice().str());
    return proxy_id;
  }();
  if (enable) {
    enable_proxy_impl(proxy_id);
  }
  promise.set_value(get_proxy_object(proxy_id));
}

void ConnectionCreator::enable_proxy(int32 proxy_id, Promise<Unit> promise) {
  if (proxies_.count(proxy_id) == 0) {
    return promise.set_error(Status::Error(400, "Unknown proxy identifier"));
  }

  enable_proxy_impl(proxy_id);
  promise.set_value(Unit());
}

void ConnectionCreator::disable_proxy(Promise<Unit> promise) {
  save_proxy_last_used_date(0);
  disable_proxy_impl();
  promise.set_value(Unit());
}

void ConnectionCreator::remove_proxy(int32 proxy_id, Promise<Unit> promise) {
  if (proxies_.count(proxy_id) == 0) {
    return promise.set_error(Status::Error(400, "Unknown proxy identifier"));
  }

  if (proxy_id == active_proxy_id_) {
    disable_proxy_impl();
  }

  proxies_.erase(proxy_id);

  G()->td_db()->get_binlog_pmc()->erase(get_proxy_database_key(proxy_id));
  G()->td_db()->get_binlog_pmc()->erase(get_proxy_used_database_key(proxy_id));
  promise.set_value(Unit());
}

void ConnectionCreator::get_proxies(Promise<td_api::object_ptr<td_api::proxies>> promise) {
  promise.set_value(td_api::make_object<td_api::proxies>(
      transform(proxies_, [this](const std::pair<int32, Proxy> &proxy) { return get_proxy_object(proxy.first); })));
}

void ConnectionCreator::get_proxy_link(int32 proxy_id, Promise<string> promise) {
  if (proxies_.count(proxy_id) == 0) {
    return promise.set_error(Status::Error(400, "Unknown proxy identifier"));
  }

  auto &proxy = proxies_[proxy_id];
  string url = G()->shared_config().get_option_string("t_me_url", "https://t.me/");
  bool is_socks = false;
  switch (proxy.type()) {
    case Proxy::Type::Socks5:
      url += "socks";
      is_socks = true;
      break;
    case Proxy::Type::HttpTcp:
    case Proxy::Type::HttpCaching:
      return promise.set_error(Status::Error(400, "HTTP proxy can't have public link"));
    case Proxy::Type::Mtproto:
      url += "proxy";
      break;
    default:
      UNREACHABLE();
  }
  url += "?server=";
  url += url_encode(proxy.server());
  url += "&port=";
  url += to_string(proxy.port());
  if (is_socks) {
    if (!proxy.user().empty() || !proxy.password().empty()) {
      url += "&user=";
      url += url_encode(proxy.user());
      url += "&pass=";
      url += url_encode(proxy.password());
    }
  } else {
    url += "&secret=";
    url += url_encode(proxy.secret());
  }
  promise.set_value(std::move(url));
}

void ConnectionCreator::ping_proxy(int32 proxy_id, Promise<double> promise) {
  if (proxy_id == 0) {
    ProxyInfo proxy{nullptr, IPAddress()};
    auto main_dc_id = G()->net_query_dispatcher().main_dc_id();
    bool prefer_ipv6 = G()->shared_config().get_option_boolean("prefer_ipv6");
    auto infos = dc_options_set_.find_all_connections(main_dc_id, false, false, prefer_ipv6, false);
    if (infos.empty()) {
      return promise.set_error(Status::Error(400, "Can't find valid DC address"));
    }
    const size_t MAX_CONNECTIONS = 10;
    if (infos.size() > MAX_CONNECTIONS) {
      infos.resize(MAX_CONNECTIONS);
    }

    auto token = next_token();
    auto &request = ping_main_dc_requests_[token];
    request.promise = std::move(promise);
    request.left_queries = infos.size();
    request.result = Status::Error(400, "Failed to ping");

    for (auto &info : infos) {
      auto r_transport_type = get_transport_type(ProxyInfo{nullptr, IPAddress()}, info);
      if (r_transport_type.is_error()) {
        LOG(ERROR) << r_transport_type.error();
        on_ping_main_dc_result(token, r_transport_type.move_as_error());
        continue;
      }

      auto r_socket_fd = SocketFd::open(info.option->get_ip_address());
      if (r_socket_fd.is_error()) {
        LOG(DEBUG) << "Failed to open socket: " << r_socket_fd.error();
        on_ping_main_dc_result(token, r_socket_fd.move_as_error());
        continue;
      }

      ping_proxy_socket_fd(r_socket_fd.move_as_ok(), r_transport_type.move_as_ok(),
                           PromiseCreator::lambda([actor_id = actor_id(this), token](Result<double> result) {
                             send_closure(actor_id, &ConnectionCreator::on_ping_main_dc_result, token,
                                          std::move(result));
                           }));
    }
    return;
  }

  auto it = proxies_.find(proxy_id);
  if (it == proxies_.end()) {
    return promise.set_error(Status::Error(400, "Unknown proxy identifier"));
  }
  const Proxy &proxy = it->second;
  bool prefer_ipv6 = G()->shared_config().get_option_boolean("prefer_ipv6");
  send_closure(get_host_by_name_actor_, &GetHostByNameActor::run, proxy.server().str(), proxy.port(), prefer_ipv6,
               PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise),
                                       proxy_id](Result<IPAddress> result) mutable {
                 if (result.is_error()) {
                   return promise.set_error(Status::Error(400, result.error().message()));
                 }
                 send_closure(actor_id, &ConnectionCreator::ping_proxy_resolved, proxy_id, result.move_as_ok(),
                              std::move(promise));
               }));
}

void ConnectionCreator::ping_proxy_resolved(int32 proxy_id, IPAddress ip_address, Promise<double> promise) {
  auto it = proxies_.find(proxy_id);
  if (it == proxies_.end()) {
    return promise.set_error(Status::Error(400, "Unknown proxy identifier"));
  }
  ProxyInfo proxy(&it->second, ip_address);
  auto main_dc_id = G()->net_query_dispatcher().main_dc_id();
  FindConnectionExtra extra;
  auto r_socket_fd = find_connection(proxy, main_dc_id, false, extra);
  if (r_socket_fd.is_error()) {
    return promise.set_error(Status::Error(400, r_socket_fd.error().message()));
  }
  auto socket_fd = r_socket_fd.move_as_ok();

  auto socket_fd_promise =
      PromiseCreator::lambda([promise = std::move(promise), actor_id = actor_id(this),
                              transport_type = std::move(extra.transport_type)](Result<SocketFd> r_socket_fd) mutable {
        if (r_socket_fd.is_error()) {
          return promise.set_error(Status::Error(400, r_socket_fd.error().message()));
        }
        send_closure(actor_id, &ConnectionCreator::ping_proxy_socket_fd, r_socket_fd.move_as_ok(),
                     std::move(transport_type), std::move(promise));
      });
  CHECK(proxy.use_proxy());
  if (proxy.use_socks5_proxy() || proxy.use_http_tcp_proxy()) {
    class Callback : public TransparentProxy::Callback {
     public:
      explicit Callback(Promise<SocketFd> promise) : promise_(std::move(promise)) {
      }
      void set_result(Result<SocketFd> result) override {
        promise_.set_result(std::move(result));
      }
      void on_connected() override {
      }

     private:
      Promise<SocketFd> promise_;
    };
    LOG(INFO) << "Start ping proxy: " << extra.debug_str;
    auto token = next_token();
    if (proxy.use_socks5_proxy()) {
      children_[token] = {false, create_actor<Socks5>("PingSocks5", std::move(socket_fd), extra.mtproto_ip,
                                                      proxy.proxy().user().str(), proxy.proxy().password().str(),
                                                      std::make_unique<Callback>(std::move(socket_fd_promise)),
                                                      create_reference(token))};
    } else {
      children_[token] = {false, create_actor<HttpProxy>("PingHttpProxy", std::move(socket_fd), extra.mtproto_ip,
                                                         proxy.proxy().user().str(), proxy.proxy().password().str(),
                                                         std::make_unique<Callback>(std::move(socket_fd_promise)),
                                                         create_reference(token))};
    }
  } else {
    socket_fd_promise.set_value(std::move(socket_fd));
  }
}

void ConnectionCreator::ping_proxy_socket_fd(SocketFd socket_fd, mtproto::TransportType transport_type,
                                             Promise<double> promise) {
  auto token = next_token();
  auto raw_connection =
      std::make_unique<mtproto::RawConnection>(std::move(socket_fd), std::move(transport_type), nullptr);
  children_[token] = {
      false, create_actor<detail::PingActor>(
                 "PingActor", std::move(raw_connection),
                 PromiseCreator::lambda(
                     [promise = std::move(promise)](Result<std::unique_ptr<mtproto::RawConnection>> result) mutable {
                       if (result.is_error()) {
                         return promise.set_error(Status::Error(400, result.error().message()));
                       }
                       auto ping_time = result.ok()->rtt_;
                       promise.set_value(std::move(ping_time));
                     }),
                 create_reference(token))};
}

void ConnectionCreator::enable_proxy_impl(int32 proxy_id) {
  CHECK(proxies_.count(proxy_id) == 1);
  if (proxy_id == active_proxy_id_) {
    return;
  }

  if ((active_proxy_id_ != 0 && proxies_[active_proxy_id_].type() == Proxy::Type::Mtproto) ||
      proxies_[proxy_id].type() == Proxy::Type::Mtproto) {
    update_mtproto_header(proxies_[proxy_id]);
  }
  save_proxy_last_used_date(0);

  active_proxy_id_ = proxy_id;
  G()->td_db()->get_binlog_pmc()->set("proxy_active_id", to_string(proxy_id));

  on_proxy_changed(false);
}

void ConnectionCreator::disable_proxy_impl() {
  if (active_proxy_id_ == 0) {
    on_get_proxy_info(make_tl_object<telegram_api::help_proxyDataEmpty>(0));
    return;
  }
  CHECK(proxies_.count(active_proxy_id_) == 1);

  if (proxies_[active_proxy_id_].type() == Proxy::Type::Mtproto) {
    update_mtproto_header(Proxy());
  }

  active_proxy_id_ = 0;
  G()->td_db()->get_binlog_pmc()->erase("proxy_active_id");

  on_proxy_changed(false);
}

void ConnectionCreator::on_proxy_changed(bool from_db) {
  send_closure(G()->state_manager(), &StateManager::on_proxy,
               active_proxy_id_ != 0 && proxies_[active_proxy_id_].type() != Proxy::Type::Mtproto &&
                   proxies_[active_proxy_id_].type() != Proxy::Type::HttpCaching);

  if (!from_db) {
    for (auto &child : children_) {
      if (child.second.first) {
        child.second.second.reset();
      }
    }
  }

  VLOG(connections) << "Drop proxy IP address " << proxy_ip_address_;
  resolve_proxy_query_token_ = 0;
  resolve_proxy_timestamp_ = Timestamp();
  proxy_ip_address_ = IPAddress();

  get_proxy_info_query_token_ = 0;
  get_proxy_info_timestamp_ = Timestamp();
  if (active_proxy_id_ == 0 || !from_db) {
    on_get_proxy_info(make_tl_object<telegram_api::help_proxyDataEmpty>(0));
  } else {
    schedule_get_proxy_info(0);
  }

  loop();
}

string ConnectionCreator::get_proxy_database_key(int32 proxy_id) {
  CHECK(proxy_id > 0);
  if (proxy_id == 1) {
    return "proxy";
  }
  return PSTRING() << "proxy" << proxy_id;
}

string ConnectionCreator::get_proxy_used_database_key(int32 proxy_id) {
  CHECK(proxy_id > 0);
  return PSTRING() << "proxy_used" << proxy_id;
}

void ConnectionCreator::save_proxy_last_used_date(int32 delay) {
  if (active_proxy_id_ == 0) {
    return;
  }

  CHECK(delay >= 0);
  int32 date = proxy_last_used_date_[active_proxy_id_];
  int32 &saved_date = proxy_last_used_saved_date_[active_proxy_id_];
  if (date <= saved_date + delay) {
    return;
  }
  LOG(DEBUG) << "Save proxy last used date " << date;

  saved_date = date;
  G()->td_db()->get_binlog_pmc()->set(get_proxy_used_database_key(active_proxy_id_), to_string(date));
}

td_api::object_ptr<td_api::proxy> ConnectionCreator::get_proxy_object(int32 proxy_id) const {
  auto it = proxies_.find(proxy_id);
  CHECK(it != proxies_.end());
  const Proxy &proxy = it->second;
  td_api::object_ptr<td_api::ProxyType> type;
  switch (proxy.type()) {
    case Proxy::Type::Socks5:
      type = make_tl_object<td_api::proxyTypeSocks5>(proxy.user().str(), proxy.password().str());
      break;
    case Proxy::Type::HttpTcp:
      type = make_tl_object<td_api::proxyTypeHttp>(proxy.user().str(), proxy.password().str(), false);
      break;
    case Proxy::Type::HttpCaching:
      type = make_tl_object<td_api::proxyTypeHttp>(proxy.user().str(), proxy.password().str(), true);
      break;
    case Proxy::Type::Mtproto:
      type = make_tl_object<td_api::proxyTypeMtproto>(proxy.secret().str());
      break;
    default:
      UNREACHABLE();
  }
  auto last_used_date_it = proxy_last_used_date_.find(proxy_id);
  auto last_used_date = last_used_date_it == proxy_last_used_date_.end() ? 0 : last_used_date_it->second;
  return make_tl_object<td_api::proxy>(proxy_id, proxy.server().str(), proxy.port(), last_used_date,
                                       proxy_id == active_proxy_id_, std::move(type));
}

void ConnectionCreator::on_network(bool network_flag, uint32 network_generation) {
  VLOG(connections) << "Receive network flag " << network_flag << " with generation " << network_generation;
  network_flag_ = network_flag;
  auto old_generation = network_generation_;
  network_generation_ = network_generation;
  if (network_flag_) {
    VLOG(connections) << "Set proxy query token to 0: " << old_generation << " " << network_generation_;
    resolve_proxy_query_token_ = 0;
    resolve_proxy_timestamp_ = Timestamp();
    get_proxy_info_timestamp_ = Timestamp();

    for (auto &client : clients_) {
      client.second.backoff.clear();
      client.second.flood_control.clear_events();
      client.second.flood_control_online.clear_events();
      client_loop(client.second);
    }

    if (old_generation != network_generation_) {
      loop();
    }
  }
}

void ConnectionCreator::on_online(bool online_flag) {
  VLOG(connections) << "Receive online flag " << online_flag;
  online_flag_ = online_flag;
  if (online_flag_) {
    for (auto &client : clients_) {
      client.second.backoff.clear();
      client.second.flood_control_online.clear_events();
      client_loop(client.second);
    }
  }
}

void ConnectionCreator::on_pong(size_t hash) {
  if (active_proxy_id_ != 0) {
    auto now = G()->unix_time();
    int32 &last_used = proxy_last_used_date_[active_proxy_id_];
    if (now > last_used) {
      last_used = now;
      save_proxy_last_used_date(MAX_PROXY_LAST_USED_SAVE_DELAY);
    }
  }
}

void ConnectionCreator::on_mtproto_error(size_t hash) {
  auto &client = clients_[hash];
  client.hash = hash;
  client.mtproto_error_flood_control.add_event(static_cast<int32>(Time::now_cached()));
}

void ConnectionCreator::request_raw_connection(DcId dc_id, bool allow_media_only, bool is_media,
                                               Promise<std::unique_ptr<mtproto::RawConnection>> promise, size_t hash) {
  auto &client = clients_[hash];
  if (!client.inited) {
    client.inited = true;
    client.hash = hash;
    client.dc_id = dc_id;
    client.allow_media_only = allow_media_only;
    client.is_media = is_media;
  } else {
    CHECK(client.hash == hash);
    CHECK(client.dc_id == dc_id);
    CHECK(client.allow_media_only == allow_media_only);
    CHECK(client.is_media == is_media);
  }
  VLOG(connections) << "Request connection for " << tag("client", format::as_hex(client.hash)) << " to " << dc_id << " "
                    << tag("allow_media_only", allow_media_only);
  client.queries.push_back(std::move(promise));

  client_loop(client);
}

void ConnectionCreator::request_raw_connection_by_ip(IPAddress ip_address,
                                                     Promise<std::unique_ptr<mtproto::RawConnection>> promise) {
  auto r_socket_fd = SocketFd::open(ip_address);
  if (r_socket_fd.is_error()) {
    return promise.set_error(r_socket_fd.move_as_error());
  }
  auto raw_connection = std::make_unique<mtproto::RawConnection>(
      r_socket_fd.move_as_ok(), mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, 0, ""}, nullptr);
  raw_connection->extra_ = network_generation_;
  promise.set_value(std::move(raw_connection));
}

Result<mtproto::TransportType> ConnectionCreator::get_transport_type(const ProxyInfo &proxy,
                                                                     const DcOptionsSet::ConnectionInfo &info) {
  int32 int_dc_id = info.option->get_dc_id().get_raw_id();
  if (G()->is_test_dc()) {
    int_dc_id += 10000;
  }
  int16 raw_dc_id = narrow_cast<int16>(info.option->is_media_only() ? -int_dc_id : int_dc_id);

  if (proxy.use_mtproto_proxy()) {
    TRY_RESULT(secret, hex_decode(proxy.proxy().secret()));
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, raw_dc_id, std::move(secret)};
  }
  if (proxy.use_http_caching_proxy()) {
    CHECK(info.option != nullptr);
    string proxy_authorization;
    if (!proxy.proxy().user().empty() || !proxy.proxy().password().empty()) {
      proxy_authorization =
          "|basic " + td::base64_encode(PSLICE() << proxy.proxy().user() << ':' << proxy.proxy().password());
    }
    return mtproto::TransportType{mtproto::TransportType::Http, 0,
                                  PSTRING() << info.option->get_ip_address().get_ip_str() << proxy_authorization};
  }

  if (info.use_http) {
    return mtproto::TransportType{mtproto::TransportType::Http, 0, ""};
  } else {
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, raw_dc_id, info.option->get_secret().str()};
  }
}

Result<SocketFd> ConnectionCreator::find_connection(const ProxyInfo &proxy, DcId dc_id, bool allow_media_only,
                                                    FindConnectionExtra &extra) {
  extra.debug_str = PSTRING() << "Failed to find valid IP for " << dc_id;
  bool prefer_ipv6 =
      G()->shared_config().get_option_boolean("prefer_ipv6") || (proxy.use_proxy() && proxy.ip_address().is_ipv6());
  bool only_http = proxy.use_http_caching_proxy();
  TRY_RESULT(info, dc_options_set_.find_connection(
                       dc_id, allow_media_only, proxy.use_proxy() && proxy.use_socks5_proxy(), prefer_ipv6, only_http));
  extra.stat = info.stat;
  TRY_RESULT(transport_type, get_transport_type(proxy, info));
  extra.transport_type = std::move(transport_type);

  extra.debug_str = PSTRING() << " to " << (info.option->is_media_only() ? "MEDIA " : "") << dc_id
                              << (info.use_http ? " over HTTP" : "");

  if (proxy.use_mtproto_proxy()) {
    extra.debug_str = PSTRING() << "Mtproto " << proxy.ip_address() << extra.debug_str;

    LOG(INFO) << "Create: " << extra.debug_str;
    return SocketFd::open(proxy.ip_address());
  }

  extra.check_mode |= info.should_check;

  if (proxy.use_proxy()) {
    extra.mtproto_ip = info.option->get_ip_address();
    extra.debug_str = PSTRING() << (proxy.use_socks5_proxy() ? "Socks5" : (only_http ? "HTTP_ONLY" : "HTTP_TCP")) << ' '
                                << proxy.ip_address() << " --> " << extra.mtproto_ip << extra.debug_str;
    LOG(INFO) << "Create: " << extra.debug_str;
    return SocketFd::open(proxy.ip_address());
  } else {
    extra.debug_str = PSTRING() << info.option->get_ip_address() << extra.debug_str;
    LOG(INFO) << "Create: " << extra.debug_str;
    return SocketFd::open(info.option->get_ip_address());
  }
}

void ConnectionCreator::client_loop(ClientInfo &client) {
  CHECK(client.hash != 0);
  if (!network_flag_) {
    VLOG(connections) << "Exit client_loop, because there is no network";
    return;
  }
  if (close_flag_) {
    VLOG(connections) << "Exit client_loop, because of closing";
    return;
  }

  ProxyInfo proxy{active_proxy_id_ == 0 ? nullptr : &proxies_[active_proxy_id_], proxy_ip_address_};
  if (proxy.use_proxy() && !proxy.ip_address().is_valid()) {
    VLOG(connections) << "Exit client_loop, because there is no valid IP address for proxy: " << proxy.ip_address();
    return;
  }

  VLOG(connections) << "client_loop: " << tag("client", format::as_hex(client.hash));

  // Remove expired ready connections
  client.ready_connections.erase(
      std::remove_if(client.ready_connections.begin(), client.ready_connections.end(),
                     [&, expire_at = Time::now_cached() - ClientInfo::READY_CONNECTIONS_TIMEOUT](auto &v) {
                       bool drop = v.second < expire_at;
                       VLOG_IF(connections, drop) << "Drop expired " << tag("connection", v.first.get());
                       return drop;
                     }),
      client.ready_connections.end());

  // Send ready connections into promises
  {
    auto begin = client.queries.begin();
    auto it = begin;
    while (it != client.queries.end() && !client.ready_connections.empty()) {
      if (!it->is_cancelled()) {
        VLOG(connections) << "Send to promise " << tag("connection", client.ready_connections.back().first.get());
        it->set_value(std::move(client.ready_connections.back().first));
        client.ready_connections.pop_back();
      }
      ++it;
    }
    client.queries.erase(begin, it);
  }

  // Main loop. Create new connections till needed
  bool check_mode = client.checking_connections != 0 && !proxy.use_proxy();
  while (true) {
    // Check if we need new connections
    if (client.queries.empty()) {
      if (!client.ready_connections.empty()) {
        client_set_timeout_at(client, Time::now() + ClientInfo::READY_CONNECTIONS_TIMEOUT);
      }
      return;
    }
    if (check_mode) {
      if (client.checking_connections >= 3) {
        return;
      }
    } else {
      if (client.pending_connections >= client.queries.size()) {
        return;
      }
    }

    // Check flood
    auto &flood_control = online_flag_ ? client.flood_control_online : client.flood_control;
    auto wakeup_at = max(flood_control.get_wakeup_at(), client.mtproto_error_flood_control.get_wakeup_at());
    if (!online_flag_) {
      wakeup_at = max(wakeup_at, client.backoff.get_wakeup_at());
    }
    if (wakeup_at > Time::now()) {
      return client_set_timeout_at(client, wakeup_at);
    }
    flood_control.add_event(static_cast<int32>(Time::now()));
    if (!online_flag_) {
      client.backoff.add_event(static_cast<int32>(Time::now()));
    }

    // Create new RawConnection
    // sync part
    FindConnectionExtra extra;
    auto r_socket_fd = find_connection(proxy, client.dc_id, client.allow_media_only, extra);
    check_mode |= extra.check_mode;
    if (r_socket_fd.is_error()) {
      LOG(WARNING) << extra.debug_str << ": " << r_socket_fd.error();
      if (extra.stat) {
        extra.stat->on_error();  // TODO: different kind of error
      }
      return client_set_timeout_at(client, Time::now() + 0.1);
    }

    auto socket_fd = r_socket_fd.move_as_ok();
    IPAddress debug_ip;
    auto debug_ip_status = debug_ip.init_socket_address(socket_fd);
    if (debug_ip_status.is_ok()) {
      extra.debug_str = PSTRING() << extra.debug_str << " from " << debug_ip;
    } else {
      LOG(ERROR) << debug_ip_status;
    }

    client.pending_connections++;
    if (check_mode) {
      if (extra.stat) {
        extra.stat->on_check();
      }
      client.checking_connections++;
    }

    auto promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), check_mode, transport_type = extra.transport_type, hash = client.hash,
         debug_str = extra.debug_str,
         network_generation = network_generation_](Result<ConnectionData> r_connection_data) mutable {
          send_closure(std::move(actor_id), &ConnectionCreator::client_create_raw_connection,
                       std::move(r_connection_data), check_mode, transport_type, hash, debug_str, network_generation);
        });

    auto stats_callback = std::make_unique<detail::StatsCallback>(
        client.is_media ? media_net_stats_callback_ : common_net_stats_callback_, actor_id(this), client.hash,
        extra.stat);

    if (proxy.use_socks5_proxy() || proxy.use_http_tcp_proxy()) {
      VLOG(connections) << "client_loop: create new transparent proxy connection " << extra.debug_str;
      class Callback : public TransparentProxy::Callback {
       public:
        explicit Callback(Promise<ConnectionData> promise, std::unique_ptr<detail::StatsCallback> stats_callback)
            : promise_(std::move(promise)), stats_callback_(std::move(stats_callback)) {
        }
        void set_result(Result<SocketFd> result) override {
          if (result.is_error()) {
            connection_token_ = StateManager::ConnectionToken();
            if (was_connected_) {
              stats_callback_->on_error();
            }
            promise_.set_error(result.move_as_error());
          } else {
            ConnectionData data;
            data.socket_fd = result.move_as_ok();
            data.connection_token = std::move(connection_token_);
            data.stats_callback = std::move(stats_callback_);
            promise_.set_value(std::move(data));
          }
        }
        void on_connected() override {
          connection_token_ = StateManager::connection_proxy(G()->state_manager());
          was_connected_ = true;
        }

       private:
        Promise<ConnectionData> promise_;
        StateManager::ConnectionToken connection_token_;
        bool was_connected_{false};
        std::unique_ptr<detail::StatsCallback> stats_callback_;
      };
      LOG(INFO) << "Start " << (proxy.use_socks5_proxy() ? "Socks5" : "HTTP") << ": " << extra.debug_str;
      auto token = next_token();
      if (proxy.use_socks5_proxy()) {
        children_[token] = {
            true, create_actor<Socks5>("Socks5", std::move(socket_fd), extra.mtproto_ip, proxy.proxy().user().str(),
                                       proxy.proxy().password().str(),
                                       std::make_unique<Callback>(std::move(promise), std::move(stats_callback)),
                                       create_reference(token))};
      } else {
        children_[token] = {
            true, create_actor<HttpProxy>("HttpProxy", std::move(socket_fd), extra.mtproto_ip,
                                          proxy.proxy().user().str(), proxy.proxy().password().str(),
                                          std::make_unique<Callback>(std::move(promise), std::move(stats_callback)),
                                          create_reference(token))};
      }
    } else {
      VLOG(connections) << "client_loop: create new direct connection " << extra.debug_str;

      ConnectionData data;
      data.socket_fd = std::move(socket_fd);
      data.stats_callback = std::move(stats_callback);
      promise.set_result(std::move(data));
    }
  }
}

void ConnectionCreator::client_create_raw_connection(Result<ConnectionData> r_connection_data, bool check_mode,
                                                     mtproto::TransportType transport_type, size_t hash,
                                                     string debug_str, uint32 network_generation) {
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), hash, check_mode,
                                         debug_str](Result<std::unique_ptr<mtproto::RawConnection>> result) mutable {
    VLOG(connections) << "Ready connection " << (check_mode ? "(" : "(un") << "checked) "
                      << (result.is_ok() ? result.ok().get() : nullptr) << " " << debug_str;
    send_closure(std::move(actor_id), &ConnectionCreator::client_add_connection, hash, std::move(result), check_mode);
  });

  if (r_connection_data.is_error()) {
    return promise.set_error(r_connection_data.move_as_error());
  }

  auto connection_data = r_connection_data.move_as_ok();
  auto raw_connection = std::make_unique<mtproto::RawConnection>(
      std::move(connection_data.socket_fd), std::move(transport_type), std::move(connection_data.stats_callback));
  raw_connection->set_connection_token(std::move(connection_data.connection_token));

  raw_connection->extra_ = network_generation;
  raw_connection->debug_str_ = debug_str;

  if (check_mode) {
    VLOG(connections) << "Start check: " << debug_str;
    auto token = next_token();
    children_[token] = {true, create_actor<detail::PingActor>("PingActor", std::move(raw_connection),
                                                              std::move(promise), create_reference(token))};
  } else {
    promise.set_value(std::move(raw_connection));
  }
}

void ConnectionCreator::client_set_timeout_at(ClientInfo &client, double wakeup_at) {
  if (!client.slot.has_event()) {
    client.slot.set_event(self_closure(this, &ConnectionCreator::client_wakeup, client.hash));
  }
  client.slot.set_timeout_at(wakeup_at);
  VLOG(connections) << tag("client", format::as_hex(client.hash)) << " set timeout in "
                    << wakeup_at - Time::now_cached();
}

void ConnectionCreator::client_add_connection(size_t hash,
                                              Result<std::unique_ptr<mtproto::RawConnection>> r_raw_connection,
                                              bool check_flag) {
  auto &client = clients_[hash];
  CHECK(client.pending_connections > 0);
  client.pending_connections--;
  if (check_flag) {
    CHECK(client.checking_connections > 0);
    client.checking_connections--;
  }
  if (r_raw_connection.is_ok()) {
    VLOG(connections) << "Add ready connection " << r_raw_connection.ok().get() << " for " << tag("client", hash);
    client.backoff.clear();
    client.ready_connections.push_back(std::make_pair(r_raw_connection.move_as_ok(), Time::now_cached()));
  }
  client_loop(client);
}

void ConnectionCreator::client_wakeup(size_t hash) {
  LOG(INFO) << tag("hash", format::as_hex(hash)) << " wakeup";
  client_loop(clients_[hash]);
}

void ConnectionCreator::on_dc_options(DcOptions new_dc_options) {
  LOG(INFO) << "SAVE " << new_dc_options;
  G()->td_db()->get_binlog_pmc()->set("dc_options", serialize(new_dc_options));
  dc_options_set_.reset();
  dc_options_set_.add_dc_options(get_default_dc_options(G()->is_test_dc()));
#if !TD_EMSCRIPTEN  // FIXME
  dc_options_set_.add_dc_options(std::move(new_dc_options));
#endif
}

void ConnectionCreator::on_dc_update(DcId dc_id, string ip_port, Promise<> promise) {
  promise.set_result([&]() -> Result<> {
    if (!dc_id.is_exact()) {
      return Status::Error("Invalid dc_id");
    }
    IPAddress ip_address;
    TRY_STATUS(ip_address.init_host_port(ip_port));
    DcOptions options;
    options.dc_options.emplace_back(dc_id, ip_address);
    send_closure(G()->config_manager(), &ConfigManager::on_dc_options_update, std::move(options));
    return Unit();
  }());
}

void ConnectionCreator::update_mtproto_header(const Proxy &proxy) {
  if (G()->have_mtproto_header()) {
    G()->mtproto_header().set_proxy(proxy);
  }
  if (G()->have_net_query_dispatcher()) {
    G()->net_query_dispatcher().update_mtproto_header();
  }
}

void ConnectionCreator::start_up() {
  class StateCallback : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<ConnectionCreator> connection_creator)
        : connection_creator_(std::move(connection_creator)) {
    }
    bool on_network(NetType network_type, uint32 generation) override {
      send_closure(connection_creator_, &ConnectionCreator::on_network, network_type != NetType::None, generation);
      return connection_creator_.is_alive();
    }
    bool on_online(bool online_flag) override {
      send_closure(connection_creator_, &ConnectionCreator::on_online, online_flag);
      return connection_creator_.is_alive();
    }

   private:
    ActorId<ConnectionCreator> connection_creator_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));

  auto serialized_dc_options = G()->td_db()->get_binlog_pmc()->get("dc_options");
  DcOptions dc_options;
  auto status = unserialize(dc_options, serialized_dc_options);
  if (status.is_error()) {
    on_dc_options(DcOptions());
  } else {
    on_dc_options(std::move(dc_options));
  }

  auto proxy_info = G()->td_db()->get_binlog_pmc()->prefix_get("proxy");
  auto it = proxy_info.find("proxy_max_id");
  if (it != proxy_info.end()) {
    max_proxy_id_ = to_integer<int32>(it->second);
    proxy_info.erase(it);
  }
  it = proxy_info.find("proxy_active_id");
  if (it != proxy_info.end()) {
    active_proxy_id_ = to_integer<int32>(it->second);
    proxy_info.erase(it);
  }

  for (auto &info : proxy_info) {
    if (begins_with(info.first, "proxy_used")) {
      int32 proxy_id = to_integer_safe<int32>(Slice(info.first).substr(10)).move_as_ok();
      int32 last_used = to_integer_safe<int32>(info.second).move_as_ok();
      proxy_last_used_date_[proxy_id] = last_used;
      proxy_last_used_saved_date_[proxy_id] = last_used;
    } else {
      int32 proxy_id = info.first == "proxy" ? 1 : to_integer_safe<int32>(Slice(info.first).substr(5)).move_as_ok();
      CHECK(proxies_.count(proxy_id) == 0);
      log_event_parse(proxies_[proxy_id], info.second).ensure();
      if (proxies_[proxy_id].type() == Proxy::Type::None) {
        LOG_IF(ERROR, proxy_id != 1) << "Have empty proxy " << proxy_id;
        proxies_.erase(proxy_id);
        if (active_proxy_id_ == proxy_id) {
          active_proxy_id_ = 0;
          G()->td_db()->get_binlog_pmc()->erase("proxy_active_id");
        }
      }
    }
  }

  if (max_proxy_id_ == 0) {
    // legacy one-proxy version
    max_proxy_id_ = 2;
    if (!proxies_.empty()) {
      CHECK(proxies_.begin()->first == 1);
      active_proxy_id_ = 1;
      G()->td_db()->get_binlog_pmc()->set("proxy_active_id", "1");
    }
    G()->td_db()->get_binlog_pmc()->set("proxy_max_id", "2");
  } else if (max_proxy_id_ < 2) {
    LOG(ERROR) << "Found wrong max_proxy_id = " << max_proxy_id_;
    max_proxy_id_ = 2;
  }

  if (active_proxy_id_ != 0) {
    if (proxies_[active_proxy_id_].type() == Proxy::Type::Mtproto) {
      update_mtproto_header(proxies_[active_proxy_id_]);
    }

    on_proxy_changed(true);
  }

  get_host_by_name_actor_ =
      create_actor_on_scheduler<GetHostByNameActor>("GetHostByNameActor", G()->get_gc_scheduler_id(), 5 * 60 - 1, 0);

  ref_cnt_guard_ = create_reference(-1);

  is_inited_ = true;
  loop();
}

void ConnectionCreator::hangup_shared() {
  ref_cnt_--;
  children_.erase(get_link_token());
  if (ref_cnt_ == 0) {
    stop();
  }
}

ActorShared<ConnectionCreator> ConnectionCreator::create_reference(int64 token) {
  CHECK(token != 0);
  ref_cnt_++;
  return actor_shared(this, token);
}

void ConnectionCreator::hangup() {
  close_flag_ = true;
  save_proxy_last_used_date(0);
  ref_cnt_guard_.reset();
  for (auto &child : children_) {
    child.second.second.reset();
  }
}

DcOptions ConnectionCreator::get_default_dc_options(bool is_test) {
  DcOptions res;
  enum class HostType { IPv4, IPv6, Url };
  auto add_ip_ports = [&res](int32 dc_id, const vector<string> &ips, const vector<int> &ports,
                             HostType type = HostType::IPv4) {
    IPAddress ip_address;
    for (auto &ip : ips) {
      for (auto port : ports) {
        switch (type) {
          case HostType::IPv4:
            ip_address.init_ipv4_port(ip, port).ensure();
            break;
          case HostType::IPv6:
            ip_address.init_ipv6_port(ip, port).ensure();
            break;
          case HostType::Url:
            ip_address.init_host_port(ip, port).ensure();
            break;
        }
        res.dc_options.emplace_back(DcId::internal(dc_id), ip_address);
      }
    }
  };
  vector<int> ports = {443, 80, 5222};
#if TD_EMSCRIPTEN
  if (is_test) {
    add_ip_ports(1, {"pluto.web.telegram.org/apiws_test"}, {443}, HostType::Url);
    add_ip_ports(2, {"venus.web.telegram.org/apiws_test"}, {443}, HostType::Url);
    add_ip_ports(3, {"aurora.web.telegram.org/apiws_test"}, {443}, HostType::Url);
  } else {
    add_ip_ports(1, {"pluto.web.telegram.org/apiws"}, {443}, HostType::Url);
    add_ip_ports(2, {"venus.web.telegram.org/apiws"}, {443}, HostType::Url);
    add_ip_ports(3, {"aurora.web.telegram.org/apiws"}, {443}, HostType::Url);
    add_ip_ports(4, {"vesta.web.telegram.org/apiws"}, {443}, HostType::Url);
    add_ip_ports(5, {"flora.web.telegram.org/apiws"}, {443}, HostType::Url);
  }
#else
  if (is_test) {
    add_ip_ports(1, {"149.154.175.10"}, ports);
    add_ip_ports(2, {"149.154.167.40"}, ports);
    add_ip_ports(3, {"149.154.175.117"}, ports);

    add_ip_ports(1, {"2001:b28:f23d:f001::e"}, ports, HostType::IPv6);
    add_ip_ports(2, {"2001:67c:4e8:f002::e"}, ports, HostType::IPv6);
    add_ip_ports(3, {"2001:b28:f23d:f003::e"}, ports, HostType::IPv6);
  } else {
    add_ip_ports(1, {"149.154.175.50"}, ports);
    add_ip_ports(2, {"149.154.167.51"}, ports);
    add_ip_ports(3, {"149.154.175.100"}, ports);
    add_ip_ports(4, {"149.154.167.91"}, ports);
    add_ip_ports(5, {"149.154.171.5"}, ports);

    add_ip_ports(1, {"2001:b28:f23d:f001::a"}, ports, HostType::IPv6);
    add_ip_ports(2, {"2001:67c:4e8:f002::a"}, ports, HostType::IPv6);
    add_ip_ports(3, {"2001:b28:f23d:f003::a"}, ports, HostType::IPv6);
    add_ip_ports(4, {"2001:67c:4e8:f004::a"}, ports, HostType::IPv6);
    add_ip_ports(5, {"2001:b28:f23f:f005::a"}, ports, HostType::IPv6);
  }
#endif
  return res;
}

void ConnectionCreator::loop() {
  if (!is_inited_) {
    return;
  }
  if (G()->close_flag()) {
    return;
  }
  if (!network_flag_) {
    return;
  }

  Timestamp timeout;
  if (active_proxy_id_ != 0 && proxies_[active_proxy_id_].type() == Proxy::Type::Mtproto) {
    if (get_proxy_info_timestamp_.is_in_past()) {
      if (get_proxy_info_query_token_ == 0) {
        get_proxy_info_query_token_ = next_token();
        auto query = G()->net_query_creator().create(create_storer(telegram_api::help_getProxyData()));
        G()->net_query_dispatcher().dispatch_with_callback(std::move(query),
                                                           actor_shared(this, get_proxy_info_query_token_));
      }
    } else {
      CHECK(get_proxy_info_query_token_ == 0);
      timeout.relax(get_proxy_info_timestamp_);
    }
  }

  if (active_proxy_id_ != 0) {
    if (resolve_proxy_timestamp_.is_in_past()) {
      if (resolve_proxy_query_token_ == 0) {
        resolve_proxy_query_token_ = next_token();
        const Proxy &proxy = proxies_[active_proxy_id_];
        bool prefer_ipv6 = G()->shared_config().get_option_boolean("prefer_ipv6");
        VLOG(connections) << "Resolve IP address " << resolve_proxy_query_token_ << " of " << proxy.server();
        send_closure(
            get_host_by_name_actor_, &GetHostByNameActor::run, proxy.server().str(), proxy.port(), prefer_ipv6,
            PromiseCreator::lambda([actor_id = create_reference(resolve_proxy_query_token_)](Result<IPAddress> result) {
              send_closure(std::move(actor_id), &ConnectionCreator::on_proxy_resolved, std::move(result), false);
            }));
      }
    } else {
      CHECK(resolve_proxy_query_token_ == 0);
      timeout.relax(resolve_proxy_timestamp_);
    }
  }

  if (timeout) {
    set_timeout_at(timeout.at());
  }
}

void ConnectionCreator::on_result(NetQueryPtr query) {
  SCOPE_EXIT {
    loop();
  };

  if (get_link_token() != get_proxy_info_query_token_) {
    return;
  }

  get_proxy_info_query_token_ = 0;
  auto res = fetch_result<telegram_api::help_getProxyData>(std::move(query));
  if (res.is_error()) {
    if (G()->close_flag()) {
      return;
    }
    LOG(ERROR) << "Receive error for getProxyData: " << res.error();
    return schedule_get_proxy_info(60);
  }
  on_get_proxy_info(res.move_as_ok());
}

void ConnectionCreator::on_get_proxy_info(telegram_api::object_ptr<telegram_api::help_ProxyData> proxy_data_ptr) {
  CHECK(proxy_data_ptr != nullptr);
  LOG(INFO) << "Receive " << to_string(proxy_data_ptr);
  int32 expires = 0;
  switch (proxy_data_ptr->get_id()) {
    case telegram_api::help_proxyDataEmpty::ID: {
      auto proxy = telegram_api::move_object_as<telegram_api::help_proxyDataEmpty>(proxy_data_ptr);
      expires = proxy->expires_;
      send_closure(G()->messages_manager(), &MessagesManager::on_get_sponsored_dialog_id, nullptr,
                   vector<tl_object_ptr<telegram_api::User>>(), vector<tl_object_ptr<telegram_api::Chat>>());
      break;
    }
    case telegram_api::help_proxyDataPromo::ID: {
      auto proxy = telegram_api::move_object_as<telegram_api::help_proxyDataPromo>(proxy_data_ptr);
      expires = proxy->expires_;
      send_closure(G()->messages_manager(), &MessagesManager::on_get_sponsored_dialog_id, std::move(proxy->peer_),
                   std::move(proxy->users_), std::move(proxy->chats_));
      break;
    }
    default:
      UNREACHABLE();
  }
  if (expires != 0) {
    expires -= G()->unix_time();
  }
  schedule_get_proxy_info(expires);
}

void ConnectionCreator::schedule_get_proxy_info(int32 expires) {
  if (expires < 0) {
    LOG(ERROR) << "Receive wrong expires: " << expires;
    expires = 0;
  }
  if (expires != 0 && expires < 60) {
    expires = 60;
  }
  if (expires > 86400) {
    expires = 86400;
  }
  get_proxy_info_timestamp_ = Timestamp::in(expires);
}

void ConnectionCreator::on_proxy_resolved(Result<IPAddress> r_ip_address, bool dummy) {
  SCOPE_EXIT {
    loop();
  };

  if (get_link_token() != resolve_proxy_query_token_) {
    VLOG(connections) << "Ignore unneeded proxy IP address " << get_link_token() << ", expected "
                      << resolve_proxy_query_token_;
    return;
  }

  resolve_proxy_query_token_ = 0;
  if (r_ip_address.is_error()) {
    VLOG(connections) << "Receive error for resolving proxy IP address: " << r_ip_address.error();
    resolve_proxy_timestamp_ = Timestamp::in(1 * 60);
    return;
  }
  proxy_ip_address_ = r_ip_address.move_as_ok();
  VLOG(connections) << "Set proxy IP address to " << proxy_ip_address_;
  resolve_proxy_timestamp_ = Timestamp::in(5 * 60);
  for (auto &client : clients_) {
    client_loop(client.second);
  }
}

void ConnectionCreator::on_ping_main_dc_result(uint64 token, Result<double> result) {
  auto &request = ping_main_dc_requests_[token];
  CHECK(request.left_queries > 0);
  if (result.is_error()) {
    LOG(DEBUG) << "Receive ping error " << result.error();
    if (request.result.is_error()) {
      request.result = std::move(result);
    }
  } else {
    LOG(DEBUG) << "Receive ping result " << result.ok();
    if (request.result.is_error() || request.result.ok() > result.ok()) {
      request.result = result.ok();
    }
  }

  if (--request.left_queries == 0) {
    if (request.result.is_error()) {
      request.promise.set_error(Status::Error(400, request.result.error().message()));
    } else {
      request.promise.set_value(request.result.move_as_ok());
    }
    ping_main_dc_requests_.erase(token);
  }
}

}  // namespace td
