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
  GoogleDnsResolver(std::string host, int port, bool prefer_ipv6, td::Promise<td::IPAddress> promise) {
    const int timeout = 10;
    const int ttl = 3;
    wget_ = create_actor<Wget>(
        "Wget", create_result_handler(std::move(promise), port),
        PSTRING() << "https://www.google.com/resolve?name=" << url_encode(host) << "&type=" << (prefer_ipv6 ? 28 : 1),
        std::vector<std::pair<string, string>>({{"Host", "dns.google.com"}}), timeout, ttl, prefer_ipv6,
        SslStream::VerifyPeer::Off);
  }

 private:
  ActorOwn<Wget> wget_;

  Promise<HttpQueryPtr> create_result_handler(Promise<IPAddress> promise, int port) {
    return PromiseCreator::lambda([promise = std::move(promise), port](Result<HttpQueryPtr> r_http_query) mutable {
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
        TRY_STATUS(ip.init_host_port(ip_str, port));
        return ip;
      }());
    });
  }
};
}  // namespace detail

ActorOwn<> DnsOverHttps::resolve(std::string host, int port, bool prefer_ipv6, td::Promise<td::IPAddress> promise) {
  return ActorOwn<>(create_actor<detail::GoogleDnsResolver>("GoogleDnsResolver", std::move(host), port, prefer_ipv6,
                                                            std::move(promise)));
}

GetHostByNameActor::GetHostByNameActor(int32 ok_timeout, int32 error_timeout)
    : ok_timeout_(ok_timeout), error_timeout_(error_timeout) {
}

void GetHostByNameActor::run(std::string host, int port, bool prefer_ipv6, td::Promise<td::IPAddress> promise) {
  auto r_ip = load_ip(std::move(host), port, prefer_ipv6);
  promise.set_result(std::move(r_ip));
}

Result<td::IPAddress> GetHostByNameActor::load_ip(string host, int port, bool prefer_ipv6) {
  auto &value = cache_[prefer_ipv6].emplace(host, Value{{}, 0}).first->second;
  auto begin_time = td::Time::now();
  if (value.expire_at > begin_time) {
    auto ip = value.ip.clone();
    if (ip.is_ok()) {
      ip.ok_ref().set_port(port);
      CHECK(ip.ok().get_port() == port);
    }
    return ip;
  }

  td::IPAddress ip;
  auto status = ip.init_host_port(host, port, prefer_ipv6);
  auto end_time = td::Time::now();
  LOG(WARNING) << "Init host = " << host << ", port = " << port << " in " << end_time - begin_time << " seconds to "
               << ip;

  if (status.is_ok()) {
    value = Value{ip, end_time + ok_timeout_};
    return ip;
  } else {
    value = Value{status.clone(), end_time + error_timeout_};
    return std::move(status);
  }
}

}  // namespace td
