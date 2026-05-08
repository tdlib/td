// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingSpinContract, TsLogSpinLoopKeepsBackoffAndFailClosedExitPath) {
  auto source = load_repo_text("tdutils/td/utils/TsLog.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("while(lock_.test_and_set(std::memory_order_acquire))") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(ExitGuard::is_exited()){returnfalse;}") != td::string::npos);
  ASSERT_TRUE(contains_any(source, {"std::this_thread::yield", "sched_yield", "spin_backoff", "cpu_relax"}));
}

TEST(LoggingSpinContract, TsLogCriticalSectionSerializationSemanticsRemainPinned) {
  auto header = load_repo_text("tdutils/td/utils/TsLog.h");
  auto normalized = normalize_for_contract(header);

  ASSERT_TRUE(header.find("bool enter_critical();") != td::string::npos);
  ASSERT_TRUE(normalized.find("lock_.clear(std::memory_order_release)") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!enter_critical()){return;}log_->after_rotation();exit_critical();") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find(
                  "if(!enter_critical()){return{};}autoresult=log_->get_file_paths();exit_critical();returnresult;") !=
              td::string::npos);
  ASSERT_TRUE(normalized.find("if(!enter_critical()){return;}log_->do_append(log_level,slice);exit_critical();") !=
              td::string::npos);
}

}  // namespace