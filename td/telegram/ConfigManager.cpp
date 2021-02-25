//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ConfigManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/Global.h"
#include "td/telegram/JsonValue.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/net/AuthDataShared.h"
#include "td/telegram/net/ConnectionCreator.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/net/DcOptions.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/net/NetQueryDispatcher.h"
#include "td/telegram/net/NetType.h"
#include "td/telegram/net/PublicRsaKeyShared.h"
#include "td/telegram/net/Session.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/telegram_api.h"

#include "td/mtproto/AuthData.h"
#include "td/mtproto/AuthKey.h"
#include "td/mtproto/RawConnection.h"
#include "td/mtproto/RSA.h"
#include "td/mtproto/TransportType.h"

#include "td/net/HttpQuery.h"
#if !TD_EMSCRIPTEN  //FIXME
#include "td/net/SslStream.h"
#include "td/net/Wget.h"
#endif

#include "td/actor/actor.h"

#include "td/utils/algorithm.h"
#include "td/utils/base64.h"
#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/crypto.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Parser.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/Random.h"
#include "td/utils/Time.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/tl_parsers.h"
#include "td/utils/UInt.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>

namespace td {

int VERBOSITY_NAME(config_recoverer) = VERBOSITY_NAME(INFO);

Result<int32> HttpDate::to_unix_time(int32 year, int32 month, int32 day, int32 hour, int32 minute, int32 second) {
  if (year < 1970 || year > 2037) {
    return Status::Error("Invalid year");
  }
  if (month < 1 || month > 12) {
    return Status::Error("Invalid month");
  }
  if (day < 1 || day > days_in_month(year, month)) {
    return Status::Error("Invalid day");
  }
  if (hour < 0 || hour >= 24) {
    return Status::Error("Invalid hour");
  }
  if (minute < 0 || minute >= 60) {
    return Status::Error("Invalid minute");
  }
  if (second < 0 || second > 60) {
    return Status::Error("Invalid second");
  }

  int32 res = 0;
  for (int32 y = 1970; y < year; y++) {
    res += (is_leap(y) + 365) * seconds_in_day();
  }
  for (int32 m = 1; m < month; m++) {
    res += days_in_month(year, m) * seconds_in_day();
  }
  res += (day - 1) * seconds_in_day();
  res += hour * 60 * 60;
  res += minute * 60;
  res += second;
  return res;
}

Result<int32> HttpDate::parse_http_date(string slice) {
  Parser p(slice);
  p.read_till(',');  // ignore week day
  p.skip(',');
  p.skip_whitespaces();
  p.skip_nofail('0');
  TRY_RESULT(day, to_integer_safe<int32>(p.read_word()));
  auto month_name = p.read_word();
  to_lower_inplace(month_name);
  TRY_RESULT(year, to_integer_safe<int32>(p.read_word()));
  p.skip_whitespaces();
  p.skip_nofail('0');
  TRY_RESULT(hour, to_integer_safe<int32>(p.read_till(':')));
  p.skip(':');
  p.skip_nofail('0');
  TRY_RESULT(minute, to_integer_safe<int32>(p.read_till(':')));
  p.skip(':');
  p.skip_nofail('0');
  TRY_RESULT(second, to_integer_safe<int32>(p.read_word()));
  auto gmt = p.read_word();
  TRY_STATUS(std::move(p.status()));
  if (gmt != "GMT") {
    return Status::Error("Timezone must be GMT");
  }

  static Slice month_names[12] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};

  int month = 0;

  for (int m = 1; m <= 12; m++) {
    if (month_names[m - 1] == month_name) {
      month = m;
      break;
    }
  }

  if (month == 0) {
    return Status::Error("Unknown month name");
  }

  return HttpDate::to_unix_time(year, month, day, hour, minute, second);
}

