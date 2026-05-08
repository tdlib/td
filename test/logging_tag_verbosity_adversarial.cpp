// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

#include <random>

namespace {

using td::logging_hardening::test::load_repo_text;

TEST(LoggingTagVerbosityAdversarial, RejectsRandomUnknownTagsWithoutStateCorruption) {
  auto baseline_result = td::Logging::get_tag_verbosity_level("td_init");
  ASSERT_TRUE(baseline_result.is_ok());
  const int baseline = baseline_result.ok();

  std::mt19937 rng(0x41A99C7u);
  std::uniform_int_distribution<int> len_dist(1, 24);
  std::uniform_int_distribution<int> ch_dist(33, 126);

  constexpr int kIterations = 12000;
  for (int i = 0; i < kIterations; i++) {
    td::string unknown_tag;
    const int len = len_dist(rng);
    unknown_tag.reserve(static_cast<size_t>(len));
    for (int p = 0; p < len; p++) {
      unknown_tag.push_back(static_cast<char>(ch_dist(rng)));
    }

    auto set_result = td::Logging::set_tag_verbosity_level(unknown_tag, i % VERBOSITY_NAME(NEVER));
    ASSERT_TRUE(set_result.is_error());

    auto after = td::Logging::get_tag_verbosity_level("td_init");
    ASSERT_TRUE(after.is_ok());
    ASSERT_EQ(baseline, after.ok());
  }
}

TEST(LoggingTagVerbosityAdversarial, SourceRejectsRawMutableIntTagDefinitions) {
  auto td_cpp = load_repo_text("td/telegram/Td.cpp");
  auto file_manager_cpp = load_repo_text("td/telegram/files/FileManager.cpp");
  auto logging_cpp = load_repo_text("td/telegram/Logging.cpp");

  ASSERT_TRUE(td_cpp.find("int VERBOSITY_NAME(td_init)") == td::string::npos);
  ASSERT_TRUE(td_cpp.find("int VERBOSITY_NAME(td_requests)") == td::string::npos);
  ASSERT_TRUE(file_manager_cpp.find("int VERBOSITY_NAME(update_file)") == td::string::npos);
  ASSERT_TRUE(logging_cpp.find("*it->second =") == td::string::npos);
}

// Black-hat attack: attempt to detect if ANY registered tag definition site
// reverted from std::atomic<int> back to plain int.  Covers all 20 source files.
TEST(LoggingTagVerbosityAdversarial, AllSourceFilesRejectRawIntTagDefinitions) {
  // {source_path, tag_name} — mirrors log_tags in Logging.cpp
  const td::vector<std::pair<td::string, td::string>> tag_sources = {
      {"td/telegram/Td.cpp", "td_init"},
      {"td/telegram/Td.cpp", "td_requests"},
      {"td/telegram/net/ConnectionCreator.cpp", "connections"},
      {"td/telegram/net/DcAuthManager.cpp", "dc"},
      {"td/telegram/net/NetQuery.cpp", "net_query"},
      {"td/mtproto/SessionConnection.cpp", "mtproto"},
      {"td/mtproto/Transport.cpp", "raw_mtproto"},
      {"tddb/td/db/binlog/Binlog.cpp", "binlog"},
      {"tddb/td/db/SqliteStatement.cpp", "sqlite"},
      {"td/telegram/files/FileManager.cpp", "update_file"},
      {"td/telegram/files/FileLoaderUtils.cpp", "file_loader"},
      {"td/telegram/files/FileGcWorker.cpp", "file_gc"},
      {"td/telegram/FileReferenceManager.cpp", "file_references"},
      {"tdutils/td/utils/port/detail/NativeFd.cpp", "fd"},
      {"tdactor/td/actor/impl/Scheduler.cpp", "actor"},
      {"tdnet/td/net/TransparentProxy.cpp", "proxy"},
      {"tdnet/td/net/GetHostByNameActor.cpp", "dns_resolver"},
      {"td/telegram/NotificationManager.cpp", "notifications"},
      {"td/telegram/UpdatesManager.cpp", "get_difference"},
      {"td/telegram/ConfigManager.cpp", "config_recoverer"},
  };

  for (const auto &[src_path, tag] : tag_sources) {
    auto source = load_repo_text(src_path);

    // "int VERBOSITY_NAME(tag)" would flag a plain-int definition (no atomic)
    td::string raw_int_def = "int VERBOSITY_NAME(" + tag + ")";
    td::string atomic_def = "std::atomic<int> VERBOSITY_NAME(" + tag + ")";

    ASSERT_TRUE(source.find(atomic_def) != td::string::npos);

    // Check that a bare "int verbosity_TAG" is not present as a separate definition
    // (the atomic_def check above is sufficient, but this adds extra defence)
    auto raw_pos = source.find(raw_int_def);
    if (raw_pos != td::string::npos) {
      // The only acceptable occurrence is inside the atomic definition itself
      // (e.g. "std::atomic<int> verbosity_tag")
      ASSERT_TRUE(source.substr(raw_pos > 7 ? raw_pos - 7 : 0, 20).find("std::atomic") != td::string::npos);
    }
  }
}

}  // namespace
