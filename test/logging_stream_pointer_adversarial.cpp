// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/utils/tests.h"

#include <random>

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::contains_identifier_assignment;
using td::logging_hardening::test::load_repo_text;

TEST(LoggingStreamPointerAdversarial, AttackSurfaceRejectsRawPointerReadsAndWritesInMacroPath) {
  auto source = load_repo_text("tdutils/td/utils/logging.h");

  ASSERT_TRUE(source.find("LOG_IMPL_FULL(*::td::log_interface") == td::string::npos);
  ASSERT_TRUE(source.find("load_active_log_interface()") != td::string::npos);
}

TEST(LoggingStreamPointerAdversarial, ProductionMutationSitesDoNotBypassAtomicHelpers) {
  const td::vector<td::string> files = {
      "tdutils/td/utils/check.cpp",        "td/telegram/Logging.cpp", "td/telegram/StorageManager.cpp",
      "td/telegram/files/FileManager.cpp", "td/telegram/cli.cpp",
  };

  for (const auto &path : files) {
    auto source = load_repo_text(path);
    ASSERT_TRUE(source.find("log_interface") == td::string::npos ||
                contains_any(source, {"load_active_log_interface", "store_active_log_interface"}));
  }
}

TEST(LoggingStreamPointerAdversarial, FuzzedSourceSelectionNeverFindsLegacyMutationTokens) {
  const td::vector<td::string> files = {
      "tdutils/td/utils/logging.cpp",
      "td/telegram/Logging.cpp",
      "td/telegram/cli.cpp",
      "tdutils/test/log.cpp",
      "test/stealth/test_tls_init_log_contract.cpp",
      "test/stealth/test_stealth_params_loader_reload_log_contract.cpp",
      "test/stealth/test_stream_transport_activation_fail_closed.cpp",
      "test/stealth/test_raw_connection_error_contract.cpp",
      // Production callsites verified by the contract test that were missing from
      // the adversarial fuzz pool; must not reintroduce raw log_interface assignments.
      "tdutils/td/utils/check.cpp",
      "td/telegram/StorageManager.cpp",
      "td/telegram/files/FileManager.cpp",
  };

  std::mt19937 rng(0xA53A6E11u);
  std::uniform_int_distribution<size_t> pick(0, files.size() - 1);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const auto &path = files[pick(rng)];
    auto source = load_repo_text(path);
    ASSERT_TRUE(!contains_identifier_assignment(source, "log_interface"));
  }
}

}  // namespace
