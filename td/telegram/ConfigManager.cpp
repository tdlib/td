//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ConfigManager.h"

#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/net/PublicRsaKeyShared.h"
#include "td/telegram/net/Session.h"

#if !TD_EMSCRIPTEN  //FIXME
#include "td/net/HttpQuery.h"
#include "td/net/SslStream.h"
#include "td/net/Wget.h"
#endif

#include "td/actor/actor.h"

#include "td/telegram/telegram_api.h"

#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace td {

static int VERBOSITY_NAME(config_recoverer) = VERBOSITY_NAME(INFO);

Result<SimpleConfig> decode_config(Slice input) {
  static auto rsa = td::RSA::from_pem(
                        "-----BEGIN RSA PUBLIC KEY-----\n"
                        "MIIBCgKCAQEAyr+18Rex2ohtVy8sroGP\n"
                        "BwXD3DOoKCSpjDqYoXgCqB7ioln4eDCFfOBUlfXUEvM/fnKCpF46VkAftlb4VuPD\n"
                        "eQSS/ZxZYEGqHaywlroVnXHIjgqoxiAd192xRGreuXIaUKmkwlM9JID9WS2jUsTp\n"
                        "zQ91L8MEPLJ/4zrBwZua8W5fECwCCh2c9G5IzzBm+otMS/YKwmR1olzRCyEkyAEj\n"
                        "XWqBI9Ftv5eG8m0VkBzOG655WIYdyV0HfDK/NWcvGqa0w/nriMD6mDjKOryamw0O\n"
                        "P9QuYgMN0C9xMW9y8SmP4h92OAWodTYgY1hZCxdv6cs5UnW9+PWvS+WIbkh+GaWY\n"
                        "xwIDAQAB\n"
                        "-----END RSA PUBLIC KEY-----\n")
                        .move_as_ok();

  if (input.size() < 344 || input.size() > 1024) {
    return Status::Error(PSLICE() << "Invalid " << tag("length", input.size()));
  }

  auto data_base64 = base64_filter(input);
  if (data_base64.size() != 344) {
    return Status::Error(PSLICE() << "Invalid " << tag("length", data_base64.size()) << " after base64_filter");
  }
  TRY_RESULT(data_rsa, base64_decode(data_base64));
  if (data_rsa.size() != 256) {
    return Status::Error(PSLICE() << "Invalid " << tag("length", data_rsa.size()) << " after base64_decode");
  }

  MutableSlice data_rsa_slice(data_rsa);
  rsa.decrypt(data_rsa_slice, data_rsa_slice);

  MutableSlice data_cbc = data_rsa_slice.substr(32);
  UInt256 key;
  UInt128 iv;
  MutableSlice(key.raw, sizeof(key.raw)).copy_from(data_rsa_slice.substr(0, 32));
  MutableSlice(iv.raw, sizeof(iv.raw)).copy_from(data_rsa_slice.substr(16, 16));
  aes_cbc_decrypt(key, &iv, data_cbc, data_cbc);

  CHECK(data_cbc.size() == 224);
  string hash(32, ' ');
  sha256(data_cbc.substr(0, 208), MutableSlice(hash));
  if (data_cbc.substr(208) != Slice(hash).substr(0, 16)) {
    return Status::Error("sha256 mismatch");
  }

  TlParser len_parser{data_cbc};
  int len = len_parser.fetch_int();
  if (len < 0 || len > 204) {
    return Status::Error(PSLICE() << "Invalid " << tag("data length", len) << " after aes_cbc_decrypt");
  }
  int constructor_id = len_parser.fetch_int();
  if (constructor_id != telegram_api::help_configSimple::ID) {
    return Status::Error(PSLICE() << "Wrong " << tag("constructor", format::as_hex(constructor_id)));
  }
  BufferSlice raw_config(data_cbc.substr(8, len));
  TlBufferParser parser{&raw_config};
  auto config = telegram_api::help_configSimple::fetch(parser);
  TRY_STATUS(parser.get_status());
  return std::move(config);
}

