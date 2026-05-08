// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

#include <random>

namespace {

using td::logging_hardening::test::load_repo_text;

TEST(LoggingTagVerbosityLightFuzz, RandomizedKnownTagUpdatesStayClampedAndReadable) {
  std::mt19937 rng(0x9A47CE11u);
  std::uniform_int_distribution<int> level_dist(-2000, 5000);

  auto before = td::Logging::get_tag_verbosity_level("td_init");
  ASSERT_TRUE(before.is_ok());

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    auto level = level_dist(rng);
    auto status = td::Logging::set_tag_verbosity_level("td_init", level);
    ASSERT_TRUE(status.is_ok());

    auto current = td::Logging::get_tag_verbosity_level("td_init");
    ASSERT_TRUE(current.is_ok());
    ASSERT_TRUE(1 <= current.ok());
    ASSERT_TRUE(current.ok() <= VERBOSITY_NAME(NEVER));
  }

  ASSERT_TRUE(td::Logging::set_tag_verbosity_level("td_init", before.ok()).is_ok());
}

TEST(LoggingTagVerbosityLightFuzz, SourceRequiresAtomicTagReadWriteHelpers) {
  auto source = load_repo_text("td/telegram/Logging.cpp");

  ASSERT_TRUE(source.find("store_tag_verbosity_level(") != td::string::npos);
  ASSERT_TRUE(source.find("load_tag_verbosity_level(") != td::string::npos);
}

}  // namespace
