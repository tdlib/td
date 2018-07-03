//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/port/IPAddress.h"
#include "td/utils/Status.h"

#include <unordered_map>

namespace td {
class GetHostByNameActor final : public td::Actor {
 public:
  explicit GetHostByNameActor(int32 ok_timeout = CACHE_TIME, int32 error_timeout = ERROR_CACHE_TIME);
  void run(std::string host, int port, bool prefer_ipv6, td::Promise<td::IPAddress> promise);

 private:
  struct Value {
    Result<td::IPAddress> ip;
    double expire_at;

    Value(Result<td::IPAddress> ip, double expire_at) : ip(std::move(ip)), expire_at(expire_at) {
    }
  };
  std::unordered_map<string, Value> cache_[2];
  static constexpr int32 CACHE_TIME = 60 * 29;       // 29 minutes
  static constexpr int32 ERROR_CACHE_TIME = 60 * 5;  // 5 minutes

  int32 ok_timeout_;
  int32 error_timeout_;

  Result<td::IPAddress> load_ip(string host, int port, bool prefer_ipv6) TD_WARN_UNUSED_RESULT;
};
}  // namespace td
