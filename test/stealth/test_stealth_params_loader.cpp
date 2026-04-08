// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::EchMode;
using td::mtproto::stealth::StealthParamsLoader;

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader").move_as_ok();
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

void write_file(td::Slice path, td::Slice content) {
  auto file = FileFd::open(path.str(), FileFd::Write | FileFd::Create | FileFd::Truncate, 0600).move_as_ok();
  ASSERT_EQ(content.size(), file.write(content).move_as_ok());
  ASSERT_TRUE(file.sync().is_ok());
}

td::string join_path(td::Slice dir, td::Slice file_name) {
  td::string result = dir.str();
  result += TD_DIR_SLASH;
  result += file_name.str();
  return result;
}

td::string config_path(td::Slice dir) {
  return join_path(dir, "stealth-params.json");
}

TEST(StealthParamsLoader, StrictLoadAcceptsValidMinimalConfig) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":4,\"ech_disable_ttl_seconds\":600.0,\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(static_cast<size_t>(16384), params.bulk_threshold_bytes);
  ASSERT_EQ(4u, params.route_failure.ech_failure_threshold);
  ASSERT_TRUE(params.route_policy.non_ru.ech_mode == EchMode::Rfc9180Outer);
}

TEST(StealthParamsLoader, StrictLoadMissingConfigReturnsDefaults) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(static_cast<size_t>(8192), params.bulk_threshold_bytes);
  ASSERT_EQ(3u, params.route_failure.ech_failure_threshold);
  ASSERT_TRUE(params.route_policy.unknown.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(params.route_policy.ru.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(params.route_policy.non_ru.ech_mode == EchMode::Rfc9180Outer);
}

TEST(StealthParamsLoader, StrictLoadParsesIptOverrideBlock) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"ipt\":{"
             "\"burst_mu_ms\":4.25,\"burst_sigma\":1.1,\"burst_max_ms\":240.0,"
             "\"idle_alpha\":1.8,\"idle_scale_ms\":650.0,\"idle_max_ms\":3400.0,"
             "\"p_burst_stay\":0.91,\"p_idle_to_burst\":0.22},"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":4,\"ech_disable_ttl_seconds\":600.0,\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(4.25, params.ipt_params.burst_mu_ms);
  ASSERT_EQ(1.1, params.ipt_params.burst_sigma);
  ASSERT_EQ(240.0, params.ipt_params.burst_max_ms);
  ASSERT_EQ(1.8, params.ipt_params.idle_alpha);
  ASSERT_EQ(650.0, params.ipt_params.idle_scale_ms);
  ASSERT_EQ(3400.0, params.ipt_params.idle_max_ms);
  ASSERT_EQ(0.91, params.ipt_params.p_burst_stay);
  ASSERT_EQ(0.22, params.ipt_params.p_idle_to_burst);
}

TEST(StealthParamsLoader, StrictLoadAcceptsPlanStyleRouteFailureFields) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":4,\"ech_disable_ttl_sec\":600.0,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\",\"hello_timeout\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(4u, params.route_failure.ech_failure_threshold);
  ASSERT_EQ(600.0, params.route_failure.ech_disable_ttl_seconds);
  ASSERT_TRUE(params.route_failure.persist_across_restart);
}

TEST(StealthParamsLoader, StrictLoadRejectsEmptyPlanFailureKinds) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":4,\"ech_disable_ttl_sec\":600.0,"
             "\"failure_kinds\":[],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoader, StrictLoadAcceptsPlanStyleRoutePolicyNames) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru_egress\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru_egress\":{\"ech_mode\":\"grease_draft17\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":4,\"ech_disable_ttl_sec\":600.0,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_TRUE(params.route_policy.unknown.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(params.route_policy.ru.ech_mode == EchMode::Disabled);
  ASSERT_TRUE(params.route_policy.non_ru.ech_mode == EchMode::Rfc9180Outer);
}

TEST(StealthParamsLoader, StrictLoadParsesDrsOverrideBlock) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"drs\":{"
             "\"slow_start\":{\"bins\":[{\"lo\":1200,\"hi\":1460,\"weight\":3},{\"lo\":1461,\"hi\":2200,\"weight\":2}],"
             "\"max_repeat_run\":4,\"local_jitter\":24},"
             "\"congestion_open\":{\"bins\":[{\"lo\":2200,\"hi\":4800,\"weight\":3},{\"lo\":4801,\"hi\":7600,"
             "\"weight\":2}],\"max_repeat_run\":5,\"local_jitter\":32},"
             "\"steady_state\":{\"bins\":[{\"lo\":4096,\"hi\":8192,\"weight\":4},{\"lo\":8193,\"hi\":12000,\"weight\":"
             "1}],\"max_repeat_run\":6,\"local_jitter\":48},"
             "\"slow_start_records\":5,\"congestion_bytes\":65536,\"idle_reset_ms_min\":333,\"idle_reset_ms_max\":1444,"
             "\"min_payload_cap\":900,\"max_payload_cap\":12000},"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":4,\"ech_disable_ttl_sec\":600.0,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_EQ(5, params.drs_policy.slow_start_records);
  ASSERT_EQ(65536, params.drs_policy.congestion_bytes);
  ASSERT_EQ(333, params.drs_policy.idle_reset_ms_min);
  ASSERT_EQ(1444, params.drs_policy.idle_reset_ms_max);
  ASSERT_EQ(900, params.drs_policy.min_payload_cap);
  ASSERT_EQ(12000, params.drs_policy.max_payload_cap);
  ASSERT_EQ(2u, params.drs_policy.slow_start.bins.size());
  ASSERT_EQ(2u, params.drs_policy.congestion_open.bins.size());
  ASSERT_EQ(2u, params.drs_policy.steady_state.bins.size());
}

TEST(StealthParamsLoader, StrictLoadParsesActivePolicy) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"active_policy\":\"ru_egress\","
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru_egress\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru_egress\":{\"ech_mode\":\"grease_draft17\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":4,\"ech_disable_ttl_sec\":600.0,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_ok());

  auto params = result.move_as_ok();
  ASSERT_TRUE(params.active_policy == td::mtproto::stealth::RuntimeActivePolicy::RuEgress);
}

TEST(StealthParamsLoader, StrictLoadRejectsUnknownActivePolicy) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"active_policy\":\"auto\","
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru_egress\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru_egress\":{\"ech_mode\":\"grease_draft17\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_fail_open_threshold\":4,\"ech_disable_ttl_sec\":600.0,"
             "\"failure_kinds\":[\"tcp_reset_after_ch\"],"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoader, StrictLoadRejectsUnknownTopLevelKey) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":8192,"
             "\"typo_bulk_threshold\":9000}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoader, ReloadKeepsLastKnownGoodSnapshotOnMalformedUpdate) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":8192}");

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(8192), loader.get_snapshot().bulk_threshold_bytes);

  write_file(path, "{");

  ASSERT_FALSE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(8192), loader.get_snapshot().bulk_threshold_bytes);
}

TEST(StealthParamsLoader, ReloadMissingConfigKeepsLastKnownGoodSnapshot) {
  ScopedTempDir temp_dir;
  auto path = config_path(temp_dir.path());
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":4,\"ech_disable_ttl_seconds\":600.0,\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":16384}");

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(16384), loader.get_snapshot().bulk_threshold_bytes);

  ASSERT_TRUE(td::unlink(path).is_ok());

  ASSERT_FALSE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(16384), loader.get_snapshot().bulk_threshold_bytes);
}

}  // namespace