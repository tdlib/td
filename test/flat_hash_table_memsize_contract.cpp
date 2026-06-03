// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

td::string load_repo_text(td::Slice relative_path) {
  return td::mtproto::test::read_repo_text_file(relative_path);
}

}  // namespace

TEST(FlatHashTableMemsizeContract, source_uses_explicit_size_bridge_for_pointer_and_index_arithmetic) {
  const auto source = load_repo_text("tdutils/td/utils/FlatHashTable.h");

  ASSERT_TRUE(source.find("static constexpr size_t to_size(uint32 value)") != td::string::npos);
  ASSERT_TRUE(source.find("nodes_[to_size(bucket)]") != td::string::npos);
  ASSERT_TRUE(source.find("nodes_ + to_size(bucket_count())") != td::string::npos);
  ASSERT_TRUE(source.find("nodes_ + to_size(begin_bucket_)") != td::string::npos);

  ASSERT_EQ(td::string::npos, source.find("nodes_[bucket]"));
  ASSERT_EQ(td::string::npos, source.find("nodes_[begin_bucket_]"));
  ASSERT_EQ(td::string::npos, source.find("nodes_ + bucket_count()"));
  ASSERT_EQ(td::string::npos, source.find("nodes_ + begin_bucket_"));
}

TEST(FlatHashTableMemsizeContract, source_uses_explicit_casts_for_size_and_allocation) {
  const auto source = load_repo_text("tdutils/td/utils/FlatHashTable.h");

  ASSERT_TRUE(source.find("nodes_ = new NodeT[to_size(size)];") != td::string::npos);
  ASSERT_TRUE(source.find("return static_cast<size_t>(used_node_count_);") != td::string::npos);
}
