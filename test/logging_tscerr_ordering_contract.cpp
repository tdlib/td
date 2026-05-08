// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingTsCerrOrderingContract, SpinLockUsesAcquireReleaseAndRejectsSeqCstPaths) {
  auto source = load_repo_text("tdutils/td/utils/TsCerr.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("while(lock().test_and_set(std::memory_order_acquire))") != td::string::npos);
  ASSERT_TRUE(normalized.find("lock().clear(std::memory_order_release);") != td::string::npos);
  ASSERT_TRUE(normalized.find("test_and_set(std::memory_order::seq_cst)") == td::string::npos);
  ASSERT_TRUE(normalized.find("clear(std::memory_order::seq_cst)") == td::string::npos);
}

TEST(LoggingTsCerrOrderingContract, ExitGuardFailClosedAndBackoffStayPinned) {
  auto source = load_repo_text("tdutils/td/utils/TsCerr.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("if(ExitGuard::is_exited()){returnfalse;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(lock_is_acquired_){exitCritical();}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!lock_is_acquired_){return*this;}") != td::string::npos);
  ASSERT_TRUE(contains_any(source, {"std::this_thread::yield", "sched_yield", "spin_backoff", "cpu_relax"}));
}

}  // namespace
