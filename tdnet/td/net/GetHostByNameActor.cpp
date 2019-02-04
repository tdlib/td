//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/GetHostByNameActor.h"

#include "td/net/SslStream.h"
#include "td/net/Wget.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Time.h"

namespace td {
namespace detail {

class GoogleDnsResolver : public Actor {
 public:
  GoogleDnsResolver(std::string host, GetHostByNameActor::ResolveOptions options, Promise<IPAddress> promise)
      : host_(std::move(host)), options_(std::move(options)), promise_(std::move(promise)) {
  }

 private:
  std::string host_;
  GetHostByNameActor::ResolveOptions options_;
  Promise<IPAddress> promise_;
  ActorOwn<Wget> wget_;
  double begin_time_ = 0;

  void start_up() override {
    const int timeout = 10;
    const int ttl = 3;
    begin_time_ = Time::now();
    auto wget_promise = PromiseCreator::lambda([actor_id = actor_id(this)](Result<HttpQueryPtr> r_http_query) {
      send_closure(actor_id, &GoogleDnsResolver::on_result, std::move(r_http_query));
    });
    wget_ = create_actor<Wget>("GoogleDnsResolver", std::move(wget_promise),
                               PSTRING() << "https://www.google.com/resolve?name=" << url_encode(host_)
                                         << "&type=" << (options_.prefer_ipv6 ? 28 : 1),
                               std::vector<std::pair<string, string>>({{"Host", "dns.google.com"}}), timeout, ttl,
                               options_.prefer_ipv6, SslStream::VerifyPeer::Off);
  }

  static Result<IPAddress> get_ip_address(Result<HttpQueryPtr> r_http_query) {
    TRY_RESULT(http_query, std::move(r_http_query));
    TRY_RESULT(json_value, json_decode(http_query->content_));
    if (json_value.type() != JsonValue::Type::Object) {
      return Status::Error("Failed to parse DNS result: not an object");
    }
    TRY_RESULT(answer, get_json_object_field(json_value.get_object(), "Answer", JsonValue::Type::Array, false));
    auto &array = answer.get_array();
    if (array.size() == 0) {
      return Status::Error("Failed to parse DNS result: Answer is an empty array");
    }
    if (array[0].type() != JsonValue::Type::Object) {
      return Status::Error("Failed to parse DNS result: Answer[0] is not an object");
    }
    auto &answer_0 = array[0].get_object();
    TRY_RESULT(ip_str, get_json_object_string_field(answer_0, "data", false));
    IPAddress ip;
    TRY_STATUS(ip.init_host_port(ip_str, 0));
    return ip;
  }

  void on_result(Result<HttpQueryPtr> r_http_query) {
    auto end_time = Time::now();
    auto result = get_ip_address(std::move(r_http_query));
    LOG(WARNING) << "Init host = " << host_ << " in " << end_time - begin_time_ << " seconds to "
                 << (result.is_ok() ? (PSLICE() << result.ok()) : CSlice("[invalid]"));
    promise_.set_result(std::move(result));
    stop();
  }
};

class NativeDnsResolver : public Actor {
 public:
  NativeDnsResolver(std::string host, GetHostByNameActor::ResolveOptions options, Promise<IPAddress> promise)
      : host_(std::move(host)), options_(std::move(options)), promise_(std::move(promise)) {
  }

 private:
  std::string host_;
  GetHostByNameActor::ResolveOptions options_;
  Promise<IPAddress> promise_;

  void start_up() override {
    IPAddress ip;
    auto begin_time = Time::now();
    auto status = ip.init_host_port(host_, 0, options_.prefer_ipv6);
    auto end_time = Time::now();
    LOG(WARNING) << "Init host = " << host_ << " in " << end_time - begin_time << " seconds to " << ip;
    if (status.is_error()) {
      promise_.set_error(std::move(status));
    } else {
      promise_.set_value(std::move(ip));
    }
    stop();
  }
};

class DnsResolver : public Actor {
 public:
  DnsResolver(std::string host, GetHostByNameActor::ResolveOptions options, Promise<IPAddress> promise)
      : host_(std::move(host)), options_(std::move(options)), promise_(std::move(promise)) {
  }

