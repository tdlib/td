// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/logging_hardening_test_utils.h"

#include "td/telegram/Logging.h"

#include "td/utils/tests.h"

namespace {

using td::logging_hardening::test::contains_any;
using td::logging_hardening::test::contains_identifier_assignment;
using td::logging_hardening::test::load_repo_text;

TEST(LoggingStreamPointerIntegration, RuntimeLogStreamSwitchingRoundtripRemainsFunctional) {
  auto to_empty = td::Logging::set_current_stream(td::td_api::make_object<td::td_api::logStreamEmpty>());
  ASSERT_TRUE(to_empty.is_ok());

  auto empty_stream = td::Logging::get_current_stream();
  ASSERT_TRUE(empty_stream.is_ok());
  ASSERT_EQ(td::td_api::logStreamEmpty::ID, empty_stream.ok()->get_id());

  auto to_default = td::Logging::set_current_stream(td::td_api::make_object<td::td_api::logStreamDefault>());
  ASSERT_TRUE(to_default.is_ok());

  auto default_stream = td::Logging::get_current_stream();
  ASSERT_TRUE(default_stream.is_ok());
  ASSERT_EQ(td::td_api::logStreamDefault::ID, default_stream.ok()->get_id());
}

TEST(LoggingStreamPointerIntegration, BenchAndStealthMutationSitesUseHardenedHelpers) {
  const td::vector<td::string> files = {
      "tdutils/test/log.cpp",
      "test/stealth/test_tls_init_log_contract.cpp",
      "test/stealth/test_stealth_params_loader_reload_log_contract.cpp",
      "test/stealth/test_stream_transport_activation_fail_closed.cpp",
      "test/stealth/test_raw_connection_error_contract.cpp",
  };

  for (const auto &path : files) {
    auto source = load_repo_text(path);
    ASSERT_TRUE(!contains_identifier_assignment(source, "log_interface"));
    ASSERT_TRUE(contains_any(source, {"load_active_log_interface", "store_active_log_interface"}));
  }
}

}  // namespace
