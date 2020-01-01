//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <queue>
#include <unordered_map>

namespace td {

// combines identical queries into one request
class QueryCombiner : public Actor {
 public:
  explicit QueryCombiner(Slice name, double min_delay = 0) : min_delay_(min_delay) {
    register_actor(name, this).release();
  }

  void add_query(int64 query_id, Promise<Promise<Unit>> &&send_query, Promise<Unit> &&promise);

 private:
  struct QueryInfo {
    vector<Promise<Unit>> promises;
    bool is_sent = false;
    Promise<Promise<Unit>> send_query;
  };

  int32 query_count_ = 0;

  double next_query_time_ = 0.0;
  double min_delay_;

  std::queue<int64> delayed_queries_;

  std::unordered_map<int64, QueryInfo> queries_;

  void do_send_query(int64 query_id, QueryInfo &query);

  void on_get_query_result(int64 query_id, Result<Unit> &&result);

  void loop() override;
};

}  // namespace td
