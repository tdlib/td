// SPDX-FileCopyrightText: Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: BSL-1.0 AND MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/net/ConnectionCreator.h"

#include "td/telegram/net/ConnectionDestinationBudgetController.h"
#include "td/telegram/net/ConnectionPoolPolicy.h"
#include "td/telegram/net/ConnectionRetryPolicy.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/telegram/ConfigManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/MtprotoHeader.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetReliabilityMonitor.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/PromoDataManager.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/TdDb.h"

#include "td/mtproto/ErrorStatus.h"
#include "td/mtproto/Ping.h"
#include "td/mtproto/ProxySecret.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"
#include "td/mtproto/TlsInit.h"

#include "td/net/GetHostByNameActor.h"
#include "td/net/HttpProxy.h"
#include "td/net/Socks5.h"
#include "td/net/TransparentProxy.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/format.h"
#include "td/utils/HashTableUtils.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace td {

namespace {

int64 lifecycle_now_ms(double now_seconds = Time::now()) {
  return static_cast<int64>(now_seconds * 1000.0);
}

// Single-selection handoff: computes the one runtime profile for an emulate_tls
// connection attempt and stamps it onto transport_type.selected_profile, so the
// same profile reaches both the TLS ClientHello (TlsInit) and the transport-shaping
// config (create_transport) and one attempt cannot carry split profile state. Must
// run before the connection promise copies transport_type. No-op for non-emulate_tls
// transports; with the rotation policy disabled the result equals the legacy
// deterministic baseline.
void stamp_runtime_profile_selection(mtproto::TransportType &transport_type) {
  if (!transport_type.secret.emulate_tls()) {
    return;
  }
  auto domain = transport_type.secret.get_domain();
  auto route_hints =
      mtproto::stealth::route_hints_from_country_code(G()->get_option_string("stealth_route_country_code"));
  mtproto::stealth::set_runtime_ech_failure_store(G()->td_db()->get_config_pmc_shared());
  auto unix_time = static_cast<int32>(Time::now() + G()->get_dns_time_difference());
  auto platform = mtproto::stealth::default_runtime_platform_hints();
  auto ech_mode = mtproto::stealth::get_runtime_ech_decision(domain, unix_time, route_hints).ech_mode;
  transport_type.selected_profile =
      mtproto::stealth::pick_runtime_profile_adaptive(domain, unix_time, platform, ech_mode).profile;
}

int32 clamp_backoff_event_time_to_int32(double now) {
  if (!std::isfinite(now)) {
    return std::numeric_limits<int32>::max();
  }
  if (now <= static_cast<double>(std::numeric_limits<int32>::min())) {
    return std::numeric_limits<int32>::min();
  }
  if (now >= static_cast<double>(std::numeric_limits<int32>::max())) {
    return std::numeric_limits<int32>::max();
  }
  return static_cast<int32>(now);
}

string sanitize_lifecycle_label(Slice debug_str) {
  string result = debug_str.str();
  for (auto &c : result) {
    if (c == '|') {
      c = '/';
    }
  }
  if (result.empty()) {
    result = "unknown";
  }
  return result;
}

string make_lifecycle_destination(const IPAddress &ip_address, Slice debug_str) {
  return PSTRING() << ip_address << '|' << sanitize_lifecycle_label(debug_str);
}

string get_dc_option_signature(const DcOption &option) {
  uint32 secret_fingerprint = 0;
  for (auto byte : option.get_secret().get_raw_secret()) {
    secret_fingerprint = combine_hashes(secret_fingerprint, static_cast<uint32>(static_cast<uint8>(byte)));
  }
  return PSTRING() << option.get_dc_id().get_raw_id() << '|' << option.get_ip_address().get_ip_str() << ':'
                   << option.get_ip_address().get_port() << '|' << option.is_ipv6() << option.is_media_only()
                   << option.is_obfuscated_tcp_only() << option.is_static() << '|'
                   << option.get_secret().use_random_padding() << option.get_secret().emulate_tls() << '|'
                   << secret_fingerprint;
}

struct Ipv4Cidr {
  uint32 network;
  uint32 mask;
};

struct Ipv6Cidr {
  std::array<uint8, 16> network;
  uint8 prefix;
};

uint32 make_ipv4_mask(uint8 prefix) {
  if (prefix == 0) {
    return 0;
  }
  return std::numeric_limits<uint32>::max() << (32 - prefix);
}

bool ipv4_in_cidr(uint32 ip, const Ipv4Cidr &cidr) {
  return (ip & cidr.mask) == cidr.network;
}

std::array<uint8, 16> to_ipv6_octets(const IPAddress &ip_address) {
  std::array<uint8, 16> result{};
  auto ipv6 = ip_address.get_ipv6();
  if (ipv6.size() != 16) {
    return result;
  }
  for (size_t i = 0; i < result.size(); i++) {
    result[i] = static_cast<uint8>(ipv6[i]);
  }
  return result;
}

bool ipv6_in_cidr(const std::array<uint8, 16> &ip, const Ipv6Cidr &cidr) {
  size_t full_bytes = cidr.prefix / 8;
  uint8 tail_bits = cidr.prefix % 8;

  for (size_t i = 0; i < full_bytes; i++) {
    if (ip[i] != cidr.network[i]) {
      return false;
    }
  }
  if (tail_bits == 0) {
    return true;
  }

  uint8 mask = static_cast<uint8>(0xffu << (8 - tail_bits));
  return (ip[full_bytes] & mask) == (cidr.network[full_bytes] & mask);
}

bool is_forbidden_ipv4(uint32 ip) {
  static const std::array<Ipv4Cidr, 10> forbidden = {{
      {0x0a000000u, make_ipv4_mask(8)},   // 10.0.0.0/8
      {0xac100000u, make_ipv4_mask(12)},  // 172.16.0.0/12
      {0xc0a80000u, make_ipv4_mask(16)},  // 192.168.0.0/16
      {0x64400000u, make_ipv4_mask(10)},  // 100.64.0.0/10
      {0x7f000000u, make_ipv4_mask(8)},   // 127.0.0.0/8
      {0xa9fe0000u, make_ipv4_mask(16)},  // 169.254.0.0/16
      {0xe0000000u, make_ipv4_mask(4)},   // 224.0.0.0/4
      {0xc0000200u, make_ipv4_mask(24)},  // 192.0.2.0/24
      {0xc6336400u, make_ipv4_mask(24)},  // 198.51.100.0/24
      {0xcb007100u, make_ipv4_mask(24)},  // 203.0.113.0/24
  }};

  for (const auto &cidr : forbidden) {
    if (ipv4_in_cidr(ip, cidr)) {
      return true;
    }
  }
  return false;
}

bool is_forbidden_ipv6(const std::array<uint8, 16> &ip) {
  static const std::array<Ipv6Cidr, 4> forbidden = {{
      {{{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}}, 128},             // ::1/128
      {{{0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 10},        // fe80::/10
      {{{0xff, 0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 8},         // ff00::/8
      {{{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 32},  // 2001:db8::/32
  }};

  for (const auto &cidr : forbidden) {
    if (ipv6_in_cidr(ip, cidr)) {
      return true;
    }
  }
  return false;
}

bool is_reviewed_ipv4(uint32 ip) {
  static const std::array<Ipv4Cidr, 9> reviewed = {{
      {0x5b6c3800u, make_ipv4_mask(22)},  // 91.108.56.0/22
      {0x5b6c0400u, make_ipv4_mask(22)},  // 91.108.4.0/22
      {0x5b6c0800u, make_ipv4_mask(22)},  // 91.108.8.0/22
      {0x5b6c1000u, make_ipv4_mask(22)},  // 91.108.16.0/22
      {0x5b6c0c00u, make_ipv4_mask(22)},  // 91.108.12.0/22
      {0x959aa000u, make_ipv4_mask(20)},  // 149.154.160.0/20
      {0x5b69c000u, make_ipv4_mask(23)},  // 91.105.192.0/23
      {0x5b6c1400u, make_ipv4_mask(22)},  // 91.108.20.0/22
      {0xb94c9700u, make_ipv4_mask(24)},  // 185.76.151.0/24
  }};

  for (const auto &cidr : reviewed) {
    if (ipv4_in_cidr(ip, cidr)) {
      return true;
    }
  }
  return false;
}

bool is_reviewed_ipv6(const std::array<uint8, 16> &ip) {
  static const std::array<Ipv6Cidr, 5> reviewed = {{
      {{{0x20, 0x01, 0x0b, 0x28, 0xf2, 0x3d, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 48},
      {{{0x20, 0x01, 0x0b, 0x28, 0xf2, 0x3f, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 48},
      {{{0x20, 0x01, 0x06, 0x7c, 0x04, 0xe8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 48},
      {{{0x20, 0x01, 0x0b, 0x28, 0xf2, 0x3c, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 48},
      {{{0x2a, 0x0a, 0xf2, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}}, 32},
  }};

  for (const auto &cidr : reviewed) {
    if (ipv6_in_cidr(ip, cidr)) {
      return true;
    }
  }
  return false;
}

bool is_forbidden_route_address(const IPAddress &ip_address) {
  if (!ip_address.is_valid()) {
    return true;
  }
  if (ip_address.is_ipv4()) {
    return is_forbidden_ipv4(ip_address.get_ipv4());
  }
  if (ip_address.is_ipv6()) {
    return is_forbidden_ipv6(to_ipv6_octets(ip_address));
  }
  return true;
}

Slice proxy_mode_name(const Proxy &proxy) {
  if (!proxy.use_proxy()) {
    return Slice("direct");
  }
  if (proxy.use_mtproto_proxy()) {
    return Slice("mtproto");
  }
  if (proxy.use_socks5_proxy()) {
    return Slice("socks5");
  }
  if (proxy.use_http_tcp_proxy()) {
    return Slice("http_tcp");
  }
  if (proxy.use_http_caching_proxy()) {
    return Slice("http_caching");
  }
  return Slice("unknown");
}

Slice raw_ip_transport_name(const mtproto::TransportType &transport_type) {
  switch (transport_type.type) {
    case mtproto::TransportType::Tcp:
      return Slice("tcp");
    case mtproto::TransportType::ObfuscatedTcp:
      return Slice("obfuscated_tcp");
    case mtproto::TransportType::Http:
      return Slice("http");
    default:
      return Slice("unknown");
  }
}

string mtproto_tunneled_ip_for_log(const IPAddress &ip_address) {
  if (!ip_address.is_valid()) {
    return "none";
  }
  return ip_address.get_ip_str().str();
}

Slice active_policy_name(mtproto::stealth::RuntimeActivePolicy policy) {
  switch (policy) {
    case mtproto::stealth::RuntimeActivePolicy::RuEgress:
      return Slice("ru_egress");
    case mtproto::stealth::RuntimeActivePolicy::NonRuEgress:
      return Slice("non_ru_egress");
    case mtproto::stealth::RuntimeActivePolicy::Unknown:
    default:
      return Slice("unknown");
  }
}

bool quic_enabled_for_active_policy(const mtproto::stealth::StealthRuntimeParams &params) {
  switch (params.active_policy) {
    case mtproto::stealth::RuntimeActivePolicy::RuEgress:
      return params.route_policy.ru.allow_quic;
    case mtproto::stealth::RuntimeActivePolicy::NonRuEgress:
      return params.route_policy.non_ru.allow_quic;
    case mtproto::stealth::RuntimeActivePolicy::Unknown:
    default:
      return params.route_policy.unknown.allow_quic;
  }
}

bool is_ipv6_mapped_ipv4(const IPAddress &ip_address) {
  if (!ip_address.is_ipv6()) {
    return false;
  }
  auto ipv6 = ip_address.get_ipv6();
  if (ipv6.size() != 16) {
    return false;
  }
  for (size_t i = 0; i < 10; i++) {
    if (ipv6[i] != 0) {
      return false;
    }
  }
  return static_cast<unsigned char>(ipv6[10]) == 0xff && static_cast<unsigned char>(ipv6[11]) == 0xff;
}

Result<IPAddress> normalize_peer_address(const IPAddress &ip_address) {
  if (!ip_address.is_valid()) {
    return Status::Error("Peer address is invalid");
  }
  if (!is_ipv6_mapped_ipv4(ip_address)) {
    return ip_address;
  }

  auto ipv6 = ip_address.get_ipv6();
  IPAddress normalized_ip_address;
  TRY_STATUS(normalized_ip_address.init_ipv4_port(PSTRING()
                                                      << static_cast<int>(static_cast<unsigned char>(ipv6[12])) << '.'
                                                      << static_cast<int>(static_cast<unsigned char>(ipv6[13])) << '.'
                                                      << static_cast<int>(static_cast<unsigned char>(ipv6[14])) << '.'
                                                      << static_cast<int>(static_cast<unsigned char>(ipv6[15])),
                                                  ip_address.get_port()));
  return normalized_ip_address;
}

}  // namespace

std::atomic<int> VERBOSITY_NAME(connections) = VERBOSITY_NAME(INFO);

namespace detail {

class StatsCallback final : public mtproto::RawConnection::StatsCallback {
 public:
  StatsCallback(std::shared_ptr<NetStatsCallback> net_stats_callback, ActorId<ConnectionCreator> connection_creator,
                uint32 hash, DcOptionsSet::Stat *option_stat,
                std::shared_ptr<td::ConnectionLifecycleReportBuilder> connection_lifecycle_report)
      : net_stats_callback_(std::move(net_stats_callback))
      , connection_creator_(std::move(connection_creator))
      , hash_(hash)
      , option_stat_(option_stat)
      , connection_lifecycle_report_(std::move(connection_lifecycle_report)) {
  }

  void on_read(uint64 bytes) final {
    if (net_stats_callback_ != nullptr) {
      net_stats_callback_->on_read(bytes);
    }
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->add_read(connection_id_, bytes);
    }
  }
  void on_write(uint64 bytes) final {
    if (net_stats_callback_ != nullptr) {
      net_stats_callback_->on_write(bytes);
    }
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->add_write(connection_id_, bytes);
    }
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

  void on_connection_open(uint64 connection_id, Slice destination, int64 started_at_ms) final {
    connection_id_ = connection_id;
    if (connection_lifecycle_report_) {
      connection_lifecycle_report_->begin_connection(connection_id_, destination.str(), started_at_ms, false);
    }
  }

  void on_connection_reused() final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->mark_reused(connection_id_);
    }
  }

  void on_connection_closed(int64 ended_at_ms) final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->end_connection(connection_id_, ended_at_ms);
    }
  }

  void on_connection_role(Slice role) final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->set_role(connection_id_, role);
    }
  }

  void on_connection_rotation_reason(Slice reason) final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->set_rotation_reason(connection_id_, reason);
    }
  }

  void on_connection_successor_opened(int64 successor_opened_at_ms) final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->set_successor_opened_at(connection_id_, successor_opened_at_ms);
    }
  }

  void on_connection_overlap(uint64 overlap_ms) final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->set_overlap_ms(connection_id_, overlap_ms);
    }
  }

  void on_connection_over_age_status(bool over_age_degraded, Slice exemption) final {
    if (connection_lifecycle_report_ && connection_id_ != 0) {
      connection_lifecycle_report_->set_over_age_status(connection_id_, over_age_degraded, exemption);
    }
  }

 private:
  std::shared_ptr<NetStatsCallback> net_stats_callback_;
  ActorId<ConnectionCreator> connection_creator_;
  uint32 hash_;
  DcOptionsSet::Stat *option_stat_;
  std::shared_ptr<td::ConnectionLifecycleReportBuilder> connection_lifecycle_report_;
  uint64 connection_id_{0};
};

}  // namespace detail

ConnectionCreator::ClientInfo::ClientInfo() {
  sanity_flood_control.add_limit(5, 10);

  flood_control.add_limit(1, 1);
  flood_control.add_limit(4, 2);
  flood_control.add_limit(8, 3);

  flood_control_online.add_limit(1, 4);
  flood_control_online.add_limit(5, 5);

  mtproto_error_flood_control.add_limit(1, 1);
  mtproto_error_flood_control.add_limit(4, 2);
  mtproto_error_flood_control.add_limit(8, 3);
}

uint64 ConnectionCreator::ClientInfo::extract_session_id() {
  if (!session_ids_.empty()) {
    auto res = *session_ids_.begin();
    session_ids_.erase(session_ids_.begin());
    return res;
  }
  uint64 res = 0;
  while (res == 0) {
    res = Random::secure_uint64();
  }
  return res;
}

void ConnectionCreator::ClientInfo::add_session_id(uint64 session_id) {
  if (session_id != 0) {
    session_ids_.insert(session_id);
  }
}

ConnectionCreator::ConnectionCreator(ActorShared<> parent) : parent_(std::move(parent)) {
}

ConnectionCreator::ConnectionCreator(ConnectionCreator &&) = default;

ConnectionCreator &ConnectionCreator::operator=(ConnectionCreator &&) = default;

ConnectionCreator::~ConnectionCreator() = default;

void ConnectionCreator::set_net_stats_callback(std::shared_ptr<NetStatsCallback> common_callback,
                                               std::shared_ptr<NetStatsCallback> media_callback) {
  common_net_stats_callback_ = std::move(common_callback);
  media_net_stats_callback_ = std::move(media_callback);
}

string ConnectionCreator::get_connection_lifecycle_report_json() const {
  auto runtime_params = mtproto::stealth::get_runtime_stealth_params_snapshot();
  return connection_lifecycle_report_->to_json(active_policy_name(runtime_params.active_policy),
                                               quic_enabled_for_active_policy(runtime_params));
}

void ConnectionCreator::add_proxy(int32 old_proxy_id, td_api::object_ptr<td_api::proxy> proxy, bool enable,
                                  Promise<td_api::object_ptr<td_api::addedProxy>> promise) {
  TRY_RESULT_PROMISE(promise, new_proxy, Proxy::create_proxy(proxy.get()));
  if (old_proxy_id >= 0) {
    auto old_proxy_it = proxies_.find(old_proxy_id);
    if (old_proxy_it == proxies_.end()) {
      return promise.set_error(400, "Proxy not found");
    }
    const auto &old_proxy = old_proxy_it->second;
    if (old_proxy == new_proxy) {
      if (enable) {
        enable_proxy_impl(old_proxy_id);
      }
      return promise.set_value(get_added_proxy_object(old_proxy_id));
    }
    if (old_proxy_id == active_proxy_id_) {
      enable = true;
      disable_proxy_impl();
    }

    proxies_.erase(old_proxy_id);
    G()->td_db()->get_binlog_pmc()->erase(get_proxy_used_database_key(old_proxy_id));
    proxy_last_used_date_.erase(old_proxy_id);
    proxy_last_used_saved_date_.erase(old_proxy_id);
  } else {
#if TD_EMSCRIPTEN || TD_DARWIN_WATCH_OS
    return promise.set_error(400, "The method is unsupported for the platform");
#endif
  }

  auto proxy_id = [&] {
    for (const auto &[existing_proxy_id, existing_proxy] : proxies_) {
      if (existing_proxy == new_proxy) {
        return existing_proxy_id;
      }
    }

    int32 proxy_id = old_proxy_id;
    if (proxy_id < 0) {
      CHECK(max_proxy_id_ >= 2);
      proxy_id = max_proxy_id_++;
      G()->td_db()->get_binlog_pmc()->set("proxy_max_id", to_string(max_proxy_id_));
    }
    bool is_inserted = proxies_.try_emplace(proxy_id, std::move(new_proxy)).second;
    CHECK(is_inserted);
    G()->td_db()->get_binlog_pmc()->set(get_proxy_database_key(proxy_id),
                                        log_event_store(proxies_[proxy_id]).as_slice().str());
    return proxy_id;
  }();
  if (enable) {
    enable_proxy_impl(proxy_id);
  }
  promise.set_value(get_added_proxy_object(proxy_id));
}

void ConnectionCreator::enable_proxy(int32 proxy_id, Promise<Unit> promise) {
  if (!proxies_.contains(proxy_id)) {
    return promise.set_error(400, "Unknown proxy identifier");
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
  if (!proxies_.contains(proxy_id)) {
    return promise.set_error(400, "Unknown proxy identifier");
  }

  if (proxy_id == active_proxy_id_) {
    disable_proxy_impl();
  }

  proxies_.erase(proxy_id);

  G()->td_db()->get_binlog_pmc()->erase(get_proxy_database_key(proxy_id));
  G()->td_db()->get_binlog_pmc()->erase(get_proxy_used_database_key(proxy_id));
  promise.set_value(Unit());
}

void ConnectionCreator::get_proxies(Promise<td_api::object_ptr<td_api::addedProxies>> promise) {
  promise.set_value(td_api::make_object<td_api::addedProxies>(transform(
      proxies_, [this](const std::pair<int32, Proxy> &proxy) { return get_added_proxy_object(proxy.first); })));
}

ActorId<GetHostByNameActor> ConnectionCreator::get_dns_resolver() {
  auto dns_type = G()->get_option_string("dns_type");
  auto custom_dns_url = G()->get_option_string("custom_dns_url");
  auto custom_dns_headers_str = G()->get_option_string("custom_dns_headers");

  GetHostByNameActor::Options options;
  options.scheduler_id = G()->get_gc_scheduler_id();
  options.ok_timeout = 5 * 60 - 1;
  options.error_timeout = 0;

  bool has_custom = !custom_dns_url.empty();
  bool is_blocking = G()->get_option_boolean("expect_blocking", true);

  if (has_custom) {
    options.resolver_types = {GetHostByNameActor::ResolverType::Custom};
    options.custom_doh_url = custom_dns_url;

    if (!custom_dns_headers_str.empty()) {
      std::vector<std::pair<string, string>> headers;
      string headers_str = custom_dns_headers_str;
      size_t colon = headers_str.find(':');
      if (colon != string::npos) {
        string key = headers_str.substr(0, colon);
        size_t value_start = colon + 1;
        while (value_start < headers_str.size() &&
               (headers_str[value_start] == ' ' || headers_str[value_start] == '\t'))
          value_start++;
        string value = headers_str.substr(value_start);
        headers.emplace_back(std::move(key), std::move(value));
      }
      options.custom_doh_headers = std::move(headers);
    }

    if (is_blocking) {
      options.ok_timeout = 60;
      options.error_timeout = 0;
    }
  } else if (dns_type == "custom") {
    if (!custom_dns_url.empty()) {
      // Custom already handled above in has_custom block
    } else {
      // Fall back to Google if custom selected but no URL
      options.resolver_types = {GetHostByNameActor::ResolverType::Google, GetHostByNameActor::ResolverType::CloudFlare};
    }
  } else if (dns_type == "cloudflare") {
    options.resolver_types = {GetHostByNameActor::ResolverType::CloudFlare, GetHostByNameActor::ResolverType::Google};
  } else if (dns_type == "google") {
    options.resolver_types = {GetHostByNameActor::ResolverType::Google, GetHostByNameActor::ResolverType::CloudFlare};
  } else {
    options.resolver_types = {GetHostByNameActor::ResolverType::Google, GetHostByNameActor::ResolverType::CloudFlare};
  }

  if (is_blocking) {
    if (block_get_host_by_name_actor_.empty()) {
      VLOG(connections) << "Init block bypass DNS resolver";
      block_get_host_by_name_actor_ = create_actor<GetHostByNameActor>("BlockDnsResolverActor", std::move(options));
    }
    return block_get_host_by_name_actor_.get();
  } else {
    if (get_host_by_name_actor_.empty()) {
      VLOG(connections) << "Init DNS resolver";
      get_host_by_name_actor_ = create_actor<GetHostByNameActor>("DnsResolverActor", std::move(options));
    }
    return get_host_by_name_actor_.get();
  }
}

void ConnectionCreator::ping_proxy(td_api::object_ptr<td_api::proxy> input_proxy, Promise<double> promise) {
  CHECK(!close_flag_);
  Proxy active_proxy = active_proxy_id_ == 0 ? Proxy() : proxies_[active_proxy_id_];

  // Resolve the effective proxy in a single linear flow so that the storage
  // backing the optional `requested_proxy` outlives every reference to it.
  //
  // The previous form of this block declared `Proxy requested_proxy;` at
  // function scope, then used `TRY_RESULT_PROMISE(promise, requested_proxy,
  // Proxy::create_proxy(input_proxy.get()))` inside an `if (input_proxy)`
  // block. The macro expands to a fresh `auto requested_proxy = ...`
  // declaration, which silently SHADOWS the outer variable. The inner shadow
  // lives only until the closing `}` of the `if`, so a `requested_proxy_ptr =
  // &requested_proxy;` line right above that brace captured the address of
  // the inner shadow, and the subsequent
  // `resolve_effective_ping_proxy(active_proxy, requested_proxy_ptr)` call
  // outside the `if` dereferenced a dangling stack pointer.
  //
  // PVS-Studio V506 and the MSVC C4456 shadowing warning both flagged the
  // pattern. The fix lifts the optional proxy into its own owning slot
  // (`std::unique_ptr<Proxy>`) created and consumed in a single statement
  // sequence, with no shadowing and no out-of-scope address-of.
  std::unique_ptr<Proxy> requested_proxy;
  if (input_proxy != nullptr) {
    auto r_proxy = Proxy::create_proxy(input_proxy.get());
    if (r_proxy.is_error()) {
      return promise.set_error(r_proxy.move_as_error());
    }
    requested_proxy = std::make_unique<Proxy>(r_proxy.move_as_ok());
  }
  auto proxy = resolve_effective_ping_proxy(active_proxy, requested_proxy.get());

  if (!proxy.use_proxy()) {
    auto main_dc_id = G()->net_query_dispatcher().get_main_dc_id();
    bool prefer_ipv6 = G()->get_option_boolean("prefer_ipv6");
    auto infos = dc_options_set_.find_all_connections(main_dc_id, false, false, prefer_ipv6, false);
    if (infos.empty()) {
      return promise.set_error(400, "Can't find valid DC address");
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
      auto r_transport_type = get_transport_type(Proxy(), info);
      if (r_transport_type.is_error()) {
        auto error = r_transport_type.move_as_error();
        LOG(ERROR) << "Ping main DC transport resolution failed" << tag("dc_id", main_dc_id)
                   << tag("target_ip", info.option->get_ip_address()) << tag("status_code", error.code())
                   << tag("status_message", sanitize_connection_failure_status_message_for_log(error));
        on_ping_main_dc_result(token, std::move(error));
        continue;
      }

      auto ip_address = info.option->get_ip_address();
      auto r_socket_fd = SocketFd::open(ip_address);
      if (r_socket_fd.is_error()) {
        auto error = r_socket_fd.move_as_error();
        LOG(WARNING) << "Ping main DC socket open failed" << tag("dc_id", main_dc_id) << tag("target_ip", ip_address)
                     << tag("status_code", error.code())
                     << tag("status_message", sanitize_connection_failure_status_message_for_log(error));
        on_ping_main_dc_result(token, std::move(error));
        continue;
      }

      ping_proxy_buffered_socket_fd(std::move(ip_address), BufferedFd<SocketFd>(r_socket_fd.move_as_ok()),
                                    r_transport_type.move_as_ok(), PSTRING() << info.option->get_ip_address(), Proxy(),
                                    PromiseCreator::lambda([actor_id = actor_id(this), token](Result<double> result) {
                                      send_closure(actor_id, &ConnectionCreator::on_ping_main_dc_result, token,
                                                   std::move(result));
                                    }));
    }
    return;
  }

  bool prefer_ipv6 = G()->get_option_boolean("prefer_ipv6");
  send_closure(get_dns_resolver(), &GetHostByNameActor::run, proxy.server().str(), proxy.port(), prefer_ipv6,
               PromiseCreator::lambda(
                   [actor_id = actor_id(this), promise = std::move(promise), proxy](Result<IPAddress> result) mutable {
                     if (result.is_error()) {
                       auto error = result.move_as_error();
                       LOG(WARNING) << "Ping proxy DNS resolve failed" << tag("proxy_mode", proxy_mode_name(proxy))
                                    << tag("proxy_server", proxy.server()) << tag("proxy_port", proxy.port())
                                    << tag("status_code", error.code())
                                    << tag("status_message", sanitize_connection_failure_status_message_for_log(error));
                       return promise.set_error(400, error.public_message());
                     }
                     send_closure(actor_id, &ConnectionCreator::ping_proxy_resolved, std::move(proxy),
                                  result.move_as_ok(), std::move(promise));
                   }));
}

void ConnectionCreator::ping_proxy_resolved(Proxy &&proxy, IPAddress ip_address, Promise<double> promise) {
  auto main_dc_id = G()->net_query_dispatcher().get_main_dc_id();
  FindConnectionExtra extra;
  auto r_socket_fd = find_connection(proxy, ip_address, main_dc_id, false, extra);
  if (r_socket_fd.is_error()) {
    auto error = r_socket_fd.move_as_error();
    LOG(WARNING) << "Ping proxy route resolution failed" << tag("proxy_mode", proxy_mode_name(proxy))
                 << tag("proxy_server", proxy.server()) << tag("proxy_port", proxy.port())
                 << tag("resolved_proxy_ip", ip_address) << tag("status_code", error.code())
                 << tag("status_message", sanitize_connection_failure_status_message_for_log(error));
    return promise.set_error(400, error.public_message());
  }
  auto socket_fd = r_socket_fd.move_as_ok();

  stamp_runtime_profile_selection(extra.transport_type);
  auto connection_promise = PromiseCreator::lambda([actor_id = actor_id(this), ip_address, promise = std::move(promise),
                                                    transport_type = extra.transport_type, debug_str = extra.debug_str,
                                                    proxy](Result<ConnectionData> r_connection_data) mutable {
    if (r_connection_data.is_error()) {
      auto error = r_connection_data.move_as_error();
      LOG(WARNING) << "Ping proxy transport setup failed" << tag("proxy_mode", proxy_mode_name(proxy))
                   << tag("proxy_server", proxy.server()) << tag("proxy_port", proxy.port())
                   << tag("transport", raw_ip_transport_name(transport_type))
                   << tag("transport_dc_id", transport_type.dc_id)
                   << tag("tls_emulation", transport_type.secret.emulate_tls()) << tag("target_ip", ip_address)
                   << tag("debug_route", debug_str) << tag("status_code", error.code())
                   << tag("status_message", sanitize_connection_failure_status_message_for_log(error));
      return promise.set_error(400, error.public_message());
    }
    auto connection_data = r_connection_data.move_as_ok();
    send_closure(actor_id, &ConnectionCreator::ping_proxy_buffered_socket_fd, ip_address,
                 std::move(connection_data.buffered_socket_fd), std::move(transport_type), std::move(debug_str),
                 std::move(proxy), std::move(promise));
  });
  CHECK(proxy.use_proxy());
  auto token = next_token();
  auto ref = prepare_connection(extra.ip_address, std::move(socket_fd), proxy, extra.mtproto_ip_address,
                                extra.transport_type, "Ping", extra.debug_str, nullptr, create_reference(token), false,
                                std::move(connection_promise));
  if (!ref.empty()) {
    children_[token] = {false, std::move(ref)};
  }
}

void ConnectionCreator::ping_proxy_buffered_socket_fd(IPAddress ip_address, BufferedFd<SocketFd> buffered_socket_fd,
                                                      mtproto::TransportType transport_type, string debug_str,
                                                      Proxy proxy_context, Promise<double> promise) {
  auto token = next_token();
  auto transport_type_for_log = transport_type;
  auto raw_connection =
      mtproto::RawConnection::create(ip_address, std::move(buffered_socket_fd), std::move(transport_type), nullptr);
  children_[token] = {
      false,
      create_ping_actor(
          debug_str, std::move(raw_connection), nullptr,
          PromiseCreator::lambda(
              [promise = std::move(promise), ip_address, transport_type_for_log, debug_str = std::move(debug_str),
               proxy_context = std::move(proxy_context)](Result<unique_ptr<mtproto::RawConnection>> result) mutable {
                if (result.is_error()) {
                  auto error = result.move_as_error();
                  auto classification = classify_connection_failure(true, proxy_context, error);
                  LOG(WARNING) << "Ping probe handshake failed" << tag("proxy_mode", proxy_mode_name(proxy_context))
                               << tag("transport", raw_ip_transport_name(transport_type_for_log))
                               << tag("transport_dc_id", transport_type_for_log.dc_id)
                               << tag("tls_emulation", transport_type_for_log.secret.emulate_tls())
                               << tag("target_ip", ip_address) << tag("debug_route", debug_str)
                               << tag("status_code", error.code())
                               << tag("status_message", sanitize_connection_failure_status_message_for_log(error))
                               << tag("failure_summary", summarize_connection_failure_for_log(classification, error));
                  return promise.set_error(400, error.public_message());
                }
                auto ping_time = result.ok()->extra().rtt;
                promise.set_value(std::move(ping_time));
              }),
          create_reference(token))};
}

void ConnectionCreator::set_active_proxy_id(int32 proxy_id, bool from_binlog) {
  active_proxy_id_ = proxy_id;
  if (proxy_id == 0) {
    G()->set_option_empty("enabled_proxy_id");
  } else {
    G()->set_option_integer("enabled_proxy_id", proxy_id);
  }
  if (!from_binlog) {
    if (proxy_id == 0) {
      G()->td_db()->get_binlog_pmc()->erase("proxy_active_id");
      send_closure(G()->config_manager(), &ConfigManager::request_config, false);
    } else {
      G()->td_db()->get_binlog_pmc()->set("proxy_active_id", to_string(proxy_id));
    }
  }
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

  set_active_proxy_id(proxy_id);

  on_proxy_changed(false);
}

void ConnectionCreator::disable_proxy_impl() {
  if (active_proxy_id_ == 0) {
    send_closure(G()->promo_data_manager(), &PromoDataManager::remove_sponsored_dialog);
    send_closure(G()->promo_data_manager(), &PromoDataManager::reload_promo_data);
    return;
  }
  CHECK(proxies_.count(active_proxy_id_) == 1);

  if (proxies_[active_proxy_id_].type() == Proxy::Type::Mtproto) {
    update_mtproto_header(Proxy());
  }

  set_active_proxy_id(0);

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

  if (active_proxy_id_ == 0 || !from_db) {
    send_closure(G()->promo_data_manager(), &PromoDataManager::remove_sponsored_dialog);
  }
  send_closure(G()->promo_data_manager(), &PromoDataManager::reload_promo_data);

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

td_api::object_ptr<td_api::addedProxy> ConnectionCreator::get_added_proxy_object(int32 proxy_id) const {
  auto it = proxies_.find(proxy_id);
  CHECK(it != proxies_.end());
  auto last_used_date_it = proxy_last_used_date_.find(proxy_id);
  auto last_used_date = last_used_date_it == proxy_last_used_date_.end() ? 0 : last_used_date_it->second;
  return td_api::make_object<td_api::addedProxy>(proxy_id, last_used_date, proxy_id == active_proxy_id_,
                                                 it->second.get_proxy_object());
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

    for (auto &client : clients_) {
      client.second.backoff.clear();
      client.second.sanity_flood_control.clear_events();
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
  bool need_drop_flood_control = online_flag || !online_flag_;
  online_flag_ = online_flag;
  if (need_drop_flood_control) {
    for (auto &client : clients_) {
      client.second.backoff.clear();
      client.second.sanity_flood_control.clear_events();
      client.second.flood_control_online.clear_events();
      client_loop(client.second);
    }
  }
}
void ConnectionCreator::on_logging_out(bool is_logging_out) {
  if (is_logging_out_ == is_logging_out) {
    return;
  }

  VLOG(connections) << "Receive logging out flag " << is_logging_out;
  is_logging_out_ = is_logging_out;
  for (auto &client : clients_) {
    client.second.backoff.clear();
    client.second.sanity_flood_control.clear_events();
    client.second.flood_control_online.clear_events();
    client_loop(client.second);
  }
}

void ConnectionCreator::on_pong(uint32 hash) {
  G()->save_server_time();
  if (active_proxy_id_ != 0) {
    auto now = G()->unix_time();
    int32 &last_used = proxy_last_used_date_[active_proxy_id_];
    if (now > last_used) {
      last_used = now;
      save_proxy_last_used_date(MAX_PROXY_LAST_USED_SAVE_DELAY);
    }
  }
}

void ConnectionCreator::on_mtproto_error(uint32 hash) {
  auto &client = clients_[hash];
  client.hash = hash;
  client.mtproto_error_flood_control.add_event(Time::now_cached());
}

void ConnectionCreator::request_raw_connection(
    DcId dc_id, bool allow_media_only, bool is_media, Promise<unique_ptr<mtproto::RawConnection>> promise,
    std::shared_ptr<ConnectionRotationGateSnapshotHandle> rotation_gate_snapshot, uint32 hash,
    unique_ptr<mtproto::AuthData> auth_data) {
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
  if (rotation_gate_snapshot != nullptr) {
    client.rotation_gate_snapshot = std::move(rotation_gate_snapshot);
  }
  client.auth_data = std::move(auth_data);
  client.auth_data_generation++;
  VLOG(connections) << "Request connection for " << tag("client", format::as_hex(client.hash)) << " to " << dc_id << " "
                    << tag("allow_media_only", allow_media_only);
  client.queries.push_back(std::move(promise));

  client_loop(client);
}

Result<ConnectionCreator::RawIpConnectionRoute> ConnectionCreator::resolve_raw_ip_connection_route(
    const Proxy &proxy, const IPAddress &proxy_ip_address, const IPAddress &target_ip_address) {
  if (!target_ip_address.is_valid()) {
    LOG(WARNING) << "Raw-IP route validation failed" << tag("reason", "invalid_target_ip")
                 << tag("proxy_mode", proxy_mode_name(proxy)) << tag("target_ip_valid", false)
                 << tag("proxy_ip_valid", proxy_ip_address.is_valid())
                 << tag("tls_emulation", proxy.secret().emulate_tls());
    return Status::Error("Target IP address is invalid");
  }

  if (proxy.use_proxy() && !proxy_ip_address.is_valid()) {
    LOG(WARNING) << "Raw-IP route validation failed" << tag("reason", "invalid_proxy_ip")
                 << tag("proxy_mode", proxy_mode_name(proxy)) << tag("target_ip_valid", true)
                 << tag("proxy_ip_valid", false) << tag("tls_emulation", proxy.secret().emulate_tls());
    return Status::Error("Proxy IP address is invalid");
  }

  RawIpConnectionRoute route;
  route.debug_str = PSTRING() << "to IP address " << target_ip_address;
  VLOG(connections) << "Resolve raw-IP route" << tag("proxy_mode", proxy_mode_name(proxy))
                    << tag("tls_emulation", proxy.secret().emulate_tls()) << tag("target_ip", target_ip_address)
                    << tag("proxy_ip", proxy_ip_address);

  if (!proxy.use_proxy()) {
    route.socket_ip_address = target_ip_address;
    return std::move(route);
  }

  if (proxy.use_mtproto_proxy()) {
    route.socket_ip_address = proxy_ip_address;
    route.debug_str = PSTRING() << "MTProto " << proxy_ip_address << ' ' << route.debug_str;
    return std::move(route);
  }

  if (proxy.use_socks5_proxy() || proxy.use_http_tcp_proxy()) {
    route.socket_ip_address = proxy_ip_address;
    route.mtproto_ip_address = target_ip_address;
    route.debug_str = PSTRING() << (proxy.use_socks5_proxy() ? "Socks5" : "HTTP_TCP") << ' ' << proxy_ip_address
                                << " --> " << target_ip_address << ' ' << route.debug_str;
    return std::move(route);
  }

  return Status::Error("HTTP caching proxy is unsupported for explicit IP connection requests");
}

Result<mtproto::TransportType> ConnectionCreator::resolve_raw_ip_transport_type(
    const Proxy &proxy, const mtproto::TransportType &requested_transport_type) {
  if (!proxy.use_proxy()) {
    return requested_transport_type;
  }

  if (proxy.use_mtproto_proxy()) {
    if (requested_transport_type.type != mtproto::TransportType::ObfuscatedTcp) {
      return Status::Error("MTProto proxy raw-IP route requires ObfuscatedTcp transport");
    }
    TRY_RESULT(validated_proxy_secret, mtproto::ProxySecret::from_binary(proxy.secret().get_raw_secret()));
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, requested_transport_type.dc_id,
                                  std::move(validated_proxy_secret)};
  }

  return requested_transport_type;
}

Proxy ConnectionCreator::resolve_effective_ping_proxy(const Proxy &active_proxy, const Proxy *requested_proxy) {
  if (requested_proxy != nullptr) {
    return *requested_proxy;
  }
  return active_proxy;
}

bool ConnectionCreator::should_prefer_ipv6_for_dc_options([[maybe_unused]] const Proxy &proxy, bool user_prefer_ipv6,
                                                          [[maybe_unused]] const IPAddress &resolved_proxy_ip_address) {
  return user_prefer_ipv6;
}

Result<ConnectionCreator::ProxyAddressCandidates> ConnectionCreator::resolve_proxy_address_candidates(
    const Proxy &proxy, const IPAddress &resolved_proxy_ip_address) {
  if (!proxy.use_proxy()) {
    return Status::Error("Proxy socket resolution requested while proxy mode is disabled");
  }
  if (!resolved_proxy_ip_address.is_valid()) {
    return Status::Error(PSLICE() << "Proxy IP address is invalid for " << proxy_mode_name(proxy) << " proxy "
                                  << proxy.server() << ':' << proxy.port());
  }

  ProxyAddressCandidates candidates;
  candidates.primary_ip_address = resolved_proxy_ip_address;

  if (IPAddress::get_ip_address(proxy.server()).is_ok()) {
    return std::move(candidates);
  }

  IPAddress fallback_ip_address;
  auto fallback_status =
      fallback_ip_address.init_host_port(proxy.server(), proxy.port(), !resolved_proxy_ip_address.is_ipv6());
  if (fallback_status.is_ok() && !(fallback_ip_address == resolved_proxy_ip_address) &&
      fallback_ip_address.get_address_family() != resolved_proxy_ip_address.get_address_family()) {
    candidates.fallback_ip_address = std::move(fallback_ip_address);
  }

  return std::move(candidates);
}

Result<ConnectionCreator::ProxySocketOpenResult> ConnectionCreator::open_proxy_socket(
    const Proxy &proxy, const IPAddress &resolved_proxy_ip_address) {
  TRY_RESULT(candidates, resolve_proxy_address_candidates(proxy, resolved_proxy_ip_address));

  auto try_open = [](const IPAddress &ip_address) -> Result<ProxySocketOpenResult> {
    TRY_RESULT(socket_fd, SocketFd::open(ip_address));
#if TD_PORT_POSIX
    // POSIX: probe SO_ERROR immediately after `SocketFd::open` so the
    // non-blocking `connect()` failure surfaces here instead of at the
    // first read/write. Windows-side IOCP reports the same condition via
    // the completion callback path of `get_socket_pending_error(fd,
    // overlapped, iocp_error)`, so we skip the probe here on Windows
    // rather than calling the 3-arg signature without a valid overlapped
    // handle (which would fail to compile and also produce a misleading
    // read of the handle state before IOCP has advanced).
    TRY_STATUS(detail::get_socket_pending_error(socket_fd.get_native_fd()));
#endif

    ProxySocketOpenResult result;
    result.socket_fd = std::move(socket_fd);
    result.connected_proxy_ip_address = ip_address;
    return std::move(result);
  };

  auto r_socket = try_open(candidates.primary_ip_address);
  if (r_socket.is_ok()) {
    return r_socket;
  }

  if (!candidates.fallback_ip_address.is_valid()) {
    auto primary_error = r_socket.move_as_error();
    return Status::Error(primary_error.code(), PSLICE() << "Failed to connect to " << proxy_mode_name(proxy)
                                                        << " proxy " << proxy.server() << ':' << proxy.port() << " via "
                                                        << candidates.primary_ip_address << ": "
                                                        << primary_error.public_message());
  }

  const auto &primary_error_for_retry = r_socket.error();
  VLOG(connections) << "Retry proxy connect via alternate family " << candidates.fallback_ip_address
                    << tag("status_code", primary_error_for_retry.code())
                    << tag("status_message",
                           sanitize_connection_failure_status_message_for_log(primary_error_for_retry));
  auto r_fallback_socket = try_open(candidates.fallback_ip_address);
  if (r_fallback_socket.is_ok()) {
    return r_fallback_socket;
  }

  auto fallback_error = r_fallback_socket.move_as_error();
  return Status::Error(fallback_error.code(), PSLICE() << "Failed to connect to " << proxy_mode_name(proxy) << " proxy "
                                                       << proxy.server() << ':' << proxy.port() << " using primary "
                                                       << candidates.primary_ip_address << " and fallback "
                                                       << candidates.fallback_ip_address << ": "
                                                       << fallback_error.public_message());
}

Status ConnectionCreator::verify_connection_peer(const Proxy &proxy, const IPAddress &expected_peer_address,
                                                 const IPAddress &actual_peer_address) {
  if (proxy.use_proxy()) {
    return Status::OK();
  }
  if (!expected_peer_address.is_valid()) {
    return Status::Error("Expected peer address is invalid");
  }
  if (!actual_peer_address.is_valid()) {
    return Status::Error("Actual peer address is invalid");
  }

  TRY_RESULT(normalized_expected_peer_address, normalize_peer_address(expected_peer_address));
  TRY_RESULT(normalized_actual_peer_address, normalize_peer_address(actual_peer_address));
  if (normalized_expected_peer_address == normalized_actual_peer_address) {
    return Status::OK();
  }

  net_health::note_route_peer_mismatch();

  return Status::Error(PSLICE() << "Connected peer mismatch: expected " << normalized_expected_peer_address << ", got "
                                << normalized_actual_peer_address);
}

void ConnectionCreator::request_raw_connection_by_ip(IPAddress ip_address, mtproto::TransportType transport_type,
                                                     Promise<unique_ptr<mtproto::RawConnection>> promise) {
  Proxy proxy = active_proxy_id_ == 0 ? Proxy() : proxies_[active_proxy_id_];
  TRY_RESULT_PROMISE(promise, route, resolve_raw_ip_connection_route(proxy, proxy_ip_address_, ip_address));
  TRY_RESULT_PROMISE(promise, effective_transport_type, resolve_raw_ip_transport_type(proxy, transport_type));

  VLOG(connections) << "Resolved raw-IP route " << tag("proxy_mode", proxy_mode_name(proxy))
                    << tag("socket_ip", route.socket_ip_address) << tag("target_ip", ip_address)
                    << tag("tunneled_mtproto_ip", mtproto_tunneled_ip_for_log(route.mtproto_ip_address))
                    << tag("transport", raw_ip_transport_name(effective_transport_type))
                    << tag("transport_dc_id", effective_transport_type.dc_id)
                    << tag("tls_emulation", effective_transport_type.secret.emulate_tls());

  SocketFd socket_fd;
  if (proxy.use_proxy()) {
    TRY_RESULT_PROMISE(promise, proxy_socket, open_proxy_socket(proxy, route.socket_ip_address));
    route.socket_ip_address = proxy_socket.connected_proxy_ip_address;
    if (active_proxy_id_ != 0 && proxy == proxies_[active_proxy_id_]) {
      proxy_ip_address_ = route.socket_ip_address;
    }
    socket_fd = std::move(proxy_socket.socket_fd);
  } else {
    TRY_RESULT_PROMISE(promise, opened_socket_fd, SocketFd::open(route.socket_ip_address));
    socket_fd = std::move(opened_socket_fd);
  }

  stamp_runtime_profile_selection(effective_transport_type);
  auto connection_promise = PromiseCreator::lambda([actor_id = actor_id(this), promise = std::move(promise),
                                                    effective_transport_type, network_generation = network_generation_,
                                                    ip_address](Result<ConnectionData> r_connection_data) mutable {
    if (r_connection_data.is_error()) {
      return promise.set_error(400, r_connection_data.error().public_message());
    }
    auto connection_data = r_connection_data.move_as_ok();
    auto raw_connection = mtproto::RawConnection::create(ip_address, std::move(connection_data.buffered_socket_fd),
                                                         effective_transport_type, nullptr);
    raw_connection->extra().extra = network_generation;
    promise.set_value(std::move(raw_connection));
  });

  auto token = next_token();
  auto ref = prepare_connection(route.socket_ip_address, std::move(socket_fd), proxy, route.mtproto_ip_address,
                                effective_transport_type, "Raw", route.debug_str, nullptr, create_reference(token),
                                false, std::move(connection_promise));
  if (!ref.empty()) {
    children_[token] = {false, std::move(ref)};
  }
}

Result<mtproto::TransportType> ConnectionCreator::get_transport_type(const Proxy &proxy,
                                                                     const DcOptionsSet::ConnectionInfo &info) {
  int32 int_dc_id = info.option->get_dc_id().get_raw_id();
  if (G()->is_test_dc()) {
    int_dc_id += 10000;
  }
  auto raw_dc_id = narrow_cast<int16>(info.option->is_media_only() ? -int_dc_id : int_dc_id);

  if (proxy.use_mtproto_proxy()) {
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, raw_dc_id, proxy.secret()};
  }
  if (proxy.use_http_caching_proxy()) {
    CHECK(info.option != nullptr);
    string proxy_authorization;
    if (!proxy.user().empty() || !proxy.password().empty()) {
      proxy_authorization = "|basic " + base64_encode(PSLICE() << proxy.user() << ':' << proxy.password());
    }
    return mtproto::TransportType{mtproto::TransportType::Http, 0,
                                  mtproto::ProxySecret::from_raw(
                                      PSTRING() << info.option->get_ip_address().get_ip_host() << proxy_authorization)};
  }

  if (info.use_http) {
#if !TD_DARWIN_WATCH_OS && !TD_EMSCRIPTEN
    if (!proxy.use_http_caching_proxy()) {
      net_health::note_lane_protocol_downgrade_flag();
      return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, raw_dc_id, info.option->get_secret()};
    }
#endif
    return mtproto::TransportType{mtproto::TransportType::Http, 0, mtproto::ProxySecret()};
  } else {
    return mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp, raw_dc_id, info.option->get_secret()};
  }
}

