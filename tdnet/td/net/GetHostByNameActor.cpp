//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/net/GetHostByNameActor.h"

#include "td/utils/logging.h"
#include "td/utils/Time.h"

namespace td {

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
