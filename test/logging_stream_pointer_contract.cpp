// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::contains_identifier_assignment;
using td::logging_hardening::test::load_repo_text;
using td::logging_hardening::test::normalize_for_contract;

TEST(LoggingStreamPointerContract, HeaderPinsAtomicAccessorSurface) {
  auto source = load_repo_text("tdutils/td/utils/logging.h");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(source.find("LogInterface *load_active_log_interface(") != td::string::npos);
  ASSERT_TRUE(source.find("void store_active_log_interface(") != td::string::npos);
  ASSERT_TRUE(normalized.find("LOG_IMPL_FULL(*::td::load_active_log_interface()") != td::string::npos);
  ASSERT_TRUE(normalized.find("LOG_IMPL_FULL(*::td::log_interface") == td::string::npos);
}

TEST(LoggingStreamPointerContract, ImplementationPinsAtomicStateAndMemoryOrder) {
  auto source = load_repo_text("tdutils/td/utils/logging.cpp");
  auto normalized = normalize_for_contract(source);

  ASSERT_TRUE(normalized.find("std::atomic<LogInterface*>active_log_interface") != td::string::npos);
  ASSERT_TRUE(normalized.find("returnactive_log_interface.load(std::memory_order_acquire)") != td::string::npos);
  ASSERT_TRUE(normalized.find("active_log_interface.store(interface,std::memory_order_release)") != td::string::npos);
  ASSERT_TRUE(normalized.find("LogInterface*log_interface=default_log_interface") == td::string::npos);
}

TEST(LoggingStreamPointerContract, ProductionCallsitesUseHelpersInsteadOfRawPointerMutation) {
  const td::vector<td::string> callsites = {
      "tdutils/td/utils/check.cpp",        "td/telegram/Logging.cpp", "td/telegram/StorageManager.cpp",
      "td/telegram/files/FileManager.cpp", "td/telegram/cli.cpp",
  };

  for (const auto &path : callsites) {
    auto source = load_repo_text(path);

    ASSERT_TRUE(!contains_identifier_assignment(source, "log_interface"));
    ASSERT_TRUE(source.find("*log_interface") == td::string::npos);
    ASSERT_TRUE(source.find("log_interface->") == td::string::npos);

    ASSERT_TRUE(contains_any(source, {"load_active_log_interface()", "store_active_log_interface("}));
  }
}

}  // namespace
