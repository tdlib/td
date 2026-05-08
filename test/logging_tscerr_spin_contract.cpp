// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingTsCerrSpinContract, SpinLoopKeepsExitGuardAndAddsBackoffHint) {
  auto source = load_repo_text("tdutils/td/utils/TsCerr.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("lock().test_and_set(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(normalized.find("test_and_set(std::memory_order::seq_cst)") == td::string::npos);
  ASSERT_TRUE(normalized.find("if(ExitGuard::is_exited()){returnfalse;}") != td::string::npos);
  ASSERT_TRUE(normalized.find("&&!ExitGuard::is_exited()") == td::string::npos);
  ASSERT_TRUE(contains_any(source, {"std::this_thread::yield", "sched_yield", "spin_backoff", "cpu_relax"}));
}

TEST(LoggingTsCerrSpinContract, CriticalSectionSerializationSemanticsRemainPinned) {
  auto source = load_repo_text("tdutils/td/utils/TsCerr.cpp");
  auto header = load_repo_text("tdutils/td/utils/TsCerr.h");

  ASSERT_TRUE(source.find("TsCerr::TsCerr()") != td::string::npos);
  ASSERT_TRUE(source.find("TsCerr::~TsCerr()") != td::string::npos);
  ASSERT_TRUE(source.find("std::atomic_flag &TsCerr::lock()") != td::string::npos);
  ASSERT_TRUE(source.find("static std::atomic_flag lock_instance = ATOMIC_FLAG_INIT;") != td::string::npos);
  ASSERT_TRUE(source.find("lock_is_acquired_ = enterCritical();") != td::string::npos);
  ASSERT_TRUE(source.find("if (lock_is_acquired_)") != td::string::npos);
  ASSERT_TRUE(source.find("lock().clear(std::memory_order_release);") != td::string::npos);
  ASSERT_TRUE(source.find("lock().clear(std::memory_order::seq_cst);") == td::string::npos);
  ASSERT_TRUE(source.find("if (!lock_is_acquired_)") != td::string::npos);
  ASSERT_TRUE(header.find("bool lock_is_acquired_{false};") != td::string::npos);
  ASSERT_TRUE(header.find("static std::atomic_flag &lock();") != td::string::npos);
}

}  // namespace