static ActorOwn<> get_simple_config_impl(Promise<SimpleConfig> promise, int32 scheduler_id, string url, string host,
                                         bool prefer_ipv6) {
  VLOG(config_recoverer) << "Request simple config from " << url;
#if TD_EMSCRIPTEN  // FIXME
  return ActorOwn<>();
#else
  const int timeout = 10;
  const int ttl = 3;
  return ActorOwn<>(create_actor_on_scheduler<Wget>(
      "Wget", scheduler_id,
      PromiseCreator::lambda([promise = std::move(promise)](Result<HttpQueryPtr> r_query) mutable {
        promise.set_result([&]() -> Result<SimpleConfig> {
          TRY_RESULT(http_query, std::move(r_query));
          return decode_config(http_query->content_);
        }());
      }),
      std::move(url), std::vector<std::pair<string, string>>({{"Host", std::move(host)}}), timeout, ttl, prefer_ipv6,
      SslStream::VerifyPeer::Off));
#endif
}

ActorOwn<> get_simple_config_azure(Promise<SimpleConfig> promise, const ConfigShared *shared_config, bool is_test,
                                   int32 scheduler_id) {
  string url = PSTRING() << "https://software-download.microsoft.com/" << (is_test ? "test" : "prod")
                         << "v2/config.txt";
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  return get_simple_config_impl(std::move(promise), scheduler_id, std::move(url), "tcdnb.azureedge.net", prefer_ipv6);
}

ActorOwn<> get_simple_config_google_dns(Promise<SimpleConfig> promise, const ConfigShared *shared_config, bool is_test,
                                        int32 scheduler_id) {
  VLOG(config_recoverer) << "Request simple config from Google DNS";
#if TD_EMSCRIPTEN  // FIXME
  return ActorOwn<>();
#else
  string name = shared_config == nullptr ? string() : shared_config->get_option_string("dc_txt_domain_name");
  const int timeout = 10;
  const int ttl = 3;
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  if (name.empty()) {
    name = is_test ? "tapv2.stel.com" : "apv2.stel.com";
  }
  return ActorOwn<>(create_actor_on_scheduler<Wget>(
      "Wget", scheduler_id,
      PromiseCreator::lambda([promise = std::move(promise)](Result<HttpQueryPtr> r_query) mutable {
        promise.set_result([&]() -> Result<SimpleConfig> {
          TRY_RESULT(http_query, std::move(r_query));
          TRY_RESULT(json, json_decode(http_query->content_));
          if (json.type() != JsonValue::Type::Object) {
            return Status::Error("json error");
          }
          auto &answer_object = json.get_object();
          TRY_RESULT(answer, get_json_object_field(answer_object, "Answer", JsonValue::Type::Array, false));
          auto &answer_array = answer.get_array();
          vector<string> parts;
          for (auto &v : answer_array) {
            if (v.type() != JsonValue::Type::Object) {
              return Status::Error("json error");
            }
            auto &data_object = v.get_object();
            TRY_RESULT(part, get_json_object_string_field(data_object, "data", false));
            parts.push_back(std::move(part));
          }
          if (parts.size() != 2) {
            return Status::Error("Expected data in two parts");
          }
          string data;
          if (parts[0].size() < parts[1].size()) {
            data = parts[1] + parts[0];
          } else {
            data = parts[0] + parts[1];
          }
          return decode_config(data);
        }());
      }),
      PSTRING() << "https://www.google.com/resolve?name=" << url_encode(name) << "&type=16",
      std::vector<std::pair<string, string>>({{"Host", "dns.google.com"}}), timeout, ttl, prefer_ipv6,
      SslStream::VerifyPeer::Off));
#endif
}

