//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

#include <functional>
#include <unordered_map>

namespace td {

// combines identical queries into one request
class QueryCombiner : public Actor {
 public:
  explicit QueryCombiner(Slice name) {
    register_actor(name, this).release();
  }

  void add_query(int64 query_id, Promise<Promise<Unit>> &&send_query, Promise<Unit> &&promise);

 private:
  struct QueryInfo {
    vector<Promise<Unit>> promises;
    bool is_sent = false;
  };

  std::unordered_map<int64, QueryInfo> queries_;

  void on_get_query_result(int64 query_id, Result<Unit> &&result);
};

}  // namespace td
