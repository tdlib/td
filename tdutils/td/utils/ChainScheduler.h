//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/Container.h"
#include "td/utils/List.h"
#include "td/utils/optional.h"
#include "td/utils/Span.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/VectorQueue.h"

#include <functional>
#include <map>

namespace td {

struct ChainSchedulerTaskWithParents {
  uint64 task_id{};
  vector<uint64> parents;
};

template <class ExtraT = Unit>
class ChainScheduler {
 public:
  using TaskId = uint64;
  using ChainId = uint64;
  TaskId create_task(Span<ChainId> chains, ExtraT extra = {});
  ExtraT *get_task_extra(TaskId task_id);

  optional<ChainSchedulerTaskWithParents> start_next_task();
  void finish_task(TaskId task_id);
  void reset_task(TaskId task_id);
  template <class ExtraTT>
  friend StringBuilder &operator<<(StringBuilder &sb, ChainScheduler<ExtraTT> &scheduler);

  template <class F>
  void for_each(F &&f) {
    tasks_.for_each([&f](auto, Task &task) { f(task.extra); });
  }

 private:
  struct ChainNode : ListNode {
    TaskId task_id{};
  };

  class Chain {
   public:
    void add_task(ChainNode *node) {
      head_.put_back(node);
    }

    optional<TaskId> get_first() {
      if (head_.empty()) {
        return {};
      }
      return static_cast<ChainNode &>(*head_.get_next()).task_id;
    }

    optional<TaskId> get_child(ChainNode *chain_node) {
      if (chain_node->get_next() == head_.end()) {
        return {};
      }
      return static_cast<ChainNode &>(*chain_node->get_next()).task_id;
    }
    optional<TaskId> get_parent(ChainNode *chain_node) {
      if (chain_node->get_prev() == head_.end()) {
        return {};
      }
      return static_cast<ChainNode &>(*chain_node->get_prev()).task_id;
    }

    void finish_task(ChainNode *node) {
      node->remove();
    }

    bool empty() const {
      return head_.empty();
    }

    void foreach(std::function<void(TaskId)> f) const {
      for (auto it = head_.begin(); it != head_.end(); it = it->get_next()) {
        f(static_cast<const ChainNode &>(*it).task_id);
      }
    }

   private:
    ListNode head_;
  };
  struct ChainInfo {
    Chain chain;
    uint32 active_tasks{};
  };
  struct TaskChainInfo {
    ChainNode chain_node;
    ChainId chain_id{};
    ChainInfo *chain_info{};
    bool waiting_for_parent{};
  };
  struct Task {
    enum class State { Pending, Active } state{State::Pending};
    vector<TaskChainInfo> chains;
    ExtraT extra;
  };
  std::map<ChainId, ChainInfo> chains_;
  std::map<ChainId, TaskId> limited_tasks_;
  Container<Task> tasks_;
  VectorQueue<TaskId> pending_tasks_;

  void on_parent_is_ready(TaskId task_id, ChainId chain_id) {
    auto *task = tasks_.get(task_id);
    CHECK(task);
    for (TaskChainInfo &task_chain_info : task->chains) {
      if (task_chain_info.chain_id == chain_id) {
        task_chain_info.waiting_for_parent = false;
      }
    }

    try_start_task(task_id, task);
  }

  void try_start_task(TaskId task_id, Task *task) {
    if (task->state != Task::State::Pending) {
      return;
    }
    for (TaskChainInfo &task_chain_info : task->chains) {
      if (task_chain_info.waiting_for_parent) {
        return;
      }
      ChainInfo &chain_info = chains_[task_chain_info.chain_id];
      if (chain_info.active_tasks >= 10) {
        limited_tasks_[task_chain_info.chain_id] = task_id;
        return;
      }
    }

    do_start_task(task_id, task);
  }

  void do_start_task(TaskId task_id, Task *task) {
    for (TaskChainInfo &task_chain_info : task->chains) {
      ChainInfo &chain_info = chains_[task_chain_info.chain_id];
      chain_info.active_tasks++;
    }
    task->state = Task::State::Active;

    pending_tasks_.push(task_id);
    notify_children(task);
  }

  void notify_children(Task *task) {
    for (TaskChainInfo &task_chain_info : task->chains) {
      ChainInfo &chain_info = chains_[task_chain_info.chain_id];
      auto o_child = chain_info.chain.get_child(&task_chain_info.chain_node);
      if (o_child) {
        on_parent_is_ready(o_child.value(), task_chain_info.chain_id);
      }
    }
  }

