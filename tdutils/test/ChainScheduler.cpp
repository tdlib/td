//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/algorithm.h"
#include "td/utils/ChainScheduler.h"
#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Span.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"

#include <memory>
#include <numeric>

TEST(ChainScheduler, Basic) {
  td::ChainScheduler<int> scheduler;
  using ChainId = td::ChainScheduler<int>::ChainId;
  using TaskId = td::ChainScheduler<int>::TaskId;
  for (int i = 0; i < 100; i++) {
    scheduler.create_task({ChainId{1}}, i);
  }
  int j = 0;
  while (j != 100) {
    td::vector<TaskId> tasks;
    while (true) {
      auto o_task_id = scheduler.start_next_task();
      if (!o_task_id) {
        break;
      }
      auto task_id = o_task_id.value().task_id;
      auto extra = *scheduler.get_task_extra(task_id);
      auto parents =
          td::transform(o_task_id.value().parents, [&](auto parent) { return *scheduler.get_task_extra(parent); });
      LOG(INFO) << "Start " << extra << parents;
      CHECK(extra == j);
      j++;
      tasks.push_back(task_id);
    }
    for (auto &task_id : tasks) {
      auto extra = *scheduler.get_task_extra(task_id);
      LOG(INFO) << "Finish " << extra;
      scheduler.finish_task(task_id);
    }
  }
}

struct Query;
using QueryPtr = std::shared_ptr<Query>;
using ChainId = td::ChainScheduler<QueryPtr>::ChainId;
using TaskId = td::ChainScheduler<QueryPtr>::TaskId;
struct Query {
  int id{};
  TaskId task_id{};
  bool is_ok{};
  friend td::StringBuilder &operator<<(td::StringBuilder &sb, const Query &q) {
    return sb << "Q{" << q.id << "}";
  }
};

TEST(ChainScheduler, Stress) {
  td::Random::Xorshift128plus rnd(123);
  int max_query_id = 1000;
  int MAX_INFLIGHT_QUERIES = 20;
  int ChainsN = 4;

  struct QueryWithParents {
    QueryPtr id;
    td::vector<QueryPtr> parents;
  };
  td::vector<QueryWithParents> active_queries;

  td::ChainScheduler<QueryPtr> scheduler;
  td::vector<td::vector<QueryPtr>> chains(ChainsN);
  int inflight_queries{};
  int current_query_id{};
  bool done = false;

  auto schedule_new_query = [&] {
    if (current_query_id > max_query_id) {
      if (inflight_queries == 0) {
        done = true;
      }
      return;
    }
    if (inflight_queries >= MAX_INFLIGHT_QUERIES) {
      return;
    }
    auto query_id = current_query_id++;
    auto query = std::make_shared<Query>();
    query->id = query_id;
    int chain_n = rnd.fast(1, ChainsN);
    td::vector<ChainId> chain_ids(ChainsN);
    std::iota(chain_ids.begin(), chain_ids.end(), 0);
    td::random_shuffle(td::as_mutable_span(chain_ids), rnd);
    chain_ids.resize(chain_n);
    for (auto chain_id : chain_ids) {
      chains[td::narrow_cast<size_t>(chain_id)].push_back(query);
    }
    auto task_id = scheduler.create_task(chain_ids, query);
    query->task_id = task_id;
    inflight_queries++;
  };

  auto check_parents_ok = [&](const QueryWithParents &query_with_parents) -> bool {
    return td::all_of(query_with_parents.parents, [](auto &parent) { return parent->is_ok; });
  };

  auto to_query_ptr = [&](TaskId task_id) {
    return *scheduler.get_task_extra(task_id);
  };
  auto flush_pending_queries = [&] {
    while (true) {
      auto o_task_with_parents = scheduler.start_next_task();
      if (!o_task_with_parents) {
        break;
      }
      auto task_with_parents = o_task_with_parents.unwrap();
      QueryWithParents query_with_parents;
      query_with_parents.id = to_query_ptr(task_with_parents.task_id);
      query_with_parents.parents = td::transform(task_with_parents.parents, to_query_ptr);
      active_queries.push_back(query_with_parents);
    }
  };
  auto execute_one_query = [&] {
    if (active_queries.empty()) {
      return;
    }
    auto it = active_queries.begin() + rnd.fast(0, static_cast<int>(active_queries.size()) - 1);
    auto query_with_parents = *it;
    active_queries.erase(it);

    auto query = query_with_parents.id;
    if (rnd.fast(0, 20) == 0) {
      scheduler.finish_task(query->task_id);
      inflight_queries--;
      LOG(INFO) << "Fail " << query->id;
    } else if (check_parents_ok(query_with_parents)) {
      query->is_ok = true;
      scheduler.finish_task(query->task_id);
      inflight_queries--;
      LOG(INFO) << "OK " << query->id;
    } else {
      scheduler.reset_task(query->task_id);
    }
  };

  td::RandomSteps steps({{schedule_new_query, 100}, {execute_one_query, 100}});
  while (!done) {
    steps.step(rnd);
    flush_pending_queries();
    // LOG(INFO) << scheduler;
  }
  for (auto &chain : chains) {
    int prev_ok = -1;
    int failed_cnt = 0;
    int ok_cnt = 0;
    for (auto &q : chain) {
      if (q->is_ok) {
        CHECK(prev_ok < q->id);
        prev_ok = q->id;
        ok_cnt++;
      } else {
        failed_cnt++;
      }
    }
    LOG(INFO) << "Chain ok " << ok_cnt << " failed " << failed_cnt;
  }
}
