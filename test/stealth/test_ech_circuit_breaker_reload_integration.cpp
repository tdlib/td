// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs

#include "td/mtproto/stealth/StealthParamsLoader.h"
#include "td/mtproto/stealth/StealthRuntimeParams.h"
#include "td/mtproto/stealth/TlsHelloProfileRegistry.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::get_runtime_ech_decision;
using td::mtproto::stealth::NetworkRouteHints;
using td::mtproto::stealth::note_runtime_ech_failure;
using td::mtproto::stealth::note_runtime_ech_success;
using td::mtproto::stealth::reset_runtime_ech_counters_for_tests;
using td::mtproto::stealth::reset_runtime_ech_failure_state_for_tests;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthParamsLoader;

class RuntimeGuard final {
 public:
  RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeGuard() {
    reset_runtime_ech_failure_state_for_tests();
    reset_runtime_ech_counters_for_tests();
    reset_runtime_stealth_params_for_tests();
  }
};

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "ech-cb-reload").move_as_ok();
  }

  ~ScopedTempDir() {
    td::rmrf(dir_).ignore();
  }

  td::Slice path() const {
    return dir_;
  }

 private:
  td::string dir_;
};

td::string join_path(td::Slice dir, td::Slice file_name) {
  td::string result = dir.str();
  result += TD_DIR_SLASH;
  result += file_name.str();
  return result;
}

void write_file(td::Slice path, td::Slice content) {
  auto file = FileFd::open(path.str(), FileFd::Write | FileFd::Create | FileFd::Truncate, 0600).move_as_ok();
  ASSERT_EQ(content.size(), file.write(content).move_as_ok());
  ASSERT_TRUE(file.sync().is_ok());
}

td::string make_runtime_config_json(td::Slice non_ru_ech_mode) {
  return "{"
         "\"version\":1,"
         "\"profile_weights\":{"
         "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
      "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\"},"
         "\"ru\":{\"ech_mode\":\"disabled\"},"
         "\"non_ru\":{\"ech_mode\":\"" +
         non_ru_ech_mode.str() +
         "\"}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":1,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":8192"
         "}";
}

NetworkRouteHints non_ru_route() {
  NetworkRouteHints hints;
  hints.is_known = true;
  hints.is_ru = false;
  return hints;
}

TEST(EchCircuitBreakerReloadIntegration, DestinationFailureStateDominatesPolicyFlipsUntilCleared) {
  RuntimeGuard guard;

  ScopedTempDir temp_dir;
  auto config_path = join_path(temp_dir.path(), "stealth-params.json");
  StealthParamsLoader loader(config_path);

  write_file(config_path, make_runtime_config_json("rfc9180_outer"));
  ASSERT_TRUE(loader.try_reload());

  const td::string destination = "reload-dominance.example.com";
  const td::int32 unix_time = 1712345678;

  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  note_runtime_ech_failure(destination, unix_time);
  auto blocked = get_runtime_ech_decision(destination, unix_time, non_ru_route());
  ASSERT_TRUE(blocked.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(blocked.disabled_by_circuit_breaker);

  // Flip to route-disabled and then back to route-enabled; per-destination CB state must survive.
  write_file(config_path, make_runtime_config_json("disabled"));
  ASSERT_TRUE(loader.try_reload());
  write_file(config_path, make_runtime_config_json("rfc9180_outer"));
  ASSERT_TRUE(loader.try_reload());

  auto still_blocked = get_runtime_ech_decision(destination, unix_time, non_ru_route());
  ASSERT_TRUE(still_blocked.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(still_blocked.disabled_by_circuit_breaker);

  note_runtime_ech_success(destination, unix_time);
  auto recovered = get_runtime_ech_decision(destination, unix_time, non_ru_route());
  ASSERT_TRUE(recovered.ech_mode == EchMode::Rfc9180Outer);
}

}  // namespace
