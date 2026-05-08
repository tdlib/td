// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::load_repo_text;

TEST(LoggingTagVerbosityIntegration, RuntimeSetGetClampForKnownTag) {
  auto before = td::Logging::get_tag_verbosity_level("td_init");
  ASSERT_TRUE(before.is_ok());

  auto set_status = td::Logging::set_tag_verbosity_level("td_init", VERBOSITY_NAME(NEVER) + 777);
  ASSERT_TRUE(set_status.is_ok());

  auto after = td::Logging::get_tag_verbosity_level("td_init");
  ASSERT_TRUE(after.is_ok());
  ASSERT_EQ(VERBOSITY_NAME(NEVER), after.ok());

  ASSERT_TRUE(td::Logging::set_tag_verbosity_level("td_init", before.ok()).is_ok());
}

TEST(LoggingTagVerbosityIntegration, SourceRequiresAtomicTagHelpersForReadAndWritePaths) {
  auto source = load_repo_text("td/telegram/Logging.cpp");

  ASSERT_TRUE(source.find("store_tag_verbosity_level(") != td::string::npos);
  ASSERT_TRUE(source.find("load_tag_verbosity_level(") != td::string::npos);
}

}  // namespace
