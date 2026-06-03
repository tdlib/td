// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingTagVerbosityContract, TagRegistryUsesStaticEntriesInsteadOfGlobalMapAllocation) {
  auto source = load_repo_text("td/telegram/Logging.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("structLogTagEntry{Slicename;std::atomic<int>*verbosity_level;") != td::string::npos);
  ASSERT_TRUE(normalized.find("std::array<LogTagEntry,20>log_tags") != td::string::npos);
  ASSERT_TRUE(source.find("find_log_tag_entry(") != td::string::npos);
  ASSERT_TRUE(normalized.find("std::map<Slice,std::atomic<int>*>log_tags") == td::string::npos);
  ASSERT_TRUE(normalized.find("std::map<Slice,int*>log_tags") == td::string::npos);
}

TEST(LoggingTagVerbosityContract, SetAndGetTagVerbosityUseAtomicHelpersAndClampFailClosed) {
  auto source = load_repo_text("td/telegram/Logging.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(source.find("store_tag_verbosity_level(") != td::string::npos);
  ASSERT_TRUE(source.find("load_tag_verbosity_level(") != td::string::npos);
  ASSERT_TRUE(source.find("clamp(new_verbosity_level, 1, VERBOSITY_NAME(NEVER))") != td::string::npos);
  ASSERT_TRUE(normalized.find("if(tag.empty()){returnStatus::Error(\"Logtagmustbenon-empty\");}") != td::string::npos);
}

TEST(LoggingTagVerbosityContract, MutableTagGlobalsAreDeclaredAndDefinedAsAtomics) {
  auto td_h = load_repo_text("td/telegram/Td.h");
  auto td_cpp = load_repo_text("td/telegram/Td.cpp");
  auto file_manager_h = load_repo_text("td/telegram/files/FileManager.h");
  auto file_manager_cpp = load_repo_text("td/telegram/files/FileManager.cpp");

  ASSERT_TRUE(td_h.find("extern std::atomic<int> VERBOSITY_NAME(td_init);") != td::string::npos);
  ASSERT_TRUE(td_h.find("extern std::atomic<int> VERBOSITY_NAME(td_requests);") != td::string::npos);
  ASSERT_TRUE(td_cpp.find("std::atomic<int> VERBOSITY_NAME(td_init)") != td::string::npos);
  ASSERT_TRUE(td_cpp.find("std::atomic<int> VERBOSITY_NAME(td_requests)") != td::string::npos);

  ASSERT_TRUE(file_manager_h.find("extern std::atomic<int> VERBOSITY_NAME(update_file);") != td::string::npos);
  ASSERT_TRUE(file_manager_cpp.find("std::atomic<int> VERBOSITY_NAME(update_file)") != td::string::npos);
}

// Every one of the 20 tags registered in Logging.cpp::log_tags must be declared
// as extern std::atomic<int> in its header and defined as std::atomic<int> in its
// translation unit.  This test pins the full registry, not just a sample.
TEST(LoggingTagVerbosityContract, AllRegisteredTagsAreDeclaredAndDefinedAsAtomicsFullRegistry) {
  // {header_path, impl_path, tag_name}
  const td::vector<std::tuple<td::string, td::string, td::string>> registry = {
      // Td group
      {"td/telegram/Td.h", "td/telegram/Td.cpp", "td_init"},
      {"td/telegram/Td.h", "td/telegram/Td.cpp", "td_requests"},
      // Network
      {"td/telegram/net/ConnectionCreator.h", "td/telegram/net/ConnectionCreator.cpp", "connections"},
      {"td/telegram/net/DcAuthManager.h", "td/telegram/net/DcAuthManager.cpp", "dc"},
      {"td/telegram/net/NetQuery.h", "td/telegram/net/NetQuery.cpp", "net_query"},
      // MTProto / transport
      {"td/mtproto/SessionConnection.h", "td/mtproto/SessionConnection.cpp", "mtproto"},
      {"td/mtproto/Transport.h", "td/mtproto/Transport.cpp", "raw_mtproto"},
      // DB / file
      {"tddb/td/db/binlog/Binlog.h", "tddb/td/db/binlog/Binlog.cpp", "binlog"},
      {"tddb/td/db/SqliteStatement.h", "tddb/td/db/SqliteStatement.cpp", "sqlite"},
      // Files
      {"td/telegram/files/FileManager.h", "td/telegram/files/FileManager.cpp", "update_file"},
      {"td/telegram/files/FileLoaderUtils.h", "td/telegram/files/FileLoaderUtils.cpp", "file_loader"},
      {"td/telegram/files/FileGcWorker.h", "td/telegram/files/FileGcWorker.cpp", "file_gc"},
      {"td/telegram/FileReferenceManager.h", "td/telegram/FileReferenceManager.cpp", "file_references"},
      // Utilities
      {"tdutils/td/utils/port/detail/NativeFd.h", "tdutils/td/utils/port/detail/NativeFd.cpp", "fd"},
      {"tdactor/td/actor/impl/Scheduler-decl.h", "tdactor/td/actor/impl/Scheduler.cpp", "actor"},
      // Net subsystem
      {"tdnet/td/net/TransparentProxy.h", "tdnet/td/net/TransparentProxy.cpp", "proxy"},
      {"tdnet/td/net/GetHostByNameActor.h", "tdnet/td/net/GetHostByNameActor.cpp", "dns_resolver"},
      // Telegram features
      {"td/telegram/NotificationManager.h", "td/telegram/NotificationManager.cpp", "notifications"},
      {"td/telegram/UpdatesManager.h", "td/telegram/UpdatesManager.cpp", "get_difference"},
      {"td/telegram/ConfigManager.h", "td/telegram/ConfigManager.cpp", "config_recoverer"},
  };

  for (const auto &[hdr, src, tag] : registry) {
    auto header_text = load_repo_text(hdr);
    auto source_text = load_repo_text(src);

    td::string expect_decl = "extern std::atomic<int> VERBOSITY_NAME(" + tag + ")";
    td::string expect_def = "std::atomic<int> VERBOSITY_NAME(" + tag + ")";
    td::string wrong_decl = "extern int VERBOSITY_NAME(" + tag + ")";

    ASSERT_TRUE(header_text.find(expect_decl) != td::string::npos);
    ASSERT_TRUE(header_text.find(wrong_decl) == td::string::npos);
    ASSERT_TRUE(source_text.find(expect_def) != td::string::npos);
  }
}

}  // namespace