Result<SocketFd> ConnectionCreator::find_connection(const Proxy &proxy, const IPAddress &proxy_ip_address, DcId dc_id,
                                                    bool allow_media_only, FindConnectionExtra &extra) {
  extra.debug_str = PSTRING() << "Failed to find valid IP address for " << dc_id;
  bool prefer_ipv6 = should_prefer_ipv6_for_dc_options(proxy, G()->get_option_boolean("prefer_ipv6"), proxy_ip_address);
  bool only_http = proxy.use_http_caching_proxy();
#if TD_DARWIN_WATCH_OS
  only_http = true;
#endif
  TRY_RESULT(info, dc_options_set_.find_connection(
                       dc_id, allow_media_only, proxy.use_proxy() && proxy.use_socks5_proxy(), prefer_ipv6, only_http));
  extra.stat = info.stat;
  TRY_RESULT_ASSIGN(extra.transport_type, get_transport_type(proxy, info));

  extra.debug_str = PSTRING() << " to " << (info.option->is_media_only() ? "MEDIA " : "") << dc_id
                              << (info.use_http ? " over HTTP" : "");

  if (proxy.use_mtproto_proxy()) {
    TRY_RESULT(proxy_socket, open_proxy_socket(proxy, proxy_ip_address));
    extra.ip_address = proxy_socket.connected_proxy_ip_address;
    if (active_proxy_id_ != 0 && proxy == proxies_[active_proxy_id_]) {
      proxy_ip_address_ = extra.ip_address;
    }
    extra.debug_str = PSTRING() << "MTProto " << extra.ip_address << extra.debug_str;
    VLOG(connections) << "Create: " << extra.debug_str;
    return std::move(proxy_socket.socket_fd);
  }

  extra.check_mode |= info.should_check;

  if (proxy.use_proxy()) {
    extra.mtproto_ip_address = info.option->get_ip_address();
    TRY_RESULT(proxy_socket, open_proxy_socket(proxy, proxy_ip_address));
    extra.ip_address = proxy_socket.connected_proxy_ip_address;
    if (active_proxy_id_ != 0 && proxy == proxies_[active_proxy_id_]) {
      proxy_ip_address_ = extra.ip_address;
    }
    extra.debug_str = PSTRING() << (proxy.use_socks5_proxy() ? "Socks5" : (only_http ? "HTTP_ONLY" : "HTTP_TCP")) << ' '
                                << extra.ip_address << " --> " << extra.mtproto_ip_address << extra.debug_str;
    VLOG(connections) << "Create: " << extra.debug_str;
    return std::move(proxy_socket.socket_fd);
  }

  extra.ip_address = info.option->get_ip_address();
  extra.debug_str = PSTRING() << info.option->get_ip_address() << extra.debug_str;
  VLOG(connections) << "Create: " << extra.debug_str;
  return SocketFd::open(extra.ip_address);
}

