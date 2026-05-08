// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingMacroContract, VerbosityConstantsRemainStable) {
  ASSERT_EQ(-1, VERBOSITY_NAME(PLAIN));
  ASSERT_EQ(0, VERBOSITY_NAME(FATAL));
  ASSERT_EQ(1, VERBOSITY_NAME(ERROR));
  ASSERT_EQ(2, VERBOSITY_NAME(WARNING));
  ASSERT_EQ(3, VERBOSITY_NAME(INFO));
  ASSERT_EQ(4, VERBOSITY_NAME(DEBUG));
  ASSERT_EQ(1024, VERBOSITY_NAME(NEVER));
}

TEST(LoggingMacroContract, StripPredicateMatchesArithmeticReferenceForStandardLevels) {
  const td::vector<int> levels = {VERBOSITY_NAME(PLAIN),   VERBOSITY_NAME(FATAL), VERBOSITY_NAME(ERROR),
                                  VERBOSITY_NAME(WARNING), VERBOSITY_NAME(INFO),  VERBOSITY_NAME(DEBUG),
                                  VERBOSITY_NAME(NEVER)};

  for (auto strip_level : levels) {
    for (auto build_strip_level : levels) {
      ASSERT_EQ(strip_level > build_strip_level, td::detail::is_log_stripped(strip_level, build_strip_level));
    }
  }
}

TEST(LoggingMacroContract, HeaderPinsAtomicCustomVerbosityExample) {
  auto normalized = normalize_for_contract(load_repo_text("tdutils/td/utils/logging.h"));

  ASSERT_TRUE(normalized.find("std::atomic<int>VERBOSITY_NAME(custom){VERBOSITY_NAME(WARNING)};") != td::string::npos);
  ASSERT_TRUE(normalized.find("intVERBOSITY_NAME(custom)=VERBOSITY_NAME(WARNING);") == td::string::npos);
}

}  // namespace
