#include "td/utils/tests.h"
#include "td/utils/FlatHashMap.h"
#include <array>
#include <string>

TEST(FlatHashMap, basic) {
  td::FlatHashMap<int, int> map;
  map[1] = 2;
  ASSERT_EQ(2, map[1]);
  ASSERT_EQ(1, map.find(1)->first);
  ASSERT_EQ(2, map.find(1)->second);
  // ASSERT_EQ(1, map.find(1)->key());
  // ASSERT_EQ(2, map.find(1)->value());
  for (auto &kv : map) {
    ASSERT_EQ(1, kv.first);
    ASSERT_EQ(2, kv.second);
  }
  map.erase(map.find(1));
  auto map_copy = map;

  td::FlatHashMap<int, std::array<std::unique_ptr<std::string>, 20>> x;
  auto y = std::move(x);
  x[12];
  x.erase(x.find(12));
}