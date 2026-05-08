// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

#include <random>

namespace {

using td::logging_hardening::test::contains_identifier_assignment;
using td::logging_hardening::test::load_repo_text;

TEST(LoggingStreamPointerLightFuzz, RandomizedSourceSamplingRejectsLegacyAssignments) {
  const td::vector<td::string> files = {
      "tdutils/td/utils/logging.cpp",
      "td/telegram/Logging.cpp",
      "td/telegram/cli.cpp",
      "tdutils/test/log.cpp",
      "test/stealth/test_tls_init_log_contract.cpp",
      "test/stealth/test_stealth_params_loader_reload_log_contract.cpp",
      "test/stealth/test_stream_transport_activation_fail_closed.cpp",
      "test/stealth/test_raw_connection_error_contract.cpp",
      // Production callsites that were missing from the light_fuzz pool
      "tdutils/td/utils/check.cpp",
      "td/telegram/StorageManager.cpp",
      "td/telegram/files/FileManager.cpp",
  };

  std::mt19937 rng(0x7BB5E2F0u);
  std::uniform_int_distribution<size_t> pick(0, files.size() - 1);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    const auto &path = files[pick(rng)];
    auto source = load_repo_text(path);
    ASSERT_TRUE(!contains_identifier_assignment(source, "log_interface"));
  }
}

TEST(LoggingStreamPointerLightFuzz, RandomizedRuntimeStreamSwitchingStaysInRecognizedModes) {
  std::mt19937 rng(0x2DD91F84u);
  std::uniform_int_distribution<int> op(0, 1);

  constexpr int kIterations = 10000;
  for (int i = 0; i < kIterations; i++) {
    if (op(rng) == 0) {
      ASSERT_TRUE(td::Logging::set_current_stream(td::td_api::make_object<td::td_api::logStreamDefault>()).is_ok());
    } else {
      ASSERT_TRUE(td::Logging::set_current_stream(td::td_api::make_object<td::td_api::logStreamEmpty>()).is_ok());
    }
    auto current = td::Logging::get_current_stream();
    ASSERT_TRUE(current.is_ok());
    ASSERT_TRUE(current.ok()->get_id() == td::td_api::logStreamDefault::ID ||
                current.ok()->get_id() == td::td_api::logStreamEmpty::ID);
  }
}

}  // namespace
