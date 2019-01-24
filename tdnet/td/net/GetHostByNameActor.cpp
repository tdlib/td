//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/GetHostByNameActor.h"

#include "td/utils/logging.h"
#include "td/utils/Time.h"
#include "td/utils/JsonBuilder.h"
#include "td/net/Wget.h"

namespace td {
namespace detail {
class GoogleDnsResolver : public Actor {
 public:
  GoogleDnsResolver(std::string host, GetHostByNameActor::Options options, td::Promise<td::IPAddress> promise)
      : host_(std::move(host)), options_(std::move(options)), promise_(std::move(promise)) {
  }

 private:
  std::string host_;
  GetHostByNameActor::Options options_;
  Promise<IPAddress> promise_;
  ActorOwn<Wget> wget_;

  void start_up() override {
    const int timeout = 10;
    const int ttl = 3;
    wget_ = create_actor<Wget>("Wget", create_result_handler(std::move(promise_)),
                               PSTRING() << "https://www.google.com/resolve?name=" << url_encode(host_)
                                         << "&type=" << (options_.prefer_ipv6 ? 28 : 1),
                               std::vector<std::pair<string, string>>({{"Host", "dns.google.com"}}), timeout, ttl,
                               options_.prefer_ipv6, SslStream::VerifyPeer::Off);
  }

  Promise<HttpQueryPtr> create_result_handler(Promise<IPAddress> promise) {
    return PromiseCreator::lambda([promise = std::move(promise)](Result<HttpQueryPtr> r_http_query) mutable {
      promise.set_result([&]() -> Result<IPAddress> {
        TRY_RESULT(http_query, std::move(r_http_query));
        LOG(ERROR) << *http_query;
        TRY_RESULT(json_value, json_decode(http_query->content_));
        if (json_value.type() != JsonValue::Type::Object) {
          return Status::Error("Failed to parse dns result: not an object");
        }
        TRY_RESULT(answer, get_json_object_field(json_value.get_object(), "Answer", JsonValue::Type::Array, false));
        if (answer.get_array().size() == 0) {
          return Status::Error("Failed to parse dns result: Answer is an empty array");
        }
        if (answer.get_array()[0].type() != JsonValue::Type::Object) {
          return Status::Error("Failed to parse dns result: Answer[0] is not an object");
        }
        auto &answer_0 = answer.get_array()[0].get_object();
        TRY_RESULT(ip_str, get_json_object_string_field(answer_0, "data", false));
        IPAddress ip;
        TRY_STATUS(ip.init_host_port(ip_str, 0));
        return ip;
      }());
    });
  }
};
class NativeDnsResolver : public Actor {
 public:
  NativeDnsResolver(std::string host, GetHostByNameActor::Options options, td::Promise<td::IPAddress> promise)
      : host_(std::move(host)), options_(std::move(options)), promise_(std::move(promise)) {
  }

 private:
  std::string host_;
  GetHostByNameActor::Options options_;
  Promise<IPAddress> promise_;

  void start_up() override {
    IPAddress ip;
    auto begin_time = td::Time::now();
    auto status = ip.init_host_port(host_, 0, options_.prefer_ipv6);
    auto end_time = td::Time::now();
    LOG(WARNING) << "Init host = " << host_ << " in " << end_time - begin_time << " seconds to " << ip;
    if (status.is_error()) {
      promise_.set_error(std::move(status));
      return;
    }
    promise_.set_value(std::move(ip));
    stop();
  }
};
class DnsResolver : public Actor {
 public:
  DnsResolver(std::string host, GetHostByNameActor::Options options, td::Promise<td::IPAddress> promise)
      : host_(std::move(host)), options_(std::move(options)), promise_(std::move(promise)) {
  }

 private:
  std::string host_;
  GetHostByNameActor::Options options_;
  Promise<IPAddress> promise_;
  ActorOwn<> query_;
  size_t pos_ = 0;
  GetHostByNameActor::Options::Type types[2] = {GetHostByNameActor::Options::Google,
                                                GetHostByNameActor::Options::Native};

  void loop() override {
    if (!query_.empty()) {
      return;
    }
    if (pos_ == 2) {
      promise_.set_error(Status::Error("Failed to resolve ip"));
      return stop();
    }
    options_.type = types[pos_];
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

ActorOwn<> GetHostByNameActor::resolve(std::string host, Options options, Promise<IPAddress> promise) {
  switch (options.type) {
    case Options::Native:
      return ActorOwn<>(create_actor_on_scheduler<detail::NativeDnsResolver>(
          "NativeDnsResolver", options.scheduler_id, std::move(host), options, std::move(promise)));
    case Options::Google:
      return ActorOwn<>(create_actor_on_scheduler<detail::GoogleDnsResolver>(
          "GoogleDnsResolver", options.scheduler_id, std::move(host), options, std::move(promise)));
    case Options::All:
      return ActorOwn<>(create_actor_on_scheduler<detail::DnsResolver>("DnsResolver", options.scheduler_id,
                                                                       std::move(host), options, std::move(promise)));
  }
}

GetHostByNameActor::GetHostByNameActor(int32 ok_timeout, int32 error_timeout)
    : ok_timeout_(ok_timeout), error_timeout_(error_timeout) {
}

void GetHostByNameActor::on_result(std::string host, bool prefer_ipv6, Result<IPAddress> res) {
  auto &value = cache_[prefer_ipv6].emplace(host, Value{{}, 0}).first->second;

  auto promises = std::move(value.promises);
  auto end_time = td::Time::now();
  if (res.is_ok()) {
    value = Value{res.move_as_ok(), end_time + ok_timeout_};
  } else {
    value = Value{res.move_as_error(), end_time + error_timeout_};
  }
  for (auto &promise : promises) {
    promise.second.set_result(value.get_ip_port(promise.first));
  }
}

void GetHostByNameActor::run(string host, int port, bool prefer_ipv6, Promise<IPAddress> promise) {
  auto &value = cache_[prefer_ipv6].emplace(host, Value{{}, 0}).first->second;
  auto begin_time = td::Time::now();
  if (value.expire_at > begin_time) {
    return promise.set_result(value.get_ip_port(port));
  }

  value.promises.emplace_back(port, std::move(promise));
  if (value.query.empty()) {
    Options options;
    options.type = Options::Type::All;
    options.prefer_ipv6 = prefer_ipv6;
    value.query =
        resolve(host, options,
                PromiseCreator::lambda([actor_id = actor_id(this), host, prefer_ipv6](Result<IPAddress> res) mutable {
                  send_closure(actor_id, &GetHostByNameActor::on_result, std::move(host), prefer_ipv6, std::move(res));
                }));
  }
}

}  // namespace td