ActorOwn<> get_full_config(DcId dc_id, IPAddress ip_address, Promise<FullConfig> promise) {
  class SessionCallback : public Session::Callback {
   public:
    SessionCallback(ActorShared<> parent, IPAddress address)
        : parent_(std::move(parent)), address_(std::move(address)) {
    }
    void on_failed() final {
    }
    void on_closed() final {
    }
    void request_raw_connection(Promise<std::unique_ptr<mtproto::RawConnection>> promise) final {
      request_raw_connection_cnt_++;
      VLOG(config_recoverer) << "Request full config from " << address_ << ", try = " << request_raw_connection_cnt_;
      if (request_raw_connection_cnt_ <= 2) {
        send_closure(G()->connection_creator(), &ConnectionCreator::request_raw_connection_by_ip, address_,
                     std::move(promise));
      } else {
        // Delay all queries except first forever
        delay_forever_.push_back(std::move(promise));
      }
    }
    void on_tmp_auth_key_updated(mtproto::AuthKey auth_key) final {
      // nop
    }

   private:
    ActorShared<> parent_;
    IPAddress address_;
    size_t request_raw_connection_cnt_{0};
    std::vector<Promise<std::unique_ptr<mtproto::RawConnection>>> delay_forever_;
  };

  class SimpleAuthData : public AuthDataShared {
   public:
    explicit SimpleAuthData(DcId dc_id) : dc_id_(dc_id) {
    }
    DcId dc_id() const override {
      return dc_id_;
    }
    const std::shared_ptr<PublicRsaKeyShared> &public_rsa_key() override {
      return public_rsa_key_;
    }
    mtproto::AuthKey get_auth_key() override {
      string dc_key = G()->td_db()->get_binlog_pmc()->get(auth_key_key());

      mtproto::AuthKey res;
      if (!dc_key.empty()) {
        unserialize(res, dc_key).ensure();
      }
      return res;
    }
    std::pair<AuthState, bool> get_auth_state() override {
      auto auth_key = get_auth_key();
      AuthState state = AuthDataShared::get_auth_state(auth_key);
      return std::make_pair(state, auth_key.was_auth_flag());
    }
    void set_auth_key(const mtproto::AuthKey &auth_key) override {
      G()->td_db()->get_binlog_pmc()->set(auth_key_key(), serialize(auth_key));

      //notify();
    }
    void update_server_time_difference(double diff) override {
      G()->update_server_time_difference(diff);
    }
    double get_server_time_difference() override {
      return G()->get_server_time_difference();
    }
    void add_auth_key_listener(unique_ptr<Listener> listener) override {
      if (listener->notify()) {
        auth_key_listeners_.push_back(std::move(listener));
      }
    }

    void set_future_salts(const std::vector<mtproto::ServerSalt> &future_salts) override {
      G()->td_db()->get_binlog_pmc()->set(future_salts_key(), serialize(future_salts));
    }

    std::vector<mtproto::ServerSalt> get_future_salts() override {
      string future_salts = G()->td_db()->get_binlog_pmc()->get(future_salts_key());
      std::vector<mtproto::ServerSalt> res;
      if (!future_salts.empty()) {
        unserialize(res, future_salts).ensure();
      }
      return res;
    }

   private:
    DcId dc_id_;
    std::shared_ptr<PublicRsaKeyShared> public_rsa_key_ = std::make_shared<PublicRsaKeyShared>(DcId::empty());

    std::vector<std::unique_ptr<Listener>> auth_key_listeners_;
    void notify() {
      auto it = std::remove_if(auth_key_listeners_.begin(), auth_key_listeners_.end(),
                               [&](auto &listener) { return !listener->notify(); });
      auth_key_listeners_.erase(it, auth_key_listeners_.end());
    }

    string auth_key_key() const {
      return PSTRING() << "config_recovery_auth" << dc_id().get_raw_id();
    }
    string future_salts_key() const {
      return PSTRING() << "config_recovery_salt" << dc_id().get_raw_id();
    }
  };

  class GetConfigActor : public NetQueryCallback {
   public:
    GetConfigActor(DcId dc_id, IPAddress ip_address, Promise<FullConfig> promise)
        : dc_id_(dc_id), ip_address_(std::move(ip_address)), promise_(std::move(promise)) {
    }

   private:
    void start_up() override {
      auto session_callback = std::make_unique<SessionCallback>(actor_shared(this, 1), std::move(ip_address_));

      auto auth_data = std::make_shared<SimpleAuthData>(dc_id_);
      int32 int_dc_id = dc_id_.get_raw_id();
      if (G()->is_test_dc()) {
        int_dc_id += 10000;
      }
      session_ = create_actor<Session>("ConfigSession", std::move(session_callback), std::move(auth_data), int_dc_id,
                                       false /*is_main*/, true /*use_pfs*/, false /*is_cdn*/, mtproto::AuthKey(),
                                       std::vector<mtproto::ServerSalt>());
      auto query = G()->net_query_creator().create(create_storer(telegram_api::help_getConfig()), DcId::empty(),
                                                   NetQuery::Type::Common, NetQuery::AuthFlag::Off,
                                                   NetQuery::GzipFlag::On, 60 * 60 * 24);
      query->set_callback(actor_shared(this));
      query->dispatch_ttl = 0;
      send_closure(session_, &Session::send, std::move(query));
      set_timeout_in(10);
    }
    void on_result(NetQueryPtr query) override {
      promise_.set_result(fetch_result<telegram_api::help_getConfig>(std::move(query)));
      stop();
    }
    void hangup_shared() override {
      if (get_link_token() == 1) {
        if (promise_) {
          promise_.set_error(Status::Error("Failed"));
        }
        stop();
      }
    }
    void hangup() override {
      session_.reset();
    }
    void timeout_expired() override {
      promise_.set_error(Status::Error("Timeout expired"));
      session_.reset();
    }

    DcId dc_id_;
    IPAddress ip_address_;
    ActorOwn<Session> session_;
    Promise<FullConfig> promise_;
  };

  return ActorOwn<>(create_actor<GetConfigActor>("GetConfigActor", dc_id, std::move(ip_address), std::move(promise)));
}