 private:
  std::string host_;
  GetHostByNameActor::ResolveOptions options_;
  Promise<IPAddress> promise_;
  ActorOwn<> query_;
  size_t pos_ = 0;
  GetHostByNameActor::ResolveType types[2] = {GetHostByNameActor::ResolveType::Google,
                                              GetHostByNameActor::ResolveType::Native};

  void loop() override {
    if (!query_.empty()) {
      return;
    }
    if (pos_ == 2) {
      promise_.set_error(Status::Error("Failed to resolve IP address"));
      return stop();
    }
    options_.type = types[pos_];
    pos_++;
    query_ = GetHostByNameActor::resolve(host_, options_,
                                         PromiseCreator::lambda([actor_id = actor_id(this)](Result<IPAddress> res) {
                                           send_closure(actor_id, &DnsResolver::on_result, std::move(res));
                                         }));
  }

  void on_result(Result<IPAddress> res) {
    query_.reset();
    if (res.is_ok() || pos_ == 2) {
      promise_.set_result(std::move(res));
      return stop();
    }
    loop();
  }
};

}  // namespace detail

ActorOwn<> GetHostByNameActor::resolve(std::string host, ResolveOptions options, Promise<IPAddress> promise) {
  switch (options.type) {
    case ResolveType::Native:
      return ActorOwn<>(create_actor_on_scheduler<detail::NativeDnsResolver>(
          "NativeDnsResolver", options.scheduler_id, std::move(host), options, std::move(promise)));
    case ResolveType::Google:
      return ActorOwn<>(create_actor_on_scheduler<detail::GoogleDnsResolver>(
          "GoogleDnsResolver", options.scheduler_id, std::move(host), options, std::move(promise)));
    case ResolveType::All:
      return ActorOwn<>(create_actor_on_scheduler<detail::DnsResolver>("DnsResolver", options.scheduler_id,
                                                                       std::move(host), options, std::move(promise)));
    default:
      UNREACHABLE();
      return ActorOwn<>();
  }
}

GetHostByNameActor::GetHostByNameActor(Options options) : options_(std::move(options)) {
}

void GetHostByNameActor::run(string host, int port, bool prefer_ipv6, Promise<IPAddress> promise) {
  auto &value = cache_[prefer_ipv6].emplace(host, Value{{}, 0}).first->second;
  auto begin_time = Time::now();
  if (value.expire_at > begin_time) {
    return promise.set_result(value.get_ip_port(port));
  }

  auto &query = active_queries_[prefer_ipv6][host];
  query.promises.emplace_back(port, std::move(promise));
  if (query.query.empty()) {
    CHECK(query.promises.size() == 1);

    ResolveOptions options;
    options.type = options_.type;
    options.scheduler_id = options_.scheduler_id;
    options.prefer_ipv6 = prefer_ipv6;
    query.query =
        resolve(host, options,
                PromiseCreator::lambda([actor_id = actor_id(this), host, prefer_ipv6](Result<IPAddress> res) mutable {
                  send_closure(actor_id, &GetHostByNameActor::on_result, std::move(host), prefer_ipv6, std::move(res));
                }));
  }
}

void GetHostByNameActor::on_result(std::string host, bool prefer_ipv6, Result<IPAddress> res) {
  auto value_it = cache_[prefer_ipv6].find(host);
  CHECK(value_it != cache_[prefer_ipv6].end());
  auto &value = value_it->second;
  auto query_it = active_queries_[prefer_ipv6].find(host);
  CHECK(query_it != active_queries_[prefer_ipv6].end());
  auto &query = query_it->second;
  CHECK(!query.promises.empty());
  CHECK(!query.query.empty());

  auto promises = std::move(query.promises);
  auto end_time = Time::now() + (res.is_ok() ? options_.ok_timeout : options_.error_timeout);
  value = Value{std::move(res), end_time};
  active_queries_[prefer_ipv6].erase(query_it);

  for (auto &promise : promises) {
    promise.second.set_result(value.get_ip_port(promise.first));
  }
}

}  // namespace td
