// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/ScopeGuard.h"
#include "td/utils/tests.h"
#include "td/utils/Time.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::StealthParamsLoader;

class CapturingLog final : public td::LogInterface {
 public:
  void do_append(int log_level, td::CSlice slice) final {
    (void)log_level;
    entries_ += slice.str();
    entries_ += '\n';
  }

  bool contains(td::Slice needle) const {
    return entries_.find(needle.str()) != td::string::npos;
  }

  const td::string &joined() const {
    return entries_;
  }

 private:
  td::string entries_;
};

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-log-contract").move_as_ok();
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

td::string valid_loader_config(td::Slice bulk_threshold_bytes) {
  td::string config =
      "{"
      "\"version\":1,"
      "\"profile_weights\":{"
      "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
      "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
      "\"route_policy\":{"
      "\"unknown\":{\"ech_mode\":\"disabled\"},"
      "\"ru\":{\"ech_mode\":\"disabled\"},"
      "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
      "\"route_failure\":{"
      "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,"
      "\"persist_across_restart\":true},"
      "\"bulk_threshold_bytes\":";
  config += bulk_threshold_bytes.str();
  config += "}";
  return config;
}

TEST(StealthParamsLoaderReloadLogContract, ReloadFailureLogContainsActionableDiagnosticsAndCooldownState) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");
  write_file(path, valid_loader_config("12288"));

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());

  write_file(path, "{");

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  ASSERT_FALSE(loader.try_reload());

  const auto &logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload failed") != td::string::npos);
  ASSERT_TRUE(logs.find("[stage:load_strict]") != td::string::npos);
  ASSERT_TRUE(logs.find("[status_code:") != td::string::npos);
  ASSERT_TRUE(logs.find("[status_message:") != td::string::npos);
  ASSERT_TRUE(logs.find("[remediation_hint:") != td::string::npos);
  ASSERT_TRUE(logs.find("[consecutive_failures:1]") != td::string::npos);
  ASSERT_TRUE(logs.find("[cooldown_active:false]") != td::string::npos);
}

TEST(StealthParamsLoaderReloadLogContract, MissingPathFailureLogContainsActionableRemediationHint) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "missing-stealth-params.json");

  StealthParamsLoader loader(path);

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  ASSERT_FALSE(loader.try_reload());

  const auto &logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload failed") != td::string::npos);
  ASSERT_TRUE(logs.find("[stage:path_missing]") != td::string::npos);
  ASSERT_TRUE(logs.find("[remediation_hint:") != td::string::npos);
}

TEST(StealthParamsLoaderReloadLogContract, UnknownRootFieldLogPreservesExactFieldAndSchemaRemediation) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\"},"
             "\"ru\":{\"ech_mode\":\"disabled\"},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":12288,"
             "\"bulk_threshold_bites\":12288}");

  StealthParamsLoader loader(path);

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  ASSERT_FALSE(loader.try_reload());

  const auto &logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload failed") != td::string::npos);
  ASSERT_TRUE(logs.find("[stage:load_strict]") != td::string::npos);
  ASSERT_TRUE(logs.find("root has unknown field \"bulk_threshold_bites\"") != td::string::npos);
  ASSERT_TRUE(logs.find("remove unsupported field names and keep the exact version=1 stealth params schema") !=
              td::string::npos);
}

TEST(StealthParamsLoaderReloadLogContract, BoundsViolationLogIncludesFieldNameAndBoundsRemediation) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");
  write_file(path, valid_loader_config("128"));

  StealthParamsLoader loader(path);

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  ASSERT_FALSE(loader.try_reload());

  const auto &logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload failed") != td::string::npos);
  ASSERT_TRUE(logs.find("[stage:load_strict]") != td::string::npos);
  ASSERT_TRUE(logs.find("bulk_threshold_bytes is out of allowed bounds") != td::string::npos);
  ASSERT_TRUE(logs.find("restore stealth params numeric fields within documented fail-closed bounds") !=
              td::string::npos);
}

TEST(StealthParamsLoaderReloadLogContract, OversizedStatusMessageFallsBackToSafeLogText) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  td::string huge_field_name(320, 'x');
  write_file(path,
             "{"
             "\"version\":1,"
             "\"profile_weights\":{"
             "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
             "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
             "\"route_policy\":{"
             "\"unknown\":{\"ech_mode\":\"disabled\"},"
             "\"ru\":{\"ech_mode\":\"disabled\"},"
             "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},"
             "\"route_failure\":{"
             "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,"
             "\"persist_across_restart\":true},"
             "\"bulk_threshold_bytes\":12288,"
             "\"" +
                 huge_field_name + "\":1}");

  StealthParamsLoader loader(path);

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  ASSERT_FALSE(loader.try_reload());

  const auto &logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload failed") != td::string::npos);
  ASSERT_TRUE(logs.find("stealth params reload rejected configuration; see stage/remediation_hint for triage") !=
              td::string::npos);
  ASSERT_TRUE(logs.find(huge_field_name) == td::string::npos);
}

TEST(StealthParamsLoaderReloadLogContract, CooldownTransitionAndRecoveryLogsAreExplicit) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");
  write_file(path, valid_loader_config("12288"));

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());

  write_file(path, "{");

  CapturingLog capture;
  auto *old_sink = td::load_active_log_interface();
  auto old_verbosity = GET_VERBOSITY_LEVEL();
  td::store_active_log_interface(&capture);
  SET_VERBOSITY_LEVEL(VERBOSITY_NAME(DEBUG));
  SCOPE_EXIT {
    SET_VERBOSITY_LEVEL(old_verbosity);
    td::store_active_log_interface(old_sink);
  };

  for (int i = 0; i < 5; i++) {
    ASSERT_FALSE(loader.try_reload());
  }
  ASSERT_FALSE(loader.try_reload());

  auto logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload failed") != td::string::npos);
  ASSERT_TRUE(logs.find("[consecutive_failures:5]") != td::string::npos);
  ASSERT_TRUE(logs.find("[entered_cooldown:true]") != td::string::npos);
  ASSERT_TRUE(logs.find("Stealth params reload skipped due to cooldown") != td::string::npos);

  write_file(path, valid_loader_config("16384"));
  td::Time::jump_in_future(td::Time::now() + 61.0);
  ASSERT_TRUE(loader.try_reload());

  logs = capture.joined();
  ASSERT_TRUE(logs.find("Stealth params reload succeeded") != td::string::npos);
  ASSERT_TRUE(logs.find("[previous_failures:5]") != td::string::npos);
}

}  // namespace