class ConfigRecoverer : public Actor {
 public:
  explicit ConfigRecoverer(ActorShared<> parent) : parent_(std::move(parent)) {
  }

  void on_dc_options_update(DcOptions dc_options) {
    dc_options_update_ = dc_options;
    update_dc_options();
    loop();
  }

 private:
  void on_network(bool has_network, uint32 network_generation) {
    has_network_ = has_network;
    if (network_generation_ != network_generation) {
      if (has_network_) {
        has_network_since_ = Time::now_cached();
      }
    }
    loop();
  }
  void on_online(bool is_online) {
    if (is_online_ == is_online) {
      return;
    }

    is_online_ = is_online;
    if (is_online) {
      if (simple_config_.dc_options.empty()) {
        simple_config_expire_at_ = 0;
      }
      if (full_config_ == nullptr) {
        full_config_expire_at_ = 0;
      }
    }
    loop();
  }
  void on_connecting(bool is_connecting) {
    VLOG(config_recoverer) << "ON CONNECTING " << is_connecting;
    if (is_connecting && !is_connecting_) {
      connecting_since_ = Time::now_cached();
    }
    is_connecting_ = is_connecting;
    loop();
  }

  static bool check_phone_number_rules(Slice phone_number, Slice rules) {
    if (rules.empty() || phone_number.empty()) {
      return true;
    }

    bool found = false;
    for (auto prefix : full_split(rules, ',')) {
      if (prefix.empty()) {
        found = true;
      } else if (prefix[0] == '+' && begins_with(phone_number, prefix.substr(1))) {
        found = true;
      } else if (prefix[0] == '-' && begins_with(phone_number, prefix.substr(1))) {
        return false;
      } else {
        LOG(ERROR) << "Invalid prefix rule " << prefix;
      }
    }
    return found;
  }

  void on_simple_config(Result<SimpleConfig> r_simple_config, bool dummy) {
    simple_config_query_.reset();
    dc_options_i_ = 0;
    if (r_simple_config.is_ok()) {
      auto config = r_simple_config.move_as_ok();
      VLOG(config_recoverer) << "Receive raw SimpleConfig" << to_string(config);
      if (config->expires_ >= G()->unix_time()) {
        string phone_number = G()->shared_config().get_option_string("my_phone_number");
        simple_config_.dc_options.clear();

        for (auto &rule : config->rules_) {
          if (check_phone_number_rules(phone_number, rule->phone_prefix_rules_) && DcId::is_valid(rule->dc_id_)) {
            DcId dc_id = DcId::internal(rule->dc_id_);
            for (auto &ip_port : rule->ips_) {
              DcOption option(dc_id, *ip_port);
              if (option.is_valid()) {
                simple_config_.dc_options.push_back(std::move(option));
              }
            }
          }
        }
        VLOG(config_recoverer) << "Got SimpleConfig " << simple_config_;
      }

      simple_config_expire_at_ = get_config_expire_time();
      simple_config_at_ = Time::now_cached();
      for (size_t i = 1; i < simple_config_.dc_options.size(); i++) {
        std::swap(simple_config_.dc_options[i], simple_config_.dc_options[Random::fast(0, static_cast<int>(i))]);
      }
    } else {
      VLOG(config_recoverer) << "Get SimpleConfig error " << r_simple_config.error();
      simple_config_ = DcOptions();
      simple_config_expire_at_ = get_failed_config_expire_time();
    }
    update_dc_options();
    loop();
  }

