// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (const auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

}  // namespace

TEST(ThreadIdGuardContract, thread_id_manager_has_process_lifetime_without_static_destruction) {
  const auto source =
      normalize_for_contract(td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/detail/ThreadIdGuard.cpp"));

  ASSERT_NE(
      td::string::npos,
      source.find("ThreadIdManager&get_thread_id_manager(){staticauto*manager=newThreadIdManager();return*manager;}"));
  ASSERT_EQ(td::string::npos, source.find("staticThreadIdManagerthread_id_manager;"));
  ASSERT_NE(td::string::npos, source.find("thread_id_=get_thread_id_manager().register_thread();"));
  ASSERT_NE(td::string::npos, source.find("get_thread_id_manager().unregister_thread(thread_id_);"));
}

TEST(ThreadIdGuardContract, thread_id_reuse_pool_avoids_tree_allocations) {
  const auto source =
      normalize_for_contract(td::mtproto::test::read_repo_text_file("tdutils/td/utils/port/detail/ThreadIdGuard.cpp"));

  ASSERT_NE(td::string::npos, source.find("std::vector<int32>unused_thread_ids_;"));
  ASSERT_EQ(td::string::npos, source.find("std::set<int32>unused_thread_ids_;"));
  ASSERT_NE(td::string::npos, source.find("autoresult=unused_thread_ids_.back();"));
  ASSERT_NE(td::string::npos, source.find("unused_thread_ids_.pop_back();"));
  ASSERT_NE(td::string::npos,
            source.find("autoit=std::find(unused_thread_ids_.begin(),unused_thread_ids_.end(),thread_id);"));
}