Result<SimpleConfig> decode_config(Slice input) {
  static auto rsa = RSA::from_pem_public_key(
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
  rsa.decrypt_signature(data_rsa_slice, data_rsa_slice);

  MutableSlice data_cbc = data_rsa_slice.substr(32);
  UInt256 key;
  UInt128 iv;
  as_slice(key).copy_from(data_rsa_slice.substr(0, 32));
  as_slice(iv).copy_from(data_rsa_slice.substr(16, 16));
  aes_cbc_decrypt(as_slice(key), as_slice(iv), data_cbc, data_cbc);

  CHECK(data_cbc.size() == 224);
  string hash(32, ' ');
  sha256(data_cbc.substr(0, 208), MutableSlice(hash));
  if (data_cbc.substr(208) != Slice(hash).substr(0, 16)) {
    return Status::Error("SHA256 mismatch");
  }

  TlParser len_parser{data_cbc};
  int len = len_parser.fetch_int();
  if (len < 8 || len > 208) {
    return Status::Error(PSLICE() << "Invalid " << tag("data length", len) << " after aes_cbc_decrypt");
  }
  int constructor_id = len_parser.fetch_int();
  if (constructor_id != telegram_api::help_configSimple::ID) {
    return Status::Error(PSLICE() << "Wrong " << tag("constructor", format::as_hex(constructor_id)));
  }
  BufferSlice raw_config(data_cbc.substr(8, len - 8));
  TlBufferParser parser{&raw_config};
  auto config = telegram_api::help_configSimple::fetch(parser);
  parser.fetch_end();
  TRY_STATUS(parser.get_status());
  return std::move(config);
}

static ActorOwn<> get_simple_config_impl(Promise<SimpleConfigResult> promise, int32 scheduler_id, string url,
                                         string host, std::vector<std::pair<string, string>> headers, bool prefer_ipv6,
                                         std::function<Result<string>(HttpQuery &)> get_config,
                                         string content = string(), string content_type = string()) {
  VLOG(config_recoverer) << "Request simple config from " << url;
#if TD_EMSCRIPTEN  // FIXME
  return ActorOwn<>();
#else
  const int timeout = 10;
  const int ttl = 3;
  headers.emplace_back("Host", std::move(host));
  headers.emplace_back("User-Agent",
                       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                       "Chrome/77.0.3865.90 Safari/537.36");
  return ActorOwn<>(create_actor_on_scheduler<Wget>(
      "Wget", scheduler_id,
      PromiseCreator::lambda([get_config = std::move(get_config),
                              promise = std::move(promise)](Result<unique_ptr<HttpQuery>> r_query) mutable {
        promise.set_result([&]() -> Result<SimpleConfigResult> {
          TRY_RESULT(http_query, std::move(r_query));
          SimpleConfigResult res;
          res.r_http_date = HttpDate::parse_http_date(http_query->get_header("date").str());
          auto r_config = get_config(*http_query);
          if (r_config.is_error()) {
            res.r_config = r_config.move_as_error();
          } else {
            res.r_config = decode_config(r_config.ok());
          }
          return std::move(res);
        }());
      }),
      std::move(url), std::move(headers), timeout, ttl, prefer_ipv6, SslStream::VerifyPeer::Off, std::move(content),
      std::move(content_type)));
#endif
}

ActorOwn<> get_simple_config_azure(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config, bool is_test,
                                   int32 scheduler_id) {
  string url = PSTRING() << "https://software-download.microsoft.com/" << (is_test ? "test" : "prod")
                         << "v2/config.txt";
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  return get_simple_config_impl(std::move(promise), scheduler_id, std::move(url), "tcdnb.azureedge.net", {},
                                prefer_ipv6,
                                [](HttpQuery &http_query) -> Result<string> { return http_query.content_.str(); });
}

static ActorOwn<> get_simple_config_dns(Slice address, Slice host, Promise<SimpleConfigResult> promise,
                                        const ConfigShared *shared_config, bool is_test, int32 scheduler_id) {
  string name = shared_config == nullptr ? string() : shared_config->get_option_string("dc_txt_domain_name");
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  if (name.empty()) {
    name = is_test ? "tapv3.stel.com" : "apv3.stel.com";
  }
  auto get_config = [](HttpQuery &http_query) -> Result<string> {
    VLOG(config_recoverer) << "Receive DNS response " << http_query.content_;
    TRY_RESULT(json, json_decode(http_query.content_));
    if (json.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object");
    }
    auto &answer_object = json.get_object();
    TRY_RESULT(answer, get_json_object_field(answer_object, "Answer", JsonValue::Type::Array, false));
    auto &answer_array = answer.get_array();
    vector<string> parts;
    for (auto &answer_part : answer_array) {
      if (answer_part.type() != JsonValue::Type::Object) {
        return Status::Error("Expected JSON object");
      }
      auto &data_object = answer_part.get_object();
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
    return data;
  };
  return get_simple_config_impl(std::move(promise), scheduler_id,
                                PSTRING() << "https://" << address << "?name=" << url_encode(name) << "&type=TXT",
                                host.str(), {{"Accept", "application/dns-json"}}, prefer_ipv6, std::move(get_config));
}

ActorOwn<> get_simple_config_google_dns(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                        bool is_test, int32 scheduler_id) {
  return get_simple_config_dns("dns.google/resolve", "dns.google", std::move(promise), shared_config, is_test,
                               scheduler_id);
}

ActorOwn<> get_simple_config_mozilla_dns(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                         bool is_test, int32 scheduler_id) {
  return get_simple_config_dns("mozilla.cloudflare-dns.com/dns-query", "mozilla.cloudflare-dns.com", std::move(promise),
                               shared_config, is_test, scheduler_id);
}

static string generate_firebase_remote_config_payload() {
  unsigned char buf[17];
  Random::secure_bytes(buf, sizeof(buf));
  buf[0] = static_cast<unsigned char>((buf[0] & 0xF0) | 0x07);
  auto app_instance_id = base64url_encode(Slice(buf, sizeof(buf)));
  app_instance_id.resize(22);
  return PSTRING() << "{\"app_id\":\"1:560508485281:web:4ee13a6af4e84d49e67ae0\",\"app_instance_id\":\""
                   << app_instance_id << "\"}";
}

ActorOwn<> get_simple_config_firebase_remote_config(Promise<SimpleConfigResult> promise,
                                                    const ConfigShared *shared_config, bool is_test,
                                                    int32 scheduler_id) {
  if (is_test) {
    promise.set_error(Status::Error(400, "Test config is not supported"));
    return ActorOwn<>();
  }

  static const string payload = generate_firebase_remote_config_payload();
  string url =
      "https://firebaseremoteconfig.googleapis.com/v1/projects/peak-vista-421/namespaces/"
      "firebase:fetch?key=AIzaSyC2-kAkpDsroixRXw-sTw-Wfqo4NxjMwwM";
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  auto get_config = [](HttpQuery &http_query) -> Result<string> {
    TRY_RESULT(json, json_decode(http_query.get_arg("entries")));
    if (json.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object");
    }
    auto &entries_object = json.get_object();
    TRY_RESULT(config, get_json_object_string_field(entries_object, "ipconfigv3", false));
    return std::move(config);
  };
  return get_simple_config_impl(std::move(promise), scheduler_id, std::move(url), "firebaseremoteconfig.googleapis.com",
                                {}, prefer_ipv6, std::move(get_config), payload, "application/json");
}

ActorOwn<> get_simple_config_firebase_realtime(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                               bool is_test, int32 scheduler_id) {
  if (is_test) {
    promise.set_error(Status::Error(400, "Test config is not supported"));
    return ActorOwn<>();
  }

  string url = "https://reserve-5a846.firebaseio.com/ipconfigv3.json";
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  auto get_config = [](HttpQuery &http_query) -> Result<string> {
    return http_query.get_arg("content").str();
  };
  return get_simple_config_impl(std::move(promise), scheduler_id, std::move(url), "reserve-5a846.firebaseio.com", {},
                                prefer_ipv6, std::move(get_config));
}

ActorOwn<> get_simple_config_firebase_firestore(Promise<SimpleConfigResult> promise, const ConfigShared *shared_config,
                                                bool is_test, int32 scheduler_id) {
  if (is_test) {
    promise.set_error(Status::Error(400, "Test config is not supported"));
    return ActorOwn<>();
  }

  string url = "https://www.google.com/v1/projects/reserve-5a846/databases/(default)/documents/ipconfig/v3";
  const bool prefer_ipv6 = shared_config == nullptr ? false : shared_config->get_option_boolean("prefer_ipv6");
  auto get_config = [](HttpQuery &http_query) -> Result<string> {
    TRY_RESULT(json, json_decode(http_query.get_arg("fields")));
    if (json.type() != JsonValue::Type::Object) {
      return Status::Error("Expected JSON object");
    }
    TRY_RESULT(data, get_json_object_field(json.get_object(), "data", JsonValue::Type::Object, false));
    TRY_RESULT(config, get_json_object_string_field(data.get_object(), "stringValue", false));
    return std::move(config);
  };
  return get_simple_config_impl(std::move(promise), scheduler_id, std::move(url), "firestore.googleapis.com", {},
                                prefer_ipv6, std::move(get_config));
}

ActorOwn<> get_full_config(DcOption option, Promise<FullConfig> promise, ActorShared<> parent) {
  class SessionCallback : public Session::Callback {
   public:
    SessionCallback(ActorShared<> parent, DcOption option) : parent_(std::move(parent)), option_(std::move(option)) {
    }
    void on_failed() final {
    }
    void on_closed() final {
    }
    void request_raw_connection(unique_ptr<mtproto::AuthData> auth_data,
                                Promise<unique_ptr<mtproto::RawConnection>> promise) final {
      request_raw_connection_cnt_++;
      VLOG(config_recoverer) << "Request full config from " << option_.get_ip_address()
                             << ", try = " << request_raw_connection_cnt_;
      if (request_raw_connection_cnt_ <= 2) {
        send_closure(G()->connection_creator(), &ConnectionCreator::request_raw_connection_by_ip,
                     option_.get_ip_address(),
                     mtproto::TransportType{mtproto::TransportType::ObfuscatedTcp,
                                            narrow_cast<int16>(option_.get_dc_id().get_raw_id()), option_.get_secret()},
                     std::move(promise));
      } else {
        // Delay all queries except first forever
        delay_forever_.push_back(std::move(promise));
      }
    }
    void on_tmp_auth_key_updated(mtproto::AuthKey auth_key) final {
      // nop
    }
    void on_result(NetQueryPtr net_query) final {
      G()->net_query_dispatcher().dispatch(std::move(net_query));
    }

   private:
    ActorShared<> parent_;
    DcOption option_;
    size_t request_raw_connection_cnt_{0};
    std::vector<Promise<unique_ptr<mtproto::RawConnection>>> delay_forever_;
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
    AuthKeyState get_auth_key_state() override {
      return AuthDataShared::get_auth_key_state(get_auth_key());
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
    std::shared_ptr<PublicRsaKeyShared> public_rsa_key_ =
        std::make_shared<PublicRsaKeyShared>(DcId::empty(), G()->is_test_dc());

    std::vector<unique_ptr<Listener>> auth_key_listeners_;
    void notify() {
      td::remove_if(auth_key_listeners_, [&](auto &listener) { return !listener->notify(); });
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
    GetConfigActor(DcOption option, Promise<FullConfig> promise, ActorShared<> parent)
        : option_(std::move(option)), promise_(std::move(promise)), parent_(std::move(parent)) {
    }

   private:
    void start_up() override {
      auto auth_data = std::make_shared<SimpleAuthData>(option_.get_dc_id());
      int32 raw_dc_id = option_.get_dc_id().get_raw_id();
      auto session_callback = make_unique<SessionCallback>(actor_shared(this, 1), std::move(option_));

      int32 int_dc_id = raw_dc_id;
      if (G()->is_test_dc()) {
        int_dc_id += 10000;
      }
      session_ = create_actor<Session>("ConfigSession", std::move(session_callback), std::move(auth_data), raw_dc_id,
                                       int_dc_id, false /*is_main*/, true /*use_pfs*/, false /*is_cdn*/,
                                       false /*need_destroy_auth_key*/, mtproto::AuthKey(),
                                       std::vector<mtproto::ServerSalt>());
      auto query = G()->net_query_creator().create_unauth(telegram_api::help_getConfig(), DcId::empty());
      query->total_timeout_limit_ = 60 * 60 * 24;
      query->set_callback(actor_shared(this));
      query->dispatch_ttl_ = 0;
      send_closure(session_, &Session::send, std::move(query));
      set_timeout_in(10);
    }
    void on_result(NetQueryPtr query) override {
      promise_.set_result(fetch_result<telegram_api::help_getConfig>(std::move(query)));
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

    DcOption option_;
    ActorOwn<Session> session_;
    Promise<FullConfig> promise_;
    ActorShared<> parent_;
  };

  return ActorOwn<>(create_actor<GetConfigActor>("GetConfigActor", option, std::move(promise), std::move(parent)));
}

class ConfigRecoverer : public Actor {
 public:
  explicit ConfigRecoverer(ActorShared<> parent) : parent_(std::move(parent)) {
    connecting_since_ = Time::now();
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
        simple_config_expires_at_ = 0;
      }
      if (full_config_ == nullptr) {
        full_config_expires_at_ = 0;
      }
    }
    loop();
  }
  void on_connecting(bool is_connecting) {
    VLOG(config_recoverer) << "On connecting " << is_connecting;
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

  void on_simple_config(Result<SimpleConfigResult> r_simple_config_result, bool dummy) {
    simple_config_query_.reset();
    dc_options_i_ = 0;

    SimpleConfigResult cfg;
    if (r_simple_config_result.is_error()) {
      cfg.r_http_date = r_simple_config_result.error().clone();
      cfg.r_config = r_simple_config_result.move_as_error();
    } else {
      cfg = r_simple_config_result.move_as_ok();
    }

    if (cfg.r_http_date.is_ok() && (date_option_i_ == 0 || cfg.r_config.is_error())) {
      G()->update_dns_time_difference(cfg.r_http_date.ok() - Time::now());
    } else if (cfg.r_config.is_ok()) {
      G()->update_dns_time_difference(cfg.r_config.ok()->date_ - Time::now());
    }
    date_option_i_ = (date_option_i_ + 1) % 2;

    do_on_simple_config(std::move(cfg.r_config));
    update_dc_options();
    loop();
  }

  void do_on_simple_config(Result<SimpleConfig> r_simple_config) {
    if (r_simple_config.is_ok()) {
      auto config = r_simple_config.move_as_ok();
      VLOG(config_recoverer) << "Receive raw " << to_string(config);
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
      } else {
        VLOG(config_recoverer) << "Config has expired at " << config->expires_;
      }

      simple_config_expires_at_ = get_config_expire_time();
      simple_config_at_ = Time::now_cached();
      for (size_t i = 1; i < simple_config_.dc_options.size(); i++) {
        std::swap(simple_config_.dc_options[i], simple_config_.dc_options[Random::fast(0, static_cast<int>(i))]);
      }
    } else {
      VLOG(config_recoverer) << "Get SimpleConfig error " << r_simple_config.error();
      simple_config_ = DcOptions();
      simple_config_expires_at_ = get_failed_config_expire_time();
    }
  }

  void on_full_config(Result<FullConfig> r_full_config, bool dummy) {
    full_config_query_.reset();
    if (r_full_config.is_ok()) {
      full_config_ = r_full_config.move_as_ok();
      VLOG(config_recoverer) << "Got FullConfig " << to_string(full_config_);
      full_config_expires_at_ = get_config_expire_time();
      send_closure(G()->connection_creator(), &ConnectionCreator::on_dc_options, DcOptions(full_config_->dc_options_));
    } else {
      VLOG(config_recoverer) << "Get FullConfig error " << r_full_config.error();
      full_config_ = FullConfig();
      full_config_expires_at_ = get_failed_config_expire_time();
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
  double simple_config_expires_at_{0};
  double simple_config_at_{0};
  ActorOwn<> simple_config_query_;

  DcOptions dc_options_update_;

  DcOptions dc_options_;  // dc_options_update_ + simple_config_
  double dc_options_at_{0};
  size_t dc_options_i_;

  size_t date_option_i_{0};

  FullConfig full_config_;
  double full_config_expires_at_{0};
  ActorOwn<> full_config_query_;

  uint32 ref_cnt_{1};
  bool close_flag_{false};
  uint32 simple_config_turn_{0};

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
      VLOG(config_recoverer) << "Failed to connect for " << Time::now() - connecting_since_;
    } else {
      VLOG(config_recoverer) << "Successfully connected in " << Time::now() - connecting_since_;
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
    bool is_valid_simple_config = !check_timeout(Timestamp::at(simple_config_expires_at_));
    if (!is_valid_simple_config && !simple_config_.dc_options.empty()) {
      simple_config_ = DcOptions();
      update_dc_options();
    }
    bool need_simple_config = has_connecting_problem && !is_valid_simple_config && simple_config_query_.empty();
    bool has_dc_options = !dc_options_.dc_options.empty();
    bool is_valid_full_config = !check_timeout(Timestamp::at(full_config_expires_at_));
    bool need_full_config = has_connecting_problem && has_dc_options && !is_valid_full_config &&
                            full_config_query_.empty() &&
                            check_timeout(Timestamp::at(dc_options_at_ + (expect_blocking() ? 5 : 10)));
    if (need_simple_config) {
      ref_cnt_++;
      VLOG(config_recoverer) << "Ask simple config with turn " << simple_config_turn_;
      auto promise =
          PromiseCreator::lambda([actor_id = actor_shared(this)](Result<SimpleConfigResult> r_simple_config) {
            send_closure(actor_id, &ConfigRecoverer::on_simple_config, std::move(r_simple_config), false);
          });
      auto get_simple_config = [&] {
        switch (simple_config_turn_ % 10) {
          case 6:
            return get_simple_config_azure;
          case 2:
            return get_simple_config_firebase_remote_config;
          case 4:
            return get_simple_config_firebase_realtime;
          case 9:
            return get_simple_config_firebase_firestore;
          case 0:
          case 3:
          case 8:
            return get_simple_config_google_dns;
          case 1:
          case 5:
          case 7:
          default:
            return get_simple_config_mozilla_dns;
        }
      }();
      simple_config_query_ =
          get_simple_config(std::move(promise), &G()->shared_config(), G()->is_test_dc(), G()->get_gc_scheduler_id());
      simple_config_turn_++;
    }

    if (need_full_config) {
      ref_cnt_++;
      VLOG(config_recoverer) << "Ask full config with dc_options_i_ = " << dc_options_i_;
      full_config_query_ =
          get_full_config(dc_options_.dc_options[dc_options_i_],
                          PromiseCreator::lambda([actor_id = actor_id(this)](Result<FullConfig> r_full_config) {
                            send_closure(actor_id, &ConfigRecoverer::on_full_config, std::move(r_full_config), false);
                          }),
                          actor_shared(this));
      dc_options_i_ = (dc_options_i_ + 1) % dc_options_.dc_options.size();
    }

    if (wakeup_timestamp) {
      VLOG(config_recoverer) << "Wakeup in " << format::as_time(wakeup_timestamp.in());
      set_timeout_at(wakeup_timestamp.at());
    } else {
      VLOG(config_recoverer) << "Wakeup never";
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
    auto new_dc_options = simple_config_.dc_options;
    new_dc_options.insert(new_dc_options.begin(), dc_options_update_.dc_options.begin(),
                          dc_options_update_.dc_options.end());
    if (new_dc_options != dc_options_.dc_options) {
      dc_options_.dc_options = std::move(new_dc_options);
      dc_options_i_ = 0;
      dc_options_at_ = Time::now();
    }
  }
};

ConfigManager::ConfigManager(ActorShared<> parent) : parent_(std::move(parent)) {
  lazy_request_flood_control_.add_limit(20, 1);
}

void ConfigManager::start_up() {
  config_recoverer_ = create_actor<ConfigRecoverer>("Recoverer", create_reference());
  send_closure(config_recoverer_, &ConfigRecoverer::on_dc_options_update, load_dc_options_update());

  auto expire_time = load_config_expire_time();
  if (expire_time.is_in_past() || true) {
    request_config();
  } else {
    expire_time_ = expire_time;
    set_timeout_in(expire_time_.in());
  }

  autologin_update_time_ = Time::now() - 365 * 86400;
  autologin_domains_ = full_split(G()->td_db()->get_binlog_pmc()->get("autologin_domains"), '\xFF');
}

ActorShared<> ConfigManager::create_reference() {
  ref_cnt_++;
  return actor_shared(this, REFCNT_TOKEN);
}

void ConfigManager::hangup_shared() {
  LOG_CHECK(get_link_token() == REFCNT_TOKEN) << "Expected REFCNT_TOKEN, got " << get_link_token();
  ref_cnt_--;
  try_stop();
}

void ConfigManager::hangup() {
  ref_cnt_--;
  config_recoverer_.reset();
  try_stop();
}

void ConfigManager::loop() {
  if (expire_time_ && expire_time_.is_in_past()) {
    request_config();
    expire_time_ = {};
  }
}

void ConfigManager::try_stop() {
  if (ref_cnt_ == 0) {
    stop();
  }
}

void ConfigManager::request_config() {
  if (G()->close_flag()) {
    return;
  }

  if (config_sent_cnt_ != 0) {
    return;
  }

  lazy_request_flood_control_.add_event(static_cast<int32>(Timestamp::now().at()));
  request_config_from_dc_impl(DcId::main());
}

void ConfigManager::lazy_request_config() {
  if (G()->close_flag()) {
    return;
  }

  if (config_sent_cnt_ != 0) {
    return;
  }

  expire_time_.relax(Timestamp::at(lazy_request_flood_control_.get_wakeup_at()));
  set_timeout_at(expire_time_.at());
}

void ConfigManager::get_app_config(Promise<td_api::object_ptr<td_api::JsonValue>> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  auto auth_manager = G()->td().get_actor_unsafe()->auth_manager_.get();
  if (auth_manager != nullptr && auth_manager->is_bot()) {
    return promise.set_value(nullptr);
  }

  get_app_config_queries_.push_back(std::move(promise));
  if (get_app_config_queries_.size() == 1) {
    auto query = G()->net_query_creator().create_unauth(telegram_api::help_getAppConfig());
    query->total_timeout_limit_ = 60 * 60 * 24;
    G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, 1));
  }
}

void ConfigManager::get_external_link(string &&link, Promise<string> &&promise) {
  if (G()->close_flag()) {
    return promise.set_value(std::move(link));
  }

  auto r_url = parse_url(link);
  if (r_url.is_error()) {
    return promise.set_value(std::move(link));
  }

  if (!td::contains(autologin_domains_, r_url.ok().host_)) {
    return promise.set_value(std::move(link));
  }

  if (autologin_update_time_ < Time::now() - 10000) {
    auto query_promise = PromiseCreator::lambda([link = std::move(link), promise = std::move(promise)](
                                                    Result<td_api::object_ptr<td_api::JsonValue>> &&result) mutable {
      if (result.is_error()) {
        return promise.set_value(std::move(link));
      }
      send_closure(G()->config_manager(), &ConfigManager::get_external_link, std::move(link), std::move(promise));
    });
    return get_app_config(std::move(query_promise));
  }

  if (autologin_token_.empty()) {
    return promise.set_value(std::move(link));
  }

  auto url = r_url.move_as_ok();
  url.protocol_ = HttpUrl::Protocol::Https;
  Slice path = url.query_;
  path.truncate(url.query_.find_first_of("?#"));
  Slice parameters_hash = Slice(url.query_).substr(path.size());
  Slice parameters = parameters_hash;
  parameters.truncate(parameters.find('#'));
  Slice hash = parameters_hash.substr(parameters.size());

  string added_parameter;
  if (parameters.empty()) {
    added_parameter = '?';
  } else if (parameters.size() == 1) {
    CHECK(parameters == "?");
  } else {
    added_parameter = '&';
  }
  added_parameter += "autologin_token=";
  added_parameter += autologin_token_;

  url.query_ = PSTRING() << path << parameters << added_parameter << hash;

  promise.set_value(url.get_url());
}

void ConfigManager::get_content_settings(Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  auto auth_manager = G()->td().get_actor_unsafe()->auth_manager_.get();
  if (auth_manager == nullptr || !auth_manager->is_authorized() || auth_manager->is_bot()) {
    return promise.set_value(Unit());
  }

  get_content_settings_queries_.push_back(std::move(promise));
  if (get_content_settings_queries_.size() == 1) {
    G()->net_query_dispatcher().dispatch_with_callback(
        G()->net_query_creator().create(telegram_api::account_getContentSettings()), actor_shared(this, 2));
  }
}

void ConfigManager::set_content_settings(bool ignore_sensitive_content_restrictions, Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  last_set_content_settings_ = ignore_sensitive_content_restrictions;
  auto &queries = set_content_settings_queries_[ignore_sensitive_content_restrictions];
  queries.push_back(std::move(promise));
  if (!is_set_content_settings_request_sent_) {
    is_set_content_settings_request_sent_ = true;
    int32 flags = 0;
    if (ignore_sensitive_content_restrictions) {
      flags |= telegram_api::account_setContentSettings::SENSITIVE_ENABLED_MASK;
    }
    G()->net_query_dispatcher().dispatch_with_callback(
        G()->net_query_creator().create(telegram_api::account_setContentSettings(flags, false /*ignored*/)),
        actor_shared(this, 3 + static_cast<uint64>(ignore_sensitive_content_restrictions)));
  }
}

void ConfigManager::get_global_privacy_settings(Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }

  auto auth_manager = G()->td().get_actor_unsafe()->auth_manager_.get();
  if (auth_manager == nullptr || !auth_manager->is_authorized() || auth_manager->is_bot()) {
    return promise.set_value(Unit());
  }

  get_global_privacy_settings_queries_.push_back(std::move(promise));
  if (get_global_privacy_settings_queries_.size() == 1) {
    G()->net_query_dispatcher().dispatch_with_callback(
        G()->net_query_creator().create(telegram_api::account_getGlobalPrivacySettings()), actor_shared(this, 5));
  }
}

void ConfigManager::set_archive_and_mute(bool archive_and_mute, Promise<Unit> &&promise) {
  if (G()->close_flag()) {
    return promise.set_error(Status::Error(500, "Request aborted"));
  }
  if (archive_and_mute) {
    remove_suggested_action(suggested_actions_, SuggestedAction{SuggestedAction::Type::EnableArchiveAndMuteNewChats});
  }

  last_set_archive_and_mute_ = archive_and_mute;
  auto &queries = set_archive_and_mute_queries_[archive_and_mute];
  queries.push_back(std::move(promise));
  if (!is_set_archive_and_mute_request_sent_) {
    is_set_archive_and_mute_request_sent_ = true;
    int32 flags = telegram_api::globalPrivacySettings::ARCHIVE_AND_MUTE_NEW_NONCONTACT_PEERS_MASK;
    auto settings = make_tl_object<telegram_api::globalPrivacySettings>(flags, archive_and_mute);
    G()->net_query_dispatcher().dispatch_with_callback(
        G()->net_query_creator().create(telegram_api::account_setGlobalPrivacySettings(std::move(settings))),
        actor_shared(this, 6 + static_cast<uint64>(archive_and_mute)));
  }
}

void ConfigManager::on_dc_options_update(DcOptions dc_options) {
  save_dc_options_update(dc_options);
  send_closure(config_recoverer_, &ConfigRecoverer::on_dc_options_update, std::move(dc_options));
  if (dc_options.dc_options.empty()) {
    return;
  }
  expire_time_ = Timestamp::now();
  save_config_expire(expire_time_);
  set_timeout_in(expire_time_.in());
}

void ConfigManager::request_config_from_dc_impl(DcId dc_id) {
  config_sent_cnt_++;
  auto query = G()->net_query_creator().create_unauth(telegram_api::help_getConfig(), dc_id);
  query->total_timeout_limit_ = 60 * 60 * 24;
  G()->net_query_dispatcher().dispatch_with_callback(std::move(query), actor_shared(this, 8));
}

void ConfigManager::do_set_ignore_sensitive_content_restrictions(bool ignore_sensitive_content_restrictions) {
  G()->shared_config().set_option_boolean("ignore_sensitive_content_restrictions",
                                          ignore_sensitive_content_restrictions);
  bool have_ignored_restriction_reasons = G()->shared_config().have_option("ignored_restriction_reasons");
  if (have_ignored_restriction_reasons != ignore_sensitive_content_restrictions) {
    get_app_config(Auto());
  }
}

void ConfigManager::do_set_archive_and_mute(bool archive_and_mute) {
  if (archive_and_mute) {
    remove_suggested_action(suggested_actions_, SuggestedAction{SuggestedAction::Type::EnableArchiveAndMuteNewChats});
  }
  G()->shared_config().set_option_boolean("archive_and_mute_new_chats_from_unknown_users", archive_and_mute);
}

void ConfigManager::dismiss_suggested_action(SuggestedAction suggested_action, Promise<Unit> &&promise) {
  auto action_str = suggested_action.get_suggested_action_str();
  if (action_str.empty()) {
    return promise.set_value(Unit());
  }

  if (!td::contains(suggested_actions_, suggested_action)) {
    return promise.set_value(Unit());
  }

  dismiss_suggested_action_request_count_++;
  auto type = static_cast<int32>(suggested_action.type_);
  auto &queries = dismiss_suggested_action_queries_[type];
  queries.push_back(std::move(promise));
  if (queries.size() == 1) {
    G()->net_query_dispatcher().dispatch_with_callback(
        G()->net_query_creator().create(
            telegram_api::help_dismissSuggestion(make_tl_object<telegram_api::inputPeerEmpty>(), action_str)),
        actor_shared(this, 100 + type));
  }
}

void ConfigManager::on_result(NetQueryPtr res) {
  auto token = get_link_token();
  if (token >= 100 && token <= 200) {
    auto type = static_cast<int32>(token - 100);
    SuggestedAction suggested_action{static_cast<SuggestedAction::Type>(type)};
    auto promises = std::move(dismiss_suggested_action_queries_[type]);
    dismiss_suggested_action_queries_.erase(type);
    CHECK(!promises.empty());
    CHECK(dismiss_suggested_action_request_count_ >= promises.size());
    dismiss_suggested_action_request_count_ -= promises.size();

    auto result_ptr = fetch_result<telegram_api::help_dismissSuggestion>(std::move(res));
    if (result_ptr.is_error()) {
      for (auto &promise : promises) {
        promise.set_error(result_ptr.error().clone());
      }
      return;
    }
    remove_suggested_action(suggested_actions_, suggested_action);
    get_app_config(Auto());

    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
    return;
  }
  if (token == 6 || token == 7) {
    is_set_archive_and_mute_request_sent_ = false;
    bool archive_and_mute = (token == 7);
    auto promises = std::move(set_archive_and_mute_queries_[archive_and_mute]);
    set_archive_and_mute_queries_[archive_and_mute].clear();
    CHECK(!promises.empty());
    auto result_ptr = fetch_result<telegram_api::account_setGlobalPrivacySettings>(std::move(res));
    if (result_ptr.is_error()) {
      for (auto &promise : promises) {
        promise.set_error(result_ptr.error().clone());
      }
    } else {
      if (last_set_archive_and_mute_ == archive_and_mute) {
        do_set_archive_and_mute(archive_and_mute);
      }

      for (auto &promise : promises) {
        promise.set_value(Unit());
      }
    }

    if (!set_archive_and_mute_queries_[!archive_and_mute].empty()) {
      if (archive_and_mute == last_set_archive_and_mute_) {
        promises = std::move(set_archive_and_mute_queries_[!archive_and_mute]);
        set_archive_and_mute_queries_[!archive_and_mute].clear();
        for (auto &promise : promises) {
          promise.set_value(Unit());
        }
      } else {
        set_archive_and_mute(!archive_and_mute, Auto());
      }
    }
    return;
  }
  if (token == 5) {
    auto promises = std::move(get_global_privacy_settings_queries_);
    get_global_privacy_settings_queries_.clear();
    CHECK(!promises.empty());
    auto result_ptr = fetch_result<telegram_api::account_getGlobalPrivacySettings>(std::move(res));
    if (result_ptr.is_error()) {
      for (auto &promise : promises) {
        promise.set_error(result_ptr.error().clone());
      }
      return;
    }

    auto result = result_ptr.move_as_ok();
    if ((result->flags_ & telegram_api::globalPrivacySettings::ARCHIVE_AND_MUTE_NEW_NONCONTACT_PEERS_MASK) != 0) {
      do_set_archive_and_mute(result->archive_and_mute_new_noncontact_peers_);
    } else {
      LOG(ERROR) << "Receive wrong response: " << to_string(result);
    }

    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
    return;
  }
  if (token == 3 || token == 4) {
    is_set_content_settings_request_sent_ = false;
    bool ignore_sensitive_content_restrictions = (token == 4);
    auto promises = std::move(set_content_settings_queries_[ignore_sensitive_content_restrictions]);
    set_content_settings_queries_[ignore_sensitive_content_restrictions].clear();
    CHECK(!promises.empty());
    auto result_ptr = fetch_result<telegram_api::account_setContentSettings>(std::move(res));
    if (result_ptr.is_error()) {
      for (auto &promise : promises) {
        promise.set_error(result_ptr.error().clone());
      }
    } else {
      if (G()->shared_config().get_option_boolean("can_ignore_sensitive_content_restrictions") &&
          last_set_content_settings_ == ignore_sensitive_content_restrictions) {
        do_set_ignore_sensitive_content_restrictions(ignore_sensitive_content_restrictions);
      }

      for (auto &promise : promises) {
        promise.set_value(Unit());
      }
    }

    if (!set_content_settings_queries_[!ignore_sensitive_content_restrictions].empty()) {
      if (ignore_sensitive_content_restrictions == last_set_content_settings_) {
        promises = std::move(set_content_settings_queries_[!ignore_sensitive_content_restrictions]);
        set_content_settings_queries_[!ignore_sensitive_content_restrictions].clear();
        for (auto &promise : promises) {
          promise.set_value(Unit());
        }
      } else {
        set_content_settings(!ignore_sensitive_content_restrictions, Auto());
      }
    }
    return;
  }
  if (token == 2) {
    auto promises = std::move(get_content_settings_queries_);
    get_content_settings_queries_.clear();
    CHECK(!promises.empty());
    auto result_ptr = fetch_result<telegram_api::account_getContentSettings>(std::move(res));
    if (result_ptr.is_error()) {
      for (auto &promise : promises) {
        promise.set_error(result_ptr.error().clone());
      }
      return;
    }

    auto result = result_ptr.move_as_ok();
    do_set_ignore_sensitive_content_restrictions(result->sensitive_enabled_);
    G()->shared_config().set_option_boolean("can_ignore_sensitive_content_restrictions", result->sensitive_can_change_);

    for (auto &promise : promises) {
      promise.set_value(Unit());
    }
    return;
  }
  if (token == 1) {
    auto promises = std::move(get_app_config_queries_);
    get_app_config_queries_.clear();
    CHECK(!promises.empty());
    auto result_ptr = fetch_result<telegram_api::help_getAppConfig>(std::move(res));
    if (result_ptr.is_error()) {
      for (auto &promise : promises) {
        if (!promise) {
          promise.set_value(nullptr);
        } else {
          promise.set_error(result_ptr.error().clone());
        }
      }
      return;
    }

    auto result = result_ptr.move_as_ok();
    process_app_config(result);
    for (auto &promise : promises) {
      if (!promise) {
        promise.set_value(nullptr);
      } else {
        promise.set_value(convert_json_value_object(result));
      }
    }
    return;
  }

  CHECK(token == 8);
  CHECK(config_sent_cnt_ > 0);
  config_sent_cnt_--;
  auto r_config = fetch_result<telegram_api::help_getConfig>(std::move(res));
  if (r_config.is_error()) {
    if (!G()->close_flag()) {
      LOG(WARNING) << "Failed to get config: " << r_config.error();
      expire_time_ = Timestamp::in(60.0);  // try again in a minute
      set_timeout_in(expire_time_.in());
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

Timestamp ConfigManager::load_config_expire_time() {
  auto expires_in = to_integer<int32>(G()->td_db()->get_binlog_pmc()->get("config_expire")) - Clocks::system();

  if (expires_in < 0 || expires_in > 60 * 60 /* 1 hour */) {
    return Timestamp::now();
  } else {
    return Timestamp::in(expires_in);
  }
}

void ConfigManager::save_config_expire(Timestamp timestamp) {
  G()->td_db()->get_binlog_pmc()->set("config_expire", to_string(static_cast<int>(Clocks::system() + timestamp.in())));
}

void ConfigManager::process_config(tl_object_ptr<telegram_api::config> config) {
  bool is_from_main_dc = G()->net_query_dispatcher().main_dc_id().get_value() == config->this_dc_;

  LOG(INFO) << to_string(config);
  auto reload_in = clamp(config->expires_ - config->date_, 60, 86400);
  save_config_expire(Timestamp::in(reload_in));
  reload_in -= Random::fast(0, reload_in / 5);
  if (!is_from_main_dc) {
    reload_in = 0;
  }
  expire_time_ = Timestamp::in(reload_in);
  set_timeout_at(expire_time_.at());
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
  shared_config.set_option_integer("pinned_archived_chat_count_max", config->pinned_infolder_count_max_);
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
      shared_config.set_option_integer("base_language_pack_version", config->base_lang_pack_version_);
    } else {
      shared_config.set_option_empty("suggested_language_pack_id");
      shared_config.set_option_empty("language_pack_version");
      shared_config.set_option_empty("base_language_pack_version");
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

  auto fix_timeout_ms = [](int32 timeout_ms) {
    return clamp(timeout_ms, 1000, 86400 * 1000);
  };

  shared_config.set_option_integer("online_update_period_ms", fix_timeout_ms(config->online_update_period_ms_));

  shared_config.set_option_integer("online_cloud_timeout_ms", fix_timeout_ms(config->online_cloud_timeout_ms_));
  shared_config.set_option_integer("notification_cloud_delay_ms", fix_timeout_ms(config->notify_cloud_delay_ms_));
  shared_config.set_option_integer("notification_default_delay_ms", fix_timeout_ms(config->notify_default_delay_ms_));

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
  shared_config.set_option_empty("offline_blur_timeout_ms");
  shared_config.set_option_empty("offline_idle_timeout_ms");
  shared_config.set_option_empty("notify_cloud_delay_ms");
  shared_config.set_option_empty("notify_default_delay_ms");
  shared_config.set_option_empty("large_chat_size");

  // TODO implement online status updates
  //  shared_config.set_option_integer("offline_blur_timeout_ms", config->offline_blur_timeout_ms_);
  //  shared_config.set_option_integer("offline_idle_timeout_ms", config->offline_idle_timeout_ms_);

  //  shared_config.set_option_integer("push_chat_period_ms", config->push_chat_period_ms_);
  //  shared_config.set_option_integer("push_chat_limit", config->push_chat_limit_);

  if (is_from_main_dc) {
    get_app_config(Auto());
    if (!shared_config.have_option("can_ignore_sensitive_content_restrictions") ||
        !shared_config.have_option("ignore_sensitive_content_restrictions")) {
      get_content_settings(Auto());
    }
    if (!shared_config.have_option("archive_and_mute_new_chats_from_unknown_users")) {
      get_global_privacy_settings(Auto());
    }
  }
}

void ConfigManager::process_app_config(tl_object_ptr<telegram_api::JSONValue> &config) {
  CHECK(config != nullptr);
  LOG(INFO) << "Receive app config " << to_string(config);

  const bool archive_and_mute =
      G()->shared_config().get_option_boolean("archive_and_mute_new_chats_from_unknown_users");

  autologin_token_.clear();
  auto old_autologin_domains = std::move(autologin_domains_);
  autologin_domains_.clear();
  autologin_update_time_ = Time::now();

  vector<tl_object_ptr<telegram_api::jsonObjectValue>> new_values;
  string ignored_restriction_reasons;
  vector<string> dice_emojis;
  std::unordered_map<string, size_t> dice_emoji_index;
  std::unordered_map<string, string> dice_emoji_success_value;
  string animation_search_provider;
  string animation_search_emojis;
  vector<SuggestedAction> suggested_actions;
  bool can_archive_and_mute_new_chats_from_unknown_users = false;
  if (config->get_id() == telegram_api::jsonObject::ID) {
    for (auto &key_value : static_cast<telegram_api::jsonObject *>(config.get())->value_) {
      Slice key = key_value->key_;
      telegram_api::JSONValue *value = key_value->value_.get();
      if (key == "test" || key == "wallet_enabled" || key == "wallet_blockchain_name" || key == "wallet_config") {
        continue;
      }
      if (key == "ignore_restriction_reasons") {
        if (value->get_id() == telegram_api::jsonArray::ID) {
          auto reasons = std::move(static_cast<telegram_api::jsonArray *>(value)->value_);
          for (auto &reason : reasons) {
            CHECK(reason != nullptr);
            if (reason->get_id() == telegram_api::jsonString::ID) {
              Slice reason_name = static_cast<telegram_api::jsonString *>(reason.get())->value_;
              if (!reason_name.empty() && reason_name.find(',') == Slice::npos) {
                if (!ignored_restriction_reasons.empty()) {
                  ignored_restriction_reasons += ',';
                }
                ignored_restriction_reasons.append(reason_name.begin(), reason_name.end());
              } else {
                LOG(ERROR) << "Receive unexpected restriction reason " << reason_name;
              }
            } else {
              LOG(ERROR) << "Receive unexpected restriction reason " << to_string(reason);
            }
          }
        } else {
          LOG(ERROR) << "Receive unexpected ignore_restriction_reasons " << to_string(*value);
        }
        continue;
      }
      if (key == "emojies_send_dice") {
        if (value->get_id() == telegram_api::jsonArray::ID) {
          auto emojis = std::move(static_cast<telegram_api::jsonArray *>(value)->value_);
          for (auto &emoji : emojis) {
            CHECK(emoji != nullptr);
            if (emoji->get_id() == telegram_api::jsonString::ID) {
              Slice emoji_text = static_cast<telegram_api::jsonString *>(emoji.get())->value_;
              if (!emoji_text.empty()) {
                dice_emoji_index[emoji_text.str()] = dice_emojis.size();
                dice_emojis.push_back(emoji_text.str());
              } else {
                LOG(ERROR) << "Receive empty dice emoji";
              }
            } else {
              LOG(ERROR) << "Receive unexpected dice emoji " << to_string(emoji);
            }
          }
        } else {
          LOG(ERROR) << "Receive unexpected emojies_send_dice " << to_string(*value);
        }
        continue;
      }
      if (key == "emojies_send_dice_success") {
        if (value->get_id() == telegram_api::jsonObject::ID) {
          auto success_values = std::move(static_cast<telegram_api::jsonObject *>(value)->value_);
          for (auto &success_value : success_values) {
            CHECK(success_value != nullptr);
            if (success_value->value_->get_id() == telegram_api::jsonObject::ID) {
              int32 dice_value = -1;
              int32 frame_start = -1;
              for (auto &dice_key_value :
                   static_cast<telegram_api::jsonObject *>(success_value->value_.get())->value_) {
                if (dice_key_value->value_->get_id() != telegram_api::jsonNumber::ID) {
                  continue;
                }
                auto current_value =
                    static_cast<int32>(static_cast<telegram_api::jsonNumber *>(dice_key_value->value_.get())->value_);
                if (dice_key_value->key_ == "value") {
                  dice_value = current_value;
                }
                if (dice_key_value->key_ == "frame_start") {
                  frame_start = current_value;
                }
              }
              if (dice_value < 0 || frame_start < 0) {
                LOG(ERROR) << "Receive unexpected dice success value " << to_string(success_value);
              } else {
                dice_emoji_success_value[success_value->key_] = PSTRING() << dice_value << ':' << frame_start;
              }
            } else {
              LOG(ERROR) << "Receive unexpected dice success value " << to_string(success_value);
            }
          }
        } else {
          LOG(ERROR) << "Receive unexpected emojies_send_dice_success " << to_string(*value);
        }
        continue;
      }
      if (key == "gif_search_branding") {
        if (value->get_id() == telegram_api::jsonString::ID) {
          animation_search_provider = std::move(static_cast<telegram_api::jsonString *>(value)->value_);
        } else {
          LOG(ERROR) << "Receive unexpected gif_search_branding " << to_string(*value);
        }
        continue;
      }
      if (key == "gif_search_emojies") {
        if (value->get_id() == telegram_api::jsonArray::ID) {
          auto emojis = std::move(static_cast<telegram_api::jsonArray *>(value)->value_);
          for (auto &emoji : emojis) {
            CHECK(emoji != nullptr);
            if (emoji->get_id() == telegram_api::jsonString::ID) {
              Slice emoji_str = static_cast<telegram_api::jsonString *>(emoji.get())->value_;
              if (!emoji_str.empty() && emoji_str.find(',') == Slice::npos) {
                if (!animation_search_emojis.empty()) {
                  animation_search_emojis += ',';
                }
                animation_search_emojis.append(emoji_str.begin(), emoji_str.end());
              } else {
                LOG(ERROR) << "Receive unexpected animation search emoji " << emoji_str;
              }
            } else {
              LOG(ERROR) << "Receive unexpected animation search emoji " << to_string(emoji);
            }
          }
        } else {
          LOG(ERROR) << "Receive unexpected gif_search_emojies " << to_string(*value);
        }
        continue;
      }
      if (key == "pending_suggestions") {
        if (value->get_id() == telegram_api::jsonArray::ID) {
          auto actions = std::move(static_cast<telegram_api::jsonArray *>(value)->value_);
          for (auto &action : actions) {
            CHECK(action != nullptr);
            if (action->get_id() == telegram_api::jsonString::ID) {
              Slice action_str = static_cast<telegram_api::jsonString *>(action.get())->value_;
              SuggestedAction suggested_action(action_str);
              if (!suggested_action.is_empty()) {
                if (archive_and_mute &&
                    suggested_action == SuggestedAction{SuggestedAction::Type::EnableArchiveAndMuteNewChats}) {
                  LOG(INFO) << "Skip EnableArchiveAndMuteNewChats suggested action";
                } else {
                  suggested_actions.push_back(suggested_action);
                }
              } else {
                LOG(ERROR) << "Receive unsupported suggested action " << action_str;
              }
            } else {
              LOG(ERROR) << "Receive unexpected suggested action " << to_string(action);
            }
          }
        } else {
          LOG(ERROR) << "Receive unexpected pending_suggestions " << to_string(*value);
        }
        continue;
      }
      if (key == "autoarchive_setting_available") {
        if (value->get_id() == telegram_api::jsonBool::ID) {
          can_archive_and_mute_new_chats_from_unknown_users =
              std::move(static_cast<telegram_api::jsonBool *>(value)->value_);
        } else {
          LOG(ERROR) << "Receive unexpected autoarchive_setting_available " << to_string(*value);
        }
        continue;
      }
      if (key == "autologin_token") {
        if (value->get_id() == telegram_api::jsonString::ID) {
          autologin_token_ = url_encode(static_cast<telegram_api::jsonString *>(value)->value_);
        } else {
          LOG(ERROR) << "Receive unexpected autologin_token " << to_string(*value);
        }
        continue;
      }
      if (key == "autologin_domains") {
        if (value->get_id() == telegram_api::jsonArray::ID) {
          auto domains = std::move(static_cast<telegram_api::jsonArray *>(value)->value_);
          for (auto &domain : domains) {
            CHECK(domain != nullptr);
            if (domain->get_id() == telegram_api::jsonString::ID) {
              autologin_domains_.push_back(std::move(static_cast<telegram_api::jsonString *>(domain.get())->value_));
            } else {
              LOG(ERROR) << "Receive unexpected autologin domain " << to_string(domain);
            }
          }
        } else {
          LOG(ERROR) << "Receive unexpected autologin_domains " << to_string(*value);
        }
        continue;
      }

      new_values.push_back(std::move(key_value));
    }
  } else {
    LOG(ERROR) << "Receive wrong app config " << to_string(config);
  }
  config = make_tl_object<telegram_api::jsonObject>(std::move(new_values));

  if (autologin_domains_ != old_autologin_domains) {
    G()->td_db()->get_binlog_pmc()->set("autologin_domains", implode(autologin_domains_, '\xFF'));
  }

  ConfigShared &shared_config = G()->shared_config();

  if (ignored_restriction_reasons.empty()) {
    shared_config.set_option_empty("ignored_restriction_reasons");

    if (shared_config.get_option_boolean("ignore_sensitive_content_restrictions", true)) {
      get_content_settings(Auto());
    }
  } else {
    shared_config.set_option_string("ignored_restriction_reasons", ignored_restriction_reasons);

    if (!shared_config.get_option_boolean("can_ignore_sensitive_content_restrictions")) {
      get_content_settings(Auto());
    }
  }

  if (!dice_emojis.empty()) {
    vector<string> dice_success_values(dice_emojis.size());
    for (auto &it : dice_emoji_success_value) {
      if (dice_emoji_index.find(it.first) == dice_emoji_index.end()) {
        LOG(ERROR) << "Can't find emoji " << it.first;
        continue;
      }
      dice_success_values[dice_emoji_index[it.first]] = it.second;
    }
    shared_config.set_option_string("dice_success_values", implode(dice_success_values, ','));
    shared_config.set_option_string("dice_emojis", implode(dice_emojis, '\x01'));
  }

  if (animation_search_provider.empty()) {
    shared_config.set_option_empty("animation_search_provider");
  } else {
    shared_config.set_option_string("animation_search_provider", animation_search_provider);
  }
  if (animation_search_emojis.empty()) {
    shared_config.set_option_empty("animation_search_emojis");
  } else {
    shared_config.set_option_string("animation_search_emojis", animation_search_emojis);
  }
  if (!can_archive_and_mute_new_chats_from_unknown_users) {
    shared_config.set_option_empty("can_archive_and_mute_new_chats_from_unknown_users");
  } else {
    shared_config.set_option_boolean("can_archive_and_mute_new_chats_from_unknown_users",
                                     can_archive_and_mute_new_chats_from_unknown_users);
  }

  shared_config.set_option_empty("default_ton_blockchain_config");
  shared_config.set_option_empty("default_ton_blockchain_name");

  // do not update suggested actions while changing content settings or dismissing an action
  if (!is_set_content_settings_request_sent_ && dismiss_suggested_action_request_count_ == 0) {
    update_suggested_actions(suggested_actions_, std::move(suggested_actions));
  }
}

void ConfigManager::get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const {
  if (!suggested_actions_.empty()) {
    updates.push_back(get_update_suggested_actions_object(suggested_actions_, {}));
  }
}

}  // namespace td