  void on_full_config(Result<FullConfig> r_full_config, bool dummy) {
    full_config_query_.reset();
    if (r_full_config.is_ok()) {
      full_config_ = r_full_config.move_as_ok();
      VLOG(config_recoverer) << "Got FullConfig " << to_string(full_config_);
      full_config_expire_at_ = get_config_expire_time();
      send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_options, DcOptions(full_config_->dc_options_));
    } else {
      VLOG(config_recoverer) << "Get FullConfig error " << r_full_config.error();
      full_config_ = FullConfig();
      full_config_expire_at_ = get_failed_config_expire_time();
    }
    loop();
  }

  bool expect_blocking() const {
    return G()->shared_config().get_option_boolean("expect_blocking", true);
  }

  double get_config_expire_time() const {
    auto offline_delay = is_online_ ? 0 : 5 * 60;
    auto expire_time = expect_blocking() ? Random::fast(2 * 60, 3 * 60) : Random::fast(20 * 60, 30 * 60);
    return Time::now() + offline_delay + expire_time;
  }

  double get_failed_config_expire_time() const {
    auto offline_delay = is_online_ ? 0 : 5 * 60;
    auto expire_time = expect_blocking() ? Random::fast(5, 7) : Random::fast(15, 30);
    return Time::now() + offline_delay + expire_time;
  }

  bool is_connecting_{false};
  double connecting_since_{0};

  bool is_online_{false};

  bool has_network_{false};
  double has_network_since_{0};
  uint32 network_generation_{0};

  DcOptions simple_config_;
  double simple_config_expire_at_{0};
  double simple_config_at_{0};
  ActorOwn<> simple_config_query_;

  DcOptions dc_options_update_;

  DcOptions dc_options_;  // dc_options_update_ + simple_config_
  double dc_options_at_{0};
  size_t dc_options_i_;

  FullConfig full_config_;
  double full_config_expire_at_{0};
  ActorOwn<> full_config_query_;

  uint32 ref_cnt_{1};
  bool close_flag_{false};
  uint8 simple_config_turn_{0};

  ActorShared<> parent_;

  void hangup_shared() override {
    ref_cnt_--;
    try_stop();
  }
  void hangup() override {
    ref_cnt_--;
    close_flag_ = true;
    full_config_query_.reset();
    simple_config_query_.reset();
    try_stop();
  }

  void try_stop() {
    if (ref_cnt_ == 0) {
      stop();
    }
  }

  double max_connecting_delay() const {
    return expect_blocking() ? 5 : 20;
  }
  void loop() override {
    if (close_flag_) {
      return;
    }

    if (is_connecting_) {
      VLOG(config_recoverer) << "Failed to connect for " << Time::now_cached() - connecting_since_;
    } else {
      VLOG(config_recoverer) << "Successfully connected";
    }

    Timestamp wakeup_timestamp;
    auto check_timeout = [&](Timestamp timestamp) {
      if (timestamp.at() < Time::now_cached()) {
        return true;
      }
      wakeup_timestamp.relax(timestamp);
      return false;
    };

    bool has_connecting_problem =
        is_connecting_ && check_timeout(Timestamp::at(connecting_since_ + max_connecting_delay()));
    bool is_valid_simple_config = !check_timeout(Timestamp::at(simple_config_expire_at_));
    if (!is_valid_simple_config && !simple_config_.dc_options.empty()) {
      simple_config_ = DcOptions();
      update_dc_options();
    }
    bool need_simple_config = has_connecting_problem && !is_valid_simple_config && simple_config_query_.empty();
    bool has_dc_options = !dc_options_.dc_options.empty();
    bool is_valid_full_config = !check_timeout(Timestamp::at(full_config_expire_at_));
    bool need_full_config = has_connecting_problem && has_dc_options && !is_valid_full_config &&
                            full_config_query_.empty() &&
                            check_timeout(Timestamp::at(dc_options_at_ + (expect_blocking() ? 5 : 10)));
    if (need_simple_config) {
      ref_cnt_++;
      VLOG(config_recoverer) << "ASK SIMPLE CONFIG";
      auto promise = PromiseCreator::lambda([actor_id = actor_shared(this)](Result<SimpleConfig> r_simple_config) {
        send_closure(actor_id, &ConfigRecoverer::on_simple_config, std::move(r_simple_config), false);
      });
      auto get_simple_config = [&]() {
        switch (simple_config_turn_ % 3) {
          case 1:
            return get_simple_config_azure;
          case 0:
          case 2:
          default:
            return get_simple_config_google_dns;
        }
      }();
      simple_config_query_ =
          get_simple_config(std::move(promise), &G()->shared_config(), G()->is_test_dc(), G()->get_gc_scheduler_id());
      simple_config_turn_++;
    }

    if (need_full_config) {
      ref_cnt_++;
      VLOG(config_recoverer) << "ASK FULL CONFIG";
      full_config_query_ = get_full_config(
          dc_options_.dc_options[dc_options_i_].get_dc_id(), dc_options_.dc_options[dc_options_i_].get_ip_address(),
          PromiseCreator::lambda([actor_id = actor_shared(this)](Result<FullConfig> r_full_config) {
            send_closure(actor_id, &ConfigRecoverer::on_full_config, std::move(r_full_config), false);
          }));
      dc_options_i_ = (dc_options_i_ + 1) % dc_options_.dc_options.size();
    }

    if (wakeup_timestamp) {
      VLOG(config_recoverer) << "Wakeup in " << format::as_time(wakeup_timestamp.in());
      set_timeout_at(wakeup_timestamp.at());
    } else {
      VLOG(config_recoverer) << "Wakeup NEVER";
    }
  }

  void start_up() override {
    class StateCallback : public StateManager::Callback {
     public:
      explicit StateCallback(ActorId<ConfigRecoverer> parent) : parent_(std::move(parent)) {
      }
      bool on_state(StateManager::State state) override {
        send_closure(parent_, &ConfigRecoverer::on_connecting, state == StateManager::State::Connecting);
        return parent_.is_alive();
      }
      bool on_network(NetType network_type, uint32 network_generation) override {
        send_closure(parent_, &ConfigRecoverer::on_network, network_type != NetType::None, network_generation);
        return parent_.is_alive();
      }
      bool on_online(bool online_flag) override {
        send_closure(parent_, &ConfigRecoverer::on_online, online_flag);
        return parent_.is_alive();
      }

     private:
      ActorId<ConfigRecoverer> parent_;
    };
    send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));
  }

  void update_dc_options() {
    auto v = simple_config_.dc_options;
    v.insert(v.begin(), dc_options_update_.dc_options.begin(), dc_options_update_.dc_options.end());
    if (v != dc_options_.dc_options) {
      dc_options_.dc_options = std::move(v);
      dc_options_i_ = 0;
      dc_options_at_ = Time::now();
    }
  }
};

