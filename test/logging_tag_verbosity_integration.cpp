// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

#include <algorithm>

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

TEST(LoggingTagVerbosityIntegration, RuntimeGetTagsReturnsSortedUniqueKnownEntries) {
  auto tags = td::Logging::get_tags();

  ASSERT_EQ(20u, tags.size());
  ASSERT_TRUE(std::is_sorted(tags.begin(), tags.end()));
  ASSERT_TRUE(std::adjacent_find(tags.begin(), tags.end()) == tags.end());
  ASSERT_TRUE(std::find(tags.begin(), tags.end(), "td_init") != tags.end());
  ASSERT_TRUE(std::find(tags.begin(), tags.end(), "td_requests") != tags.end());
  ASSERT_TRUE(std::find(tags.begin(), tags.end(), "sqlite") != tags.end());
  ASSERT_TRUE(std::find(tags.begin(), tags.end(), "file_references") != tags.end());
}

TEST(LoggingTagVerbosityIntegration, RuntimeRejectsEmptyTagQueriesFailClosed) {
  auto set_status = td::Logging::set_tag_verbosity_level(td::Slice(), 3);
  ASSERT_TRUE(set_status.is_error());

  auto get_result = td::Logging::get_tag_verbosity_level(td::Slice());
  ASSERT_TRUE(get_result.is_error());
}

}  // namespace
