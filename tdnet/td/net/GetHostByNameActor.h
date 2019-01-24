//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
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

class GetHostByNameActor final : public Actor {
 public:
  enum class ResolveType { Native, Google, All };
  struct Options {
    Options();
    ResolveType type{ResolveType::Native};
    int scheduler_id{-1};
    int32 ok_timeout{CACHE_TIME};
    int32 error_timeout{ERROR_CACHE_TIME};
  };
  explicit GetHostByNameActor(Options options = {});
  void run(std::string host, int port, bool prefer_ipv6, Promise<IPAddress> promise);

  struct ResolveOptions {
    ResolveType type{ResolveType::Native};
    bool prefer_ipv6{false};
    int scheduler_id{-1};
  };
  static TD_WARN_UNUSED_RESULT ActorOwn<> resolve(std::string host, ResolveOptions options, Promise<IPAddress> promise);

 private:
  struct Value {
    Result<IPAddress> ip;
    double expire_at;

    ActorOwn<> query;
    std::vector<std::pair<int, Promise<IPAddress>>> promises;

    Value(Result<IPAddress> ip, double expire_at) : ip(std::move(ip)), expire_at(expire_at) {
    }

    Result<IPAddress> get_ip_port(int port) {
      auto res = ip.clone();
      if (res.is_ok()) {
        res.ok_ref().set_port(port);
      }
      return res;
    }
  };
  std::unordered_map<string, Value> cache_[2];
  static constexpr int32 CACHE_TIME = 60 * 29;       // 29 minutes
  static constexpr int32 ERROR_CACHE_TIME = 60 * 5;  // 5 minutes

  Options options_;

  void on_result(std::string host, bool prefer_ipv6, Result<IPAddress> res);
};

}  // namespace td