ConfigManager::ConfigManager(ActorShared<> parent) : parent_(std::move(parent)) {
}

void ConfigManager::start_up() {
  // TODO there are some problems when many ConfigRecoverers starts at the same time
  // if (G()->parameters().use_file_db) {
  ref_cnt_++;
  config_recoverer_ = create_actor<ConfigRecoverer>("Recoverer", actor_shared());
  send_closure(config_recoverer_, &ConfigRecoverer::on_dc_options_update, load_dc_options_update());
  // }
  auto expire = load_config_expire();
  if (expire.is_in_past()) {
    request_config();
  } else {
    expire_ = expire;
    set_timeout_in(expire_.in());
  }
}

void ConfigManager::hangup_shared() {
  ref_cnt_--;
  try_stop();
}
void ConfigManager::hangup() {
  ref_cnt_--;
  config_recoverer_.reset();
  try_stop();
}
void ConfigManager::loop() {
  if (expire_ && expire_.is_in_past()) {
    request_config();
    expire_ = {};
  }
}
void ConfigManager::try_stop() {
  if (ref_cnt_ == 0) {
    stop();
  }
}
void ConfigManager::request_config() {
  if (config_sent_cnt_ != 0) {
    return;
  }
  request_config_from_dc_impl(DcId::main());
}

void ConfigManager::on_dc_options_update(DcOptions dc_options) {
  save_dc_options_update(dc_options);
  send_closure(config_recoverer_, &ConfigRecoverer::on_dc_options_update, std::move(dc_options));
  if (dc_options.dc_options.empty()) {
    return;
  }
  expire_ = Timestamp::now();
  save_config_expire(expire_);
  set_timeout_in(expire_.in());
}

void ConfigManager::request_config_from_dc_impl(DcId dc_id) {
  config_sent_cnt_++;
  G()->net_query_dispatcher().dispatch_with_callback(
      G()->net_query_creator().create(create_storer(telegram_api::help_getConfig()), dc_id, NetQuery::Type::Common,
                                      NetQuery::AuthFlag::Off, NetQuery::GzipFlag::On, 60 * 60 * 24),
      actor_shared(this));
}

