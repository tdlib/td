//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/utils/common.h"
#include "td/utils/List.h"
#include "td/utils/port/thread.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"
#include "td/utils/TsList.h"

#include <atomic>
#include <set>
#include <utility>

struct Data {
  td::uint64 value{0};
  bool in_list{false};

  Data() = default;
  Data(td::uint64 value, bool in_list) : value(value), in_list(in_list) {
  }
  Data(const Data &from) = delete;
  Data &operator=(const Data &from) = delete;
  Data(Data &&from) {
    *this = std::move(from);
  }
  Data &operator=(Data &&from) {
    if (this == &from) {
      return *this;
    }
    value = from.value;
    in_list = from.in_list;
    from.value = 0;
    from.in_list = false;
    return *this;
  }
  ~Data() = default;
};

struct Node : public td::ListNode {
  Node() = default;
  explicit Node(Data data) : data(std::move(data)) {
  }

  Data data;
};

Data &get_data(Node &node) {
  return node.data;
}

Data &get_data(td::TsListNode<Data> &node) {
  return node.get_data_unsafe();
}

std::unique_lock<std::mutex> lock(td::ListNode &node) {
  return {};
}

std::unique_lock<std::mutex> lock(td::TsListNode<Data> &node) {
  return node.lock();
}

template <class ListNodeT, class ListRootT, class NodeT>
void do_run_list_test(ListRootT &root, std::atomic<td::uint64> &id) {
  td::vector<NodeT> nodes;

  td::Random::Xorshift128plus rnd(123);

  auto next_id = [&] {
    return ++id;
  };
  auto add_node = [&] {
    if (nodes.size() >= 20) {
      return;
    }
    nodes.emplace_back(NodeT({next_id(), false}));
  };
  auto pop_node = [&] {
    if (nodes.size() == 0) {
      return;
    }
    nodes.pop_back();
  };

  auto link_node = [&] {
    if (nodes.empty()) {
      return;
    }
    auto i = rnd.fast(0, (int)nodes.size() - 1);
    nodes[i].remove();
    get_data(nodes[i]) = Data(next_id(), true);
    root.put(&nodes[i]);
  };
  auto unlink_node = [&] {
    if (nodes.empty()) {
      return;
    }
    auto i = rnd.fast(0, (int)nodes.size() - 1);
    nodes[i].remove();
    get_data(nodes[i]).in_list = false;
  };
  auto swap_nodes = [&] {
    if (nodes.empty()) {
      return;
    }
    auto i = rnd.fast(0, (int)nodes.size() - 1);
    auto j = rnd.fast(0, (int)nodes.size() - 1);
    std::swap(nodes[i], nodes[j]);
  };
  auto set_node = [&] {
    if (nodes.empty()) {
      return;
    }
    auto i = rnd.fast(0, (int)nodes.size() - 1);
    auto j = rnd.fast(0, (int)nodes.size() - 1);
    nodes[i] = std::move(nodes[j]);
  };
  auto validate = [&] {
    std::multiset<td::uint64> in_list, not_in_list;
    for (auto &node : nodes) {
      if (get_data(node).in_list) {
        in_list.insert(get_data(node).value);
      } else {
        not_in_list.insert(get_data(node).value);
      }
    }
    auto guard = lock(root);
    for (auto *begin = root.begin(), *end = root.end(); begin != end; begin = begin->get_next()) {
      auto &data = get_data(*static_cast<NodeT *>(begin));
      CHECK(data.in_list);
      CHECK(data.value != 0);
      auto it = in_list.find(data.value);
      if (it != in_list.end()) {
        in_list.erase(it);
      } else {
        ASSERT_EQ(0u, not_in_list.count(data.value));
      }
    }
    ASSERT_EQ(0u, in_list.size());
  };
  td::RandomSteps steps(
      {{add_node, 3}, {pop_node, 1}, {unlink_node, 1}, {link_node, 3}, {swap_nodes, 1}, {set_node, 1}, {validate, 1}});
  for (int i = 0; i < 10000; i++) {
    steps.step(rnd);
  }
}

TEST(Misc, List) {
  td::ListNode root;
  std::atomic<td::uint64> id{0};
  for (std::size_t i = 0; i < 4; i++) {
    do_run_list_test<td::ListNode, td::ListNode, Node>(root, id);
  }
}

TEST(Misc, TsList) {
  td::TsList<Data> root;
  std::atomic<td::uint64> id{0};
  for (std::size_t i = 0; i < 4; i++) {
    do_run_list_test<td::TsListNode<Data>, td::TsList<Data>, td::TsListNode<Data>>(root, id);
  }
}

TEST(Misc, TsListConcurrent) {
  td::TsList<Data> root;
  td::vector<td::thread> threads;
  std::atomic<td::uint64> id{0};
  for (std::size_t i = 0; i < 4; i++) {
    threads.emplace_back(
        [&] { do_run_list_test<td::TsListNode<Data>, td::TsList<Data>, td::TsListNode<Data>>(root, id); });
  }
}
