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
  enum class ResolveType { Native, Google };

  struct Options {
    static constexpr int32 DEFAULT_CACHE_TIME = 60 * 29;       // 29 minutes
    static constexpr int32 DEFAULT_ERROR_CACHE_TIME = 60 * 5;  // 5 minutes

    vector<ResolveType> types{ResolveType::Native};
    int32 scheduler_id{-1};
    int32 ok_timeout{DEFAULT_CACHE_TIME};
    int32 error_timeout{DEFAULT_ERROR_CACHE_TIME};
  };

  explicit GetHostByNameActor(Options options);

  void run(std::string host, int port, bool prefer_ipv6, Promise<IPAddress> promise);

 private:
  void on_query_result(std::string host, bool prefer_ipv6, Result<IPAddress> res);

  struct Value {
    Result<IPAddress> ip;
    double expire_at;

    Value(Result<IPAddress> ip, double expire_at) : ip(std::move(ip)), expire_at(expire_at) {
    }

    Result<IPAddress> get_ip_port(int port) const {
      auto res = ip.clone();
      if (res.is_ok()) {
        res.ok_ref().set_port(port);
      }
      return res;
    }
  };
  std::unordered_map<string, Value> cache_[2];

  struct Query {
    ActorOwn<> query;
    size_t pos = 0;
    std::vector<std::pair<int, Promise<IPAddress>>> promises;
  };
  std::unordered_map<string, Query> active_queries_[2];

  Options options_;

  void run_query(std::string host, bool prefer_ipv6, Query &query);
};

}  // namespace td
