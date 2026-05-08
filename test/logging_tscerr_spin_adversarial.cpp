// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingTsCerrSpinAdversarial, TsCerrSpinLoopMustContainBackoffHintUnderPressure) {
  auto source = load_repo_text("tdutils/td/utils/TsCerr.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("lock().test_and_set(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(normalized.find("test_and_set(std::memory_order::seq_cst)") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(ExitGuard::is_exited()){returnfalse;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("&&!ExitGuard::is_exited()") == td::string::npos);
  ASSERT_TRUE(contains_any(source, {"std::this_thread::yield", "sched_yield", "spin_backoff", "cpu_relax"}));
}

TEST(LoggingTsCerrSpinAdversarial, TsCerrSpinCommentWithoutBackoffIsRejected) {
  auto source = load_repo_text("tdutils/td/utils/TsCerr.cpp");

  ASSERT_TRUE(source.find("// spin") == td::string::npos);
}

TEST(LoggingTsCerrSpinAdversarial, ShutdownPathMustNotUnlockWithoutOwnership) {
  auto normalized = normalize_for_contract(load_repo_text("tdutils/td/utils/TsCerr.cpp"));

  ASSERT_TRUE(normalized.find("TsCerr::~TsCerr(){exitCritical();}") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(lock_is_acquired_){exitCritical();}") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(!lock_is_acquired_){return*this;}") != td::string::npos);
}

}  // namespace