void ConfigManager::on_result(NetQueryPtr res) {
  CHECK(config_sent_cnt_ > 0);
  config_sent_cnt_--;
  auto r_config = fetch_result<telegram_api::help_getConfig>(std::move(res));
  if (r_config.is_error()) {
    if (!G()->close_flag()) {
      LOG(ERROR) << "TODO: getConfig failed: " << r_config.error();
      expire_ = Timestamp::in(60.0);  // try again in a minute
      set_timeout_in(expire_.in());
    }
  } else {
    on_dc_options_update(DcOptions());
    process_config(r_config.move_as_ok());
  }
}

void ConfigManager::save_dc_options_update(DcOptions dc_options) {
  if (dc_options.dc_options.empty()) {
    G()->td_db()->get_binlog_pmc()->erase("dc_options_update");
    return;
  }
  G()->td_db()->get_binlog_pmc()->set("dc_options_update", log_event_store(dc_options).as_slice().str());
}

DcOptions ConfigManager::load_dc_options_update() {
  auto log_event_dc_options = G()->td_db()->get_binlog_pmc()->get("dc_options_update");
  DcOptions dc_options;
  if (!log_event_dc_options.empty()) {
    log_event_parse(dc_options, log_event_dc_options).ensure();
  }
  return dc_options;
}

Timestamp ConfigManager::load_config_expire() {
  auto expire_in = to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("config_expire")) - Clocks::system();

  if (expire_in < 0 || expire_in > 60 * 60 /* 1 hour */) {
    return Timestamp::now();
  } else {
    return Timestamp::in(expire_in);
  }
}

void ConfigManager::save_config_expire(Timestamp timestamp) {
  G()->td_db()->get_binlog_pmc()->set("config_expire", to_string(static_cast<int>(Clocks::system() + expire_.in())));
}

