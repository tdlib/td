// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/logging.h"
#include "td/utils/tests.h"

#include <limits>

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

class VerbosityRestoreGuard final {
 public:
  explicit VerbosityRestoreGuard(int level) : level_(level) {
  }

  ~VerbosityRestoreGuard() {
    td::set_verbosity_level(level_);
  }

 private:
  int level_;
};

TEST(LoggingScopedDisableContract, SourcePinsMutexGuardedOutermostRestorePath) {
  auto normalized = normalize_for_contract(load_repo_text("tdutils/td/utils/logging.cpp"));

  ASSERT_TRUE(normalized.find("std::mutexsdl_mutex;") != td::string::npos);
  ASSERT_TRUE(normalized.find("intsdl_cnt=0;") != td::string::npos);
  ASSERT_TRUE(normalized.find("intsdl_verbosity=0;") != td::string::npos);
  ASSERT_TRUE(normalized.find("std::unique_lock<std::mutex>guard(sdl_mutex);if(sdl_cnt==0){sdl_verbosity=set_verbosity_"
                              "level(std::numeric_limits<int>::min());}sdl_cnt++;") != td::string::npos);
  ASSERT_TRUE(normalized.find("std::unique_lock<std::mutex>guard(sdl_mutex);sdl_cnt--;if(sdl_cnt==0){set_verbosity_"
                              "level(sdl_verbosity);}") != td::string::npos);
}

TEST(LoggingScopedDisableContract, NestedScopesRestoreOriginalVerbosityExactlyOnce) {
  const auto original_level = td::get_verbosity_level();
  VerbosityRestoreGuard restore(original_level);

  td::set_verbosity_level(VERBOSITY_NAME(INFO));
  ASSERT_EQ(VERBOSITY_NAME(INFO), td::get_verbosity_level());

  {
    td::ScopedDisableLog outer_disable;
    ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());
    {
      td::ScopedDisableLog inner_disable;
      ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());
    }
    ASSERT_EQ(std::numeric_limits<int>::min(), td::get_verbosity_level());
  }

  ASSERT_EQ(VERBOSITY_NAME(INFO), td::get_verbosity_level());
}

}  // namespace