// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/Heap.h"
#include "td/utils/tests.h"

#include <array>
#include <random>

TEST(TdutilsHeapHashMiscAdversarial, heap_collision_like_churn_matches_reference_ordering) {
  struct NodeState {
    td::HeapNode node;
    td::uint64 key{0};
    bool present{false};
  };

  td::KHeap<td::uint64> heap;
  std::mt19937_64 rng(0xD31A5EEDULL);

  constexpr td::uint32 kNodes = 2048;
  std::array<NodeState, kNodes> nodes{};

  const auto find_expected_top_key = [&]() {
    bool has_expected = false;
    td::uint64 expected_top_key = 0;
    for (const auto &node : nodes) {
      if (!node.present) {
        continue;
      }
      if (!has_expected || node.key < expected_top_key) {
        expected_top_key = node.key;
        has_expected = true;
      }
    }
    return std::make_pair(has_expected, expected_top_key);
  };

  std::uniform_int_distribution<td::uint32> id_dist(0, kNodes - 1);
  std::uniform_int_distribution<td::uint64> key_dist(0, (1ULL << 40) - 1);

  constexpr td::uint32 kIterations = 120000;
  for (td::uint32 i = 0; i < kIterations; i++) {
    const auto id = id_dist(rng);
    auto &n = nodes[id];
    const auto action = static_cast<td::uint32>(rng() % 4);

    if (action == 0) {
      if (!n.present) {
        n.key = key_dist(rng);
        heap.insert(n.key, &n.node);
        n.present = true;
      }
    } else if (action == 1) {
      if (n.present) {
        n.key = key_dist(rng);
        heap.fix(n.key, &n.node);
      }
    } else if (action == 2) {
      if (n.present) {
        heap.erase(&n.node);
        n.present = false;
      }
    } else {
      if (!heap.empty()) {
        const auto [has_expected, expected_top_key] = find_expected_top_key();
        const auto top_key = heap.top_key();
        ASSERT_TRUE(has_expected);
        ASSERT_EQ(expected_top_key, top_key);

        auto *top_node = heap.pop();
        bool found = false;
        for (td::uint32 j = 0; j < kNodes; j++) {
          if (&nodes[j].node == top_node) {
            ASSERT_TRUE(nodes[j].present);
            nodes[j].present = false;
            found = true;
            break;
          }
        }
        ASSERT_TRUE(found);
      }
    }

    if (!heap.empty()) {
      const auto [has_expected, expected_top_key] = find_expected_top_key();
      ASSERT_TRUE(has_expected);
      ASSERT_EQ(expected_top_key, heap.top_key());
    } else {
      ASSERT_FALSE(find_expected_top_key().first);
    }
  }
}