void ConfigManager::process_config(tl_object_ptr<telegram_api::config> config) {
  bool is_from_main_dc = G()->net_query_dispatcher().main_dc_id().get_value() == config->this_dc_;

  LOG(INFO) << to_string(config);
  auto reload_in = max(60 /* at least 60 seconds*/, config->expires_ - config->date_);
  save_config_expire(Timestamp::in(reload_in));
  reload_in -= Random::fast(0, reload_in / 5);
  if (!is_from_main_dc) {
    reload_in = 0;
  }
  expire_ = Timestamp::in(reload_in);
  set_timeout_at(expire_.at());
  LOG_IF(ERROR, config->test_mode_ != G()->is_test_dc()) << "Wrong parameter is_test";

  ConfigShared &shared_config = G()->shared_config();

  // Do not save dc_options in config, because it will be interpreted and saved by ConnectionCreator.
  send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_options, DcOptions(config->dc_options_));

  shared_config.set_option_integer("recent_stickers_limit", config->stickers_recent_limit_);
  shared_config.set_option_integer("favorite_stickers_limit", config->stickers_faved_limit_);
  shared_config.set_option_integer("saved_animations_limit", config->saved_gifs_limit_);
  shared_config.set_option_integer("channels_read_media_period", config->channels_read_media_period_);

  shared_config.set_option_boolean("test_mode", config->test_mode_);
  shared_config.set_option_integer("forwarded_message_count_max", config->forwarded_count_max_);
  shared_config.set_option_integer("basic_group_size_max", config->chat_size_max_);
  shared_config.set_option_integer("supergroup_size_max", config->megagroup_size_max_);
  shared_config.set_option_integer("pinned_chat_count_max", config->pinned_dialogs_count_max_);
  if (is_from_main_dc || !shared_config.have_option("expect_blocking")) {
    shared_config.set_option_boolean("expect_blocking",
                                     (config->flags_ & telegram_api::config::BLOCKED_MODE_MASK) != 0);
  }
  if (is_from_main_dc || !shared_config.have_option("dc_txt_domain_name")) {
    shared_config.set_option_string("dc_txt_domain_name", config->dc_txt_domain_name_);
  }
  if (is_from_main_dc || !shared_config.have_option("t_me_url")) {
    auto url = config->me_url_prefix_;
    if (!url.empty()) {
      if (url.back() != '/') {
        url.push_back('/');
      }
      shared_config.set_option_string("t_me_url", url);
    }
  }
  if (is_from_main_dc) {
    shared_config.set_option_integer("webfile_dc_id", config->webfile_dc_id_);
    if ((config->flags_ & telegram_api::config::TMP_SESSIONS_MASK) != 0) {
      shared_config.set_option_integer("session_count", config->tmp_sessions_);
    } else {
      shared_config.set_option_empty("session_count");
    }
    if ((config->flags_ & telegram_api::config::SUGGESTED_LANG_CODE_MASK) != 0) {
      shared_config.set_option_string("suggested_language_pack_id", config->suggested_lang_code_);
      shared_config.set_option_integer("language_pack_version", config->lang_pack_version_);
    } else {
      shared_config.set_option_empty("suggested_language_pack_id");
      shared_config.set_option_empty("language_pack_version");
    }
  }

  if (is_from_main_dc) {
    shared_config.set_option_integer("edit_time_limit", config->edit_time_limit_);
    shared_config.set_option_boolean("revoke_pm_inbox",
                                     (config->flags_ & telegram_api::config::REVOKE_PM_INBOX_MASK) != 0);
    shared_config.set_option_integer("revoke_time_limit", config->revoke_time_limit_);
    shared_config.set_option_integer("revoke_pm_time_limit", config->revoke_pm_time_limit_);

    shared_config.set_option_integer("rating_e_decay", config->rating_e_decay_);

    shared_config.set_option_boolean("calls_enabled", config->phonecalls_enabled_);
  }
  shared_config.set_option_integer("call_ring_timeout_ms", config->call_ring_timeout_ms_);
  shared_config.set_option_integer("call_connect_timeout_ms", config->call_connect_timeout_ms_);
  shared_config.set_option_integer("call_packet_timeout_ms", config->call_packet_timeout_ms_);
  shared_config.set_option_integer("call_receive_timeout_ms", config->call_receive_timeout_ms_);

  shared_config.set_option_integer("message_text_length_max", config->message_length_max_);
  shared_config.set_option_integer("message_caption_length_max", config->caption_length_max_);

  if (config->gif_search_username_.empty()) {
    shared_config.set_option_empty("animation_search_bot_username");
  } else {
    shared_config.set_option_string("animation_search_bot_username", config->gif_search_username_);
  }
  if (config->venue_search_username_.empty()) {
    shared_config.set_option_empty("venue_search_bot_username");
  } else {
    shared_config.set_option_string("venue_search_bot_username", config->venue_search_username_);
  }
  if (config->img_search_username_.empty()) {
    shared_config.set_option_empty("photo_search_bot_username");
  } else {
    shared_config.set_option_string("photo_search_bot_username", config->img_search_username_);
  }

  // delete outdated options
  shared_config.set_option_empty("suggested_language_code");
  shared_config.set_option_empty("chat_big_size");
  shared_config.set_option_empty("group_size_max");
  shared_config.set_option_empty("saved_gifs_limit");
  shared_config.set_option_empty("sessions_count");
  shared_config.set_option_empty("forwarded_messages_count_max");
  shared_config.set_option_empty("broadcast_size_max");
  shared_config.set_option_empty("group_chat_size_max");
  shared_config.set_option_empty("chat_size_max");
  shared_config.set_option_empty("megagroup_size_max");
  shared_config.set_option_empty("online_update_period_ms");
  shared_config.set_option_empty("offline_blur_timeout_ms");
  shared_config.set_option_empty("offline_idle_timeout_ms");
  shared_config.set_option_empty("online_cloud_timeout_ms");
  shared_config.set_option_empty("notify_cloud_delay_ms");
  shared_config.set_option_empty("notify_default_delay_ms");
  shared_config.set_option_empty("large_chat_size");

  if (is_from_main_dc) {
    for (auto &feature : shared_config.get_options("disabled_")) {
      shared_config.set_option_empty(feature.first);
    }
  }

  // TODO implement online status updates
  //  shared_config.set_option_integer("online_update_period_ms", config->online_update_period_ms_);
  //  shared_config.set_option_integer("offline_blur_timeout_ms", config->offline_blur_timeout_ms_);
  //  shared_config.set_option_integer("offline_idle_timeout_ms", config->offline_idle_timeout_ms_);
  //  shared_config.set_option_integer("online_cloud_timeout_ms", config->online_cloud_timeout_ms_);
  //  shared_config.set_option_integer("notify_cloud_delay_ms", config->notify_cloud_delay_ms_);
  //  shared_config.set_option_integer("notify_default_delay_ms", config->notify_default_delay_ms_);

  //  shared_config.set_option_integer("push_chat_period_ms", config->push_chat_period_ms_);
  //  shared_config.set_option_integer("push_chat_limit", config->push_chat_limit_);
}

}  // namespace td