  void inactivate_task(TaskId task_id, Task *task) {
    CHECK(task->state == Task::State::Active);
    task->state = Task::State::Pending;
    for (TaskChainInfo &task_chain_info : task->chains) {
      ChainInfo &chain_info = chains_[task_chain_info.chain_id];
      chain_info.active_tasks--;

      auto it = limited_tasks_.find(task_chain_info.chain_id);
      if (it != limited_tasks_.end()) {
        auto limited_task_id = it->second;
        limited_tasks_.erase(it);
        if (limited_task_id != task_id) {
          try_start_task(limited_task_id, tasks_.get(limited_task_id));
        }
      }
      auto o_first = chain_info.chain.get_first();
      if (o_first) {
        auto first_task_id = o_first.unwrap();
        if (first_task_id != task_id) {
          try_start_task(first_task_id, tasks_.get(first_task_id));
        }
      }
    }
  }

  void finish_chain_task(TaskChainInfo &task_chain_info) {
    auto &chain = task_chain_info.chain_info->chain;
    chain.finish_task(&task_chain_info.chain_node);
    if (chain.empty()) {
      chains_.erase(task_chain_info.chain_id);
    }
  }
};

template <class ExtraT>
typename ChainScheduler<ExtraT>::TaskId ChainScheduler<ExtraT>::create_task(Span<ChainScheduler::ChainId> chains,
                                                                            ExtraT extra) {
  auto task_id = tasks_.create();
  Task &task = *tasks_.get(task_id);
  task.extra = std::move(extra);
  task.chains = transform(chains, [&](auto chain_id) {
    TaskChainInfo task_chain_info;
    ChainInfo &chain_info = chains_[chain_id];
    task_chain_info.chain_id = chain_id;
    task_chain_info.chain_info = &chain_info;
    task_chain_info.chain_node.task_id = task_id;
    return task_chain_info;
  });

  for (TaskChainInfo &task_chain_info : task.chains) {
    auto &chain = task_chain_info.chain_info->chain;
    chain.add_task(&task_chain_info.chain_node);
    task_chain_info.waiting_for_parent = static_cast<bool>(chain.get_parent(&task_chain_info.chain_node));
  }

  try_start_task(task_id, &task);
  return task_id;
}

template <class ExtraT>
ExtraT *ChainScheduler<ExtraT>::get_task_extra(ChainScheduler::TaskId task_id) {  // may return nullptr
  auto *task = tasks_.get(task_id);
  if (!task) {
    return nullptr;
  }
  return &task->extra;
}

template <class ExtraT>
optional<ChainSchedulerTaskWithParents> ChainScheduler<ExtraT>::start_next_task() {
  if (pending_tasks_.empty()) {
    return {};
  }
  auto task_id = pending_tasks_.pop();
  ChainSchedulerTaskWithParents res;
  res.task_id = task_id;
  auto *task = tasks_.get(task_id);
  CHECK(task);
  for (TaskChainInfo &task_chain_info : task->chains) {
    Chain &chain = task_chain_info.chain_info->chain;
    auto o_parent = chain.get_parent(&task_chain_info.chain_node);
    if (o_parent) {
      res.parents.push_back(o_parent.value());
    }
  }
  return res;
}

template <class ExtraT>
void ChainScheduler<ExtraT>::finish_task(ChainScheduler::TaskId task_id) {
  auto *task = tasks_.get(task_id);
  CHECK(task);

  inactivate_task(task_id, task);
  notify_children(task);

  for (TaskChainInfo &task_chain_info : task->chains) {
    finish_chain_task(task_chain_info);
  }
  tasks_.erase(task_id);
}

template <class ExtraT>
void ChainScheduler<ExtraT>::reset_task(ChainScheduler::TaskId task_id) {
  auto *task = tasks_.get(task_id);
  CHECK(task);
  inactivate_task(task_id, task);

  for (TaskChainInfo &task_chain_info : task->chains) {
    ChainInfo &chain_info = chains_[task_chain_info.chain_id];
    task_chain_info.waiting_for_parent = static_cast<bool>(chain_info.chain.get_parent(&task_chain_info.chain_node));
  }

  try_start_task(task_id, task);
}

template <class ExtraT>
StringBuilder &operator<<(StringBuilder &sb, ChainScheduler<ExtraT> &scheduler) {
  // 1 print chains
  sb << "\n";
  for (auto &it : scheduler.chains_) {
    sb << "ChainId{" << it.first << "} ";
    sb << " active_cnt=" << it.second.active_tasks;
    sb << " : ";
    it.second.chain.foreach([&](auto task_id) { sb << *scheduler.get_task_extra(task_id); });
    sb << "\n";
  }
  scheduler.tasks_.for_each([&](auto id, auto &task) {
    sb << "Task: " << task.extra;
    sb << " state =" << static_cast<int>(task.state);
    for (auto &task_chain_info : task.chains) {
      if (task_chain_info.waiting_for_parent) {
        sb << " wait "
           << *scheduler.get_task_extra(
                  task_chain_info.chain_info->chain.get_parent(&task_chain_info.chain_node).value());
      }
    }
    sb << "\n";
  });
  return sb;
}

}  // namespace td
