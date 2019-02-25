//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/QueryCombiner.h"

namespace td {

void QueryCombiner::add_query(int64 query_id, Promise<Promise<Unit>> &&send_query, Promise<Unit> &&promise) {
  auto &query = queries_[query_id];
  query.promises.push_back(std::move(promise));
  if (query.promises.size() != 1) {
    // query has already been sent, just wait for the result
    return;
  }

  send_query.set_value(PromiseCreator::lambda([actor_id = actor_id(this), query_id](Result<Unit> &&result) {
    send_closure(actor_id, &QueryCombiner::on_get_query_result, query_id, std::move(result));
  }));
}

void QueryCombiner::on_get_query_result(int64 query_id, Result<Unit> &&result) {
  auto it = queries_.find(query_id);
  CHECK(it != queries_.end());
  CHECK(!it->second.promises.empty());
  auto promises = std::move(it->second.promises);
  queries_.erase(it);

  for (auto &promise : promises) {
    if (result.is_ok()) {
      promise.set_value(Unit());
    } else {
      promise.set_error(result.error().clone());
    }
  }
}

}  // namespace td