ActorOwn<> ConnectionCreator::prepare_connection(IPAddress ip_address, SocketFd socket_fd, const Proxy &proxy,
                                                 const IPAddress &mtproto_ip_address,
                                                 const mtproto::TransportType &transport_type, Slice actor_name_prefix,
                                                 Slice debug_str,
                                                 unique_ptr<mtproto::RawConnection::StatsCallback> stats_callback,
                                                 ActorShared<> parent, bool use_connection_token,
                                                 Promise<ConnectionData> promise) {
  if (proxy.use_socks5_proxy() || proxy.use_http_tcp_proxy() || transport_type.secret.emulate_tls()) {
    VLOG(connections) << "Create new transparent proxy connection " << debug_str;
    class Callback final : public TransparentProxy::Callback {
     public:
      Callback(Promise<ConnectionData> promise, IPAddress ip_address,
               unique_ptr<mtproto::RawConnection::StatsCallback> stats_callback, bool use_connection_token,
               bool was_connected)
          : promise_(std::move(promise))
          , ip_address_(std::move(ip_address))
          , stats_callback_(std::move(stats_callback))
          , use_connection_token_(use_connection_token)
          , was_connected_(was_connected) {
      }
      void set_result(Result<BufferedFd<SocketFd>> r_buffered_socket_fd) final {
        if (r_buffered_socket_fd.is_error()) {
          if (use_connection_token_) {
            connection_token_ = mtproto::ConnectionManager::ConnectionToken();
          }
          if (was_connected_ && stats_callback_) {
            stats_callback_->on_error();
          }
          promise_.set_error(r_buffered_socket_fd.move_as_error());
        } else {
          ConnectionData data;
          data.ip_address = ip_address_;
          data.buffered_socket_fd = r_buffered_socket_fd.move_as_ok();
          data.connection_token = std::move(connection_token_);
          data.stats_callback = std::move(stats_callback_);
          promise_.set_value(std::move(data));
        }
      }
      void on_connected() final {
        if (use_connection_token_) {
          connection_token_ = mtproto::ConnectionManager::connection_proxy(
              static_cast<ActorId<mtproto::ConnectionManager>>(G()->state_manager()));
        }
        was_connected_ = true;
      }

     private:
      Promise<ConnectionData> promise_;
      mtproto::ConnectionManager::ConnectionToken connection_token_;
      IPAddress ip_address_;
      unique_ptr<mtproto::RawConnection::StatsCallback> stats_callback_;
      bool use_connection_token_{false};
      bool was_connected_{false};
    };
    VLOG(connections) << "Start "
                      << (proxy.use_socks5_proxy() ? "Socks5" : (proxy.use_http_tcp_proxy() ? "HTTP" : "TLS")) << ": "
                      << debug_str;
    auto callback = make_unique<Callback>(std::move(promise), ip_address, std::move(stats_callback),
                                          use_connection_token, !proxy.use_socks5_proxy());
    if (proxy.use_socks5_proxy()) {
      return ActorOwn<>(create_actor<Socks5>(PSLICE() << actor_name_prefix << "Socks5", std::move(socket_fd),
                                             mtproto_ip_address, proxy.user().str(), proxy.password().str(),
                                             std::move(callback), std::move(parent)));
    } else if (proxy.use_http_tcp_proxy()) {
      return ActorOwn<>(create_actor<HttpProxy>(PSLICE() << actor_name_prefix << "HttpProxy", std::move(socket_fd),
                                                mtproto_ip_address, proxy.user().str(), proxy.password().str(),
                                                std::move(callback), std::move(parent)));
    } else if (transport_type.secret.emulate_tls()) {
      auto route_hints =
          mtproto::stealth::route_hints_from_country_code(G()->get_option_string("stealth_route_country_code"));
      mtproto::stealth::set_runtime_ech_failure_store(G()->td_db()->get_config_pmc_shared());
      return ActorOwn<>(create_actor<mtproto::TlsInit>(
          PSLICE() << actor_name_prefix << "TlsInit", std::move(socket_fd), transport_type.secret.get_domain(),
          transport_type.secret.get_proxy_secret().str(), std::move(callback), std::move(parent),
          G()->get_dns_time_difference(), route_hints, transport_type.selected_profile));
    } else {
      UNREACHABLE();
    }
  } else {
    VLOG(connections) << "Create new direct connection " << debug_str;

    if (!proxy.use_proxy()) {
      IPAddress actual_peer_address;
      auto peer_status = actual_peer_address.init_peer_address(socket_fd);
      if (peer_status.is_error()) {
        promise.set_error(std::move(peer_status));
        return {};
      }
      peer_status = verify_connection_peer(proxy, ip_address, actual_peer_address);
      if (peer_status.is_error()) {
        promise.set_error(std::move(peer_status));
        return {};
      }
    }

    ConnectionData data;
    data.ip_address = ip_address;
    data.buffered_socket_fd = BufferedFd<SocketFd>(std::move(socket_fd));
    data.stats_callback = std::move(stats_callback);
    promise.set_result(std::move(data));
    return {};
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

  Proxy proxy = active_proxy_id_ == 0 ? Proxy() : proxies_[active_proxy_id_];

  if (proxy.use_proxy() && !proxy_ip_address_.is_valid()) {
    VLOG(connections) << "Exit client_loop, because there is no valid IP address for proxy: " << proxy_ip_address_;
    return;
  }

  VLOG(connections) << "In client_loop: " << tag("client", format::as_hex(client.hash));

  auto runtime_params = mtproto::stealth::get_runtime_stealth_params_snapshot();

  // Remove expired ready connections
  td::remove_if(client.ready_connections, [&, now = Time::now_cached()](auto &v) {
    bool drop = ConnectionPoolPolicy::is_pooled_connection_expired(v.second, now, ClientInfo::READY_CONNECTIONS_TIMEOUT,
                                                                   runtime_params.flow_behavior);
    if (drop && v.first && v.first->stats_callback()) {
      v.first->stats_callback()->on_connection_closed(lifecycle_now_ms(now));
    }
    VLOG_IF(connections, drop) << "Drop expired " << tag("connection", v.first.get());
    return drop;
  });

  // Send ready connections into promises
  {
    auto begin = client.queries.begin();
    auto it = begin;
    while (it != client.queries.end() && !client.ready_connections.empty()) {
      if (!it->is_canceled()) {
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
        auto oldest_ready_at = client.ready_connections.front().second;
        for (size_t i = 1; i < client.ready_connections.size(); i++) {
          oldest_ready_at = min(oldest_ready_at, client.ready_connections[i].second);
        }
        auto retention_seconds = ConnectionPoolPolicy::pooled_connection_retention_seconds(
            ClientInfo::READY_CONNECTIONS_TIMEOUT, runtime_params.flow_behavior);
        client_set_timeout_at(client, oldest_ready_at + retention_seconds);
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

    auto now = Time::now();
    bool act_as_if_online = online_flag_ || is_logging_out_;
    // Check flood
    auto &flood_control = act_as_if_online ? client.flood_control_online : client.flood_control;
    auto wakeup_at = max(flood_control.get_wakeup_at(), client.mtproto_error_flood_control.get_wakeup_at());
    wakeup_at = max(client.sanity_flood_control.get_wakeup_at(), wakeup_at);

    bool apply_connection_failure_backoff = should_apply_connection_failure_backoff(act_as_if_online, proxy);
    if (apply_connection_failure_backoff) {
      wakeup_at = max(wakeup_at, static_cast<double>(client.backoff.get_wakeup_at()));
    }
    wakeup_at = max(wakeup_at, client.flow_controller.get_wakeup_at(now, runtime_params.flow_behavior));
    wakeup_at = max(wakeup_at, destination_budget_controller_.get_wakeup_at(now, get_destination_key(client),
                                                                            runtime_params.flow_behavior));
    publish_rotation_gate_snapshot(client, runtime_params.flow_behavior, now);
    if (wakeup_at > now) {
      return client_set_timeout_at(client, wakeup_at);
    }
    client.sanity_flood_control.add_event(now);

    // Create new RawConnection
    // sync part
    FindConnectionExtra extra;
    auto r_socket_fd = find_connection(proxy, proxy_ip_address_, client.dc_id, client.allow_media_only, extra);
    check_mode |= extra.check_mode;
    if (r_socket_fd.is_error()) {
      auto error = r_socket_fd.move_as_error();
      LOG(WARNING) << "Client loop socket open failed" << tag("dc_id", client.dc_id)
                   << tag("connection_context", extra.debug_str) << tag("status_code", error.code())
                   << tag("status_message", sanitize_connection_failure_status_message_for_log(error));
      if (extra.stat) {
        extra.stat->on_error();  // TODO: different kind of error
      }
      client.last_failure_classification = classify_connection_failure(act_as_if_online, proxy, error);
      if (client.last_failure_classification.apply_exponential_backoff) {
        client.backoff.add_event(clamp_backoff_event_time_to_int32(now));
      }
      if (register_bounded_retry_failure(client, client.last_failure_classification, error)) {
        return;
      }
      auto wakeup_at_after_failure = Time::now() + 0.1;
      if (client.last_failure_classification.apply_exponential_backoff) {
        wakeup_at_after_failure = max(wakeup_at_after_failure, static_cast<double>(client.backoff.get_wakeup_at()));
      }
      return client_set_timeout_at(client, wakeup_at_after_failure);
    }

    // Events with failed socket creation are ignored
    flood_control.add_event(now);
    client.flow_controller.on_connect_started(now, runtime_params.flow_behavior);
    destination_budget_controller_.on_connect_started(now, get_destination_key(client), runtime_params.flow_behavior);
    publish_rotation_gate_snapshot(client, runtime_params.flow_behavior, now);

    auto socket_fd = r_socket_fd.move_as_ok();
#if !TD_DARWIN_WATCH_OS
    IPAddress debug_ip;
    auto debug_ip_status = debug_ip.init_socket_address(socket_fd);
    if (debug_ip_status.is_ok()) {
      extra.debug_str = PSTRING() << extra.debug_str << " from " << debug_ip;
    } else {
      LOG(ERROR) << "Client loop local endpoint introspection failed" << tag("dc_id", client.dc_id)
                 << tag("connection_context", extra.debug_str) << tag("status_code", debug_ip_status.code())
                 << tag("status_message", sanitize_connection_failure_status_message_for_log(debug_ip_status));
    }
#endif

    client.pending_connections++;
    if (check_mode) {
      if (extra.stat) {
        extra.stat->on_check();
      }
      client.checking_connections++;
    }

    stamp_runtime_profile_selection(extra.transport_type);
    auto promise = PromiseCreator::lambda(
        [actor_id = actor_id(this), check_mode, transport_type = extra.transport_type, hash = client.hash,
         debug_str = extra.debug_str,
         network_generation = network_generation_](Result<ConnectionData> r_connection_data) mutable {
          send_closure(actor_id, &ConnectionCreator::client_create_raw_connection, std::move(r_connection_data),
                       check_mode, std::move(transport_type), hash, std::move(debug_str), network_generation);
        });

    auto stats_callback =
        td::make_unique<detail::StatsCallback>(client.is_media ? media_net_stats_callback_ : common_net_stats_callback_,
                                               actor_id(this), client.hash, extra.stat, connection_lifecycle_report_);
    auto token = next_token();
    auto ref = prepare_connection(extra.ip_address, std::move(socket_fd), proxy, extra.mtproto_ip_address,
                                  extra.transport_type, Slice(), extra.debug_str, std::move(stats_callback),
                                  create_reference(token), true, std::move(promise));
    if (!ref.empty()) {
      children_[token] = {true, std::move(ref)};
    }
  }
}

ConnectionDestinationBudgetController::DestinationKey ConnectionCreator::get_destination_key(
    const ClientInfo &client) const {
  ConnectionDestinationBudgetController::DestinationKey destination;
  destination.dc_id = client.dc_id.get_raw_id();
  destination.proxy_id = active_proxy_id_;
  destination.allow_media_only = client.allow_media_only;
  destination.is_media = client.is_media;
  return destination;
}

void ConnectionCreator::publish_rotation_gate_snapshot(ClientInfo &client,
                                                       const mtproto::stealth::RuntimeFlowBehaviorPolicy &policy,
                                                       double now) {
  if (client.rotation_gate_snapshot == nullptr) {
    return;
  }

  ConnectionRotationGateSnapshot snapshot;
  snapshot.anti_churn_allows_rotation = client.flow_controller.allows_rotation_at(now, policy);
  snapshot.destination_budget_allows_overlap =
      destination_budget_controller_.allows_overlap_at(now, get_destination_key(client), policy);
  client.rotation_gate_snapshot->set(snapshot);
}

void ConnectionCreator::client_create_raw_connection(Result<ConnectionData> r_connection_data, bool check_mode,
                                                     mtproto::TransportType transport_type, uint32 hash,
                                                     string debug_str, uint32 network_generation) {
  unique_ptr<mtproto::AuthData> auth_data;
  uint64 auth_data_generation{0};
  uint64 session_id{0};
  if (check_mode) {
    auto it = clients_.find(hash);
    CHECK(it != clients_.end());
    const auto &auth_data_ptr = it->second.auth_data;
    if (auth_data_ptr && auth_data_ptr->is_keyed_session() && auth_data_ptr->has_auth_key(Time::now_cached())) {
      auth_data = make_unique<mtproto::AuthData>(*auth_data_ptr);
      auth_data_generation = it->second.auth_data_generation;
      session_id = it->second.extract_session_id();
      auth_data->set_session_id(session_id);
    }
  }
  auto promise = PromiseCreator::lambda([actor_id = actor_id(this), hash, check_mode, auth_data_generation, session_id,
                                         debug_str](Result<unique_ptr<mtproto::RawConnection>> result) mutable {
    if (result.is_ok()) {
      VLOG(connections) << "Ready connection (" << (check_mode ? "" : "un") << "checked) " << result.ok().get() << ' '
                        << tag("rtt", format::as_time(result.ok()->extra().rtt)) << ' ' << debug_str;
    } else {
      VLOG(connections) << "Failed connection (" << (check_mode ? "" : "un") << "checked) " << result.error() << ' '
                        << debug_str;
    }
    send_closure(actor_id, &ConnectionCreator::client_add_connection, hash, std::move(result), check_mode,
                 auth_data_generation, session_id);
  });

  if (r_connection_data.is_error()) {
    return promise.set_error(r_connection_data.move_as_error());
  }

  auto connection_data = r_connection_data.move_as_ok();
  auto raw_connection =
      mtproto::RawConnection::create(connection_data.ip_address, std::move(connection_data.buffered_socket_fd),
                                     std::move(transport_type), std::move(connection_data.stats_callback));
  raw_connection->set_connection_token(std::move(connection_data.connection_token));
  if (raw_connection->stats_callback()) {
    raw_connection->stats_callback()->on_connection_open(
        reinterpret_cast<uint64>(raw_connection.get()),
        make_lifecycle_destination(connection_data.ip_address, debug_str), lifecycle_now_ms());
  }

  raw_connection->extra().extra = network_generation;
  raw_connection->extra().debug_str = debug_str;

  if (check_mode) {
    VLOG(connections) << "Start check: " << debug_str << " " << (auth_data ? "with" : "without") << " auth data";
    auto token = next_token();
    children_[token] = {true, create_ping_actor(debug_str, std::move(raw_connection), std::move(auth_data),
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

bool ConnectionCreator::register_bounded_retry_failure(ClientInfo &client,
                                                       const ConnectionFailureClassification &classification,
                                                       const Status &status) {
  if (!classification.bounded_retry) {
    client.bounded_retry_failures = 0;
    return false;
  }

  client.bounded_retry_failures++;
  if (client.bounded_retry_failures < ClientInfo::MAX_BOUNDED_RETRY_FAILURES) {
    return false;
  }

  LOG(WARNING) << "Bounded retry limit reached" << tag("client", format::as_hex(client.hash))
               << tag("attempts", client.bounded_retry_failures) << tag("status_code", status.code())
               << tag("status_message", sanitize_connection_failure_status_message_for_log(status));

  auto capped_error =
      Status::Error(status.code(),
                    PSLICE() << "Connection retry limit reached after " << client.bounded_retry_failures
                             << " failures; last_error=" << sanitize_connection_failure_status_message_for_log(status));
  for (auto &query : client.queries) {
    if (!query.is_canceled()) {
      query.set_error(capped_error.clone());
    }
  }
  client.queries.clear();
  client.bounded_retry_failures = 0;
  return true;
}

void ConnectionCreator::client_add_connection(uint32 hash, Result<unique_ptr<mtproto::RawConnection>> r_raw_connection,
                                              bool check_flag, uint64 auth_data_generation, uint64 session_id) {
  auto &client = clients_[hash];
  client.add_session_id(session_id);
  CHECK(client.pending_connections > 0);
  client.pending_connections--;
  if (check_flag) {
    CHECK(client.checking_connections > 0);
    client.checking_connections--;
  }
  if (r_raw_connection.is_ok()) {
    VLOG(connections) << "Add ready connection " << r_raw_connection.ok().get() << " for "
                      << tag("client", format::as_hex(hash));
    client.backoff.clear();
    client.bounded_retry_failures = 0;
    client.last_failure_classification = {};
    client.ready_connections.emplace_back(r_raw_connection.move_as_ok(), Time::now_cached());
  } else {
    auto proxy = active_proxy_id_ == 0 ? Proxy() : proxies_[active_proxy_id_];
    const auto &failure_status = r_raw_connection.error();
    client.last_failure_classification =
        classify_connection_failure(online_flag_ || is_logging_out_, proxy, failure_status);
    if (client.last_failure_classification.apply_exponential_backoff) {
      client.backoff.add_event(clamp_backoff_event_time_to_int32(Time::now()));
    }
    if (register_bounded_retry_failure(client, client.last_failure_classification, failure_status)) {
      return;
    }
    auto action_hint = connection_failure_action_hint(client.last_failure_classification.stage,
                                                      client.last_failure_classification.reason);
    VLOG(connections) << "Classified connection failure " << tag("client", format::as_hex(hash))
                      << tag("deterministic", client.last_failure_classification.deterministic)
                      << tag("stage", static_cast<int32>(client.last_failure_classification.stage))
                      << tag("stage_name", proxy_failure_stage_name(client.last_failure_classification.stage))
                      << tag("reason", static_cast<int32>(client.last_failure_classification.reason))
                      << tag("reason_name", proxy_failure_reason_name(client.last_failure_classification.reason))
                      << tag("status_code", failure_status.code())
                      << tag("status_message", sanitize_connection_failure_status_message_for_log(failure_status))
                      << tag("action_hint", action_hint) << ' '
                      << summarize_connection_failure_for_log(client.last_failure_classification, failure_status);
    if (mtproto::is_mtproto_auth_key_not_found_status(r_raw_connection.error()) && client.auth_data &&
        client.auth_data_generation == auth_data_generation) {
      VLOG(connections) << "Drop auth data from " << tag("client", format::as_hex(hash));
      client.auth_data = nullptr;
      client.auth_data_generation++;
    }
  }
  client_loop(client);
}

void ConnectionCreator::client_wakeup(uint32 hash) {
  VLOG(connections) << tag("hash", format::as_hex(hash)) << " wakeup";
  G()->save_server_time();
  client_loop(clients_[hash]);
}

void ConnectionCreator::on_dc_options(DcOptions new_dc_options) {
  FlatHashSet<string> baseline_signatures;
  auto baseline_options = filter_reviewed_route_options(get_default_dc_options(G()->is_test_dc()), G()->is_test_dc());
  baseline_signatures.reserve(baseline_options.dc_options.size());
  for (const auto &baseline_option : baseline_options.dc_options) {
    baseline_signatures.insert(get_dc_option_signature(baseline_option));
  }

  for (const auto &incoming_option : new_dc_options.dc_options) {
    if (baseline_signatures.count(get_dc_option_signature(incoming_option)) == 0) {
      net_health::note_route_push_nonbaseline_address();
    }
    // §19: record per-DC address update arrival for forced-reauth sequence detection
    net_health::note_route_address_update(incoming_option.get_dc_id().get_raw_id(), Time::now());
  }

  new_dc_options = filter_reviewed_route_options(std::move(new_dc_options), G()->is_test_dc());

  auto seed = G()->get_option_integer("my_id");
  std::stable_sort(new_dc_options.dc_options.begin(), new_dc_options.dc_options.end(),
                   [seed](const DcOption &lhs, const DcOption &rhs) {
                     if (lhs.get_dc_id() != rhs.get_dc_id()) {
                       return lhs.get_dc_id() < rhs.get_dc_id();
                     }
                     if (lhs.is_ipv6() != rhs.is_ipv6()) {
                       return rhs.is_ipv6();
                     }
                     if (lhs.is_media_only() != rhs.is_media_only()) {
                       return rhs.is_media_only();
                     }
                     if (lhs.is_obfuscated_tcp_only() != rhs.is_obfuscated_tcp_only()) {
                       return lhs.is_obfuscated_tcp_only();
                     }
                     if (lhs.is_static() != rhs.is_static()) {
                       return rhs.is_static();
                     }
                     if (lhs.is_ipv6()) {
                       return false;
                     }
                     auto lhs_ip_address_hash = Hash<int64>()(lhs.get_ip_address().get_ipv4() + seed);
                     auto rhs_ip_address_hash = Hash<int64>()(rhs.get_ip_address().get_ipv4() + seed);
                     return lhs_ip_address_hash < rhs_ip_address_hash;
                   });

  VLOG(connections) << "SAVE " << new_dc_options;
  G()->td_db()->get_binlog_pmc()->set("dc_options", serialize(new_dc_options));
  dc_options_set_.reset();
  add_dc_options(std::move(new_dc_options));
}

void ConnectionCreator::add_dc_options(DcOptions &&new_dc_options) {
  dc_options_set_.add_dc_options(
      filter_reviewed_route_options(get_default_dc_options(G()->is_test_dc()), G()->is_test_dc()));
#if !TD_EMSCRIPTEN  // FIXME
  dc_options_set_.add_dc_options(filter_reviewed_route_options(std::move(new_dc_options), G()->is_test_dc()));
#endif
}

bool ConnectionCreator::is_reviewed_route_address(const IPAddress &ip_address, [[maybe_unused]] bool is_test) {
  if (!ip_address.is_valid()) {
    return false;
  }
  if (ip_address.is_ipv4()) {
    auto ip = ip_address.get_ipv4();
    if (is_forbidden_ipv4(ip)) {
      return false;
    }
    return is_reviewed_ipv4(ip);
  }
  if (ip_address.is_ipv6()) {
    auto octets = to_ipv6_octets(ip_address);
    if (is_forbidden_ipv6(octets)) {
      return false;
    }
    return is_reviewed_ipv6(octets);
  }
  return false;
}

DcOptions ConnectionCreator::filter_reviewed_route_options(DcOptions options, bool is_test) {
  DcOptions filtered;
  filtered.dc_options.reserve(options.dc_options.size());

  for (auto &option : options.dc_options) {
    if (is_forbidden_route_address(option.get_ip_address())) {
      net_health::note_route_push_nonbaseline_address();
      continue;
    }

    auto dc_id = option.get_dc_id();
    if (dc_id.is_internal() && !NetQueryDispatcher::is_known_main_dc_id(dc_id.get_raw_id(), is_test)) {
      net_health::note_route_catalog_unknown_id();
      continue;
    }
    if (dc_id.is_internal() && !is_reviewed_route_address(option.get_ip_address(), is_test)) {
      net_health::note_route_push_nonbaseline_address();
      continue;
    }
    filtered.dc_options.push_back(std::move(option));
  }

  return filtered;
}

void ConnectionCreator::on_dc_update(DcId dc_id, string ip_port, Promise<> promise) {
  if (!dc_id.is_exact()) {
    return promise.set_error("Invalid dc_id");
  }

  IPAddress ip_address;
  TRY_STATUS_PROMISE(promise, ip_address.init_host_port(ip_port));
  DcOptions options;
  options.dc_options.emplace_back(dc_id, ip_address);
  send_closure(G()->config_manager(), &ConfigManager::on_dc_options_update, std::move(options));
  promise.set_value(Unit());
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
  class StateCallback final : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<ConnectionCreator> connection_creator)
        : connection_creator_(std::move(connection_creator)) {
    }
    bool on_network(NetType network_type, uint32 generation) final {
      send_closure(connection_creator_, &ConnectionCreator::on_network, network_type != NetType::None, generation);
      return connection_creator_.is_alive();
    }
    bool on_online(bool online_flag) final {
      send_closure(connection_creator_, &ConnectionCreator::on_online, online_flag);
      return connection_creator_.is_alive();
    }
    bool on_logging_out(bool is_logging_out) final {
      send_closure(connection_creator_, &ConnectionCreator::on_logging_out, is_logging_out);
      return connection_creator_.is_alive();
    }

   private:
    ActorId<ConnectionCreator> connection_creator_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));
  mtproto::stealth::set_runtime_ech_failure_store(G()->td_db()->get_config_pmc_shared());

  auto serialized_dc_options = G()->td_db()->get_binlog_pmc()->get("dc_options");
  DcOptions dc_options;
  auto status = unserialize(dc_options, serialized_dc_options);
  if (status.is_error()) {
    on_dc_options(DcOptions());
  } else {
    add_dc_options(std::move(dc_options));
  }

  if (G()->td_db()->get_binlog_pmc()->get("proxy_max_id") != "2" ||
      !G()->td_db()->get_binlog_pmc()->get(get_proxy_database_key(1)).empty()) {
    // don't need to init proxies if they have never been added
    init_proxies();
  } else {
    max_proxy_id_ = 2;
  }

  ref_cnt_guard_ = create_reference(-1);

  is_inited_ = true;
  loop();
}

void ConnectionCreator::init_proxies() {
  auto proxy_info = G()->td_db()->get_binlog_pmc()->prefix_get("proxy");
  auto it = proxy_info.find("_max_id");
  if (it != proxy_info.end()) {
    max_proxy_id_ = to_integer<int32>(it->second);
    proxy_info.erase(it);
  }
  it = proxy_info.find("_active_id");
  if (it != proxy_info.end()) {
    set_active_proxy_id(to_integer<int32>(it->second), true);
    proxy_info.erase(it);
  }

  for (auto &info : proxy_info) {
    if (begins_with(info.first, "_used")) {
      auto proxy_id = to_integer_safe<int32>(Slice(info.first).substr(5)).move_as_ok();
      auto last_used = to_integer_safe<int32>(info.second).move_as_ok();
      CHECK(proxy_id > 0);
      proxy_last_used_date_[proxy_id] = last_used;
      proxy_last_used_saved_date_[proxy_id] = last_used;
    } else {
      LOG_CHECK(!ends_with(info.first, "_max_id")) << info.first;
      auto proxy_id = info.first.empty() ? static_cast<int32>(1) : to_integer_safe<int32>(info.first).move_as_ok();
      CHECK(proxy_id > 0);
      CHECK(proxies_.count(proxy_id) == 0);
      log_event_parse(proxies_[proxy_id], info.second).ensure();
      if (proxies_[proxy_id].type() == Proxy::Type::None) {
        LOG_IF(ERROR, proxy_id != 1) << "Have empty proxy " << proxy_id;
        G()->td_db()->get_binlog_pmc()->erase(get_proxy_database_key(proxy_id));
        G()->td_db()->get_binlog_pmc()->erase(get_proxy_used_database_key(proxy_id));
        proxies_.erase(proxy_id);
        if (active_proxy_id_ == proxy_id) {
          set_active_proxy_id(0);
        }
      }
    }
  }

  if (max_proxy_id_ == 0) {
    // legacy one-proxy version
    max_proxy_id_ = 2;
    if (!proxies_.empty()) {
      CHECK(proxies_.begin()->first == 1);
      set_active_proxy_id(1);
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
  enum class HostType : int32 { IPv4, IPv6, Url };
  auto add_ip_ports = [&res](int32 dc_id, vector<string> ip_address_strings, const vector<int> &ports,
                             HostType type = HostType::IPv4) {
    IPAddress ip_address;
    Random::shuffle(ip_address_strings);
    for (auto port : ports) {
      for (auto &ip_address_string : ip_address_strings) {
        switch (type) {
          case HostType::IPv4:
            ip_address.init_ipv4_port(ip_address_string, port).ensure();
            break;
          case HostType::IPv6:
            ip_address.init_ipv6_port(ip_address_string, port).ensure();
            break;
          case HostType::Url:
            ip_address.init_host_port(ip_address_string, port).ensure();
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
    add_ip_ports(2, {"149.154.167.51", "95.161.76.100"}, ports);
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
  if (G()->close_flag()) {
    return;
  }
  if (!is_inited_) {
    return;
  }
  if (!network_flag_) {
    return;
  }

  Timestamp timeout;
  if (active_proxy_id_ != 0) {
    if (resolve_proxy_timestamp_.is_in_past()) {
      if (resolve_proxy_query_token_ == 0) {
        resolve_proxy_query_token_ = next_token();
        const Proxy &proxy = proxies_[active_proxy_id_];
        bool prefer_ipv6 = G()->get_option_boolean("prefer_ipv6");
        VLOG(connections) << "Resolve IP address " << resolve_proxy_query_token_ << " of " << proxy.server();
        send_closure(
            get_dns_resolver(), &GetHostByNameActor::run, proxy.server().str(), proxy.port(), prefer_ipv6,
            PromiseCreator::lambda([actor_id = create_reference(resolve_proxy_query_token_)](Result<IPAddress> result) {
              send_closure(actor_id, &ConnectionCreator::on_proxy_resolved, std::move(result), false);
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
      request.promise.set_error(400, request.result.error().public_message());
    } else {
      request.promise.set_value(request.result.move_as_ok());
    }
    ping_main_dc_requests_.erase(token);
  }
}

}  // namespace td
