// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/mtproto/stealth/StealthRuntimeParams.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::get_runtime_stealth_params_snapshot;
using td::mtproto::stealth::reset_runtime_stealth_params_for_tests;
using td::mtproto::stealth::StealthParamsLoader;

class RuntimeParamsGuard final {
 public:
  RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }

  ~RuntimeParamsGuard() {
    reset_runtime_stealth_params_for_tests();
  }
};

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-fs").move_as_ok();
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

td::string valid_config_json() {
  return "{"
         "\"version\":1,"
         "\"profile_weights\":{"
         "\"chrome133\":50,\"chrome131\":20,\"chrome120\":15,\"firefox148\":15,"
         "\"safari26_3\":20,\"ios14\":70,\"android11_okhttp_advisory\":30},"
         "\"route_policy\":{"
         "\"unknown\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"ru\":{\"ech_mode\":\"disabled\",\"allow_quic\":false},"
         "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\",\"allow_quic\":false}},"
         "\"route_failure\":{"
         "\"ech_failure_threshold\":3,\"ech_disable_ttl_seconds\":300.0,\"persist_across_restart\":true},"
         "\"bulk_threshold_bytes\":12288}";
}

TEST(StealthParamsLoaderFilesystemFailClosed, StrictLoadRejectsEmptyConfigFile) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path, "");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderFilesystemFailClosed, StrictLoadRejectsOversizedConfigFile) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  td::string oversized_payload(2u * 1024u * 1024u, 'A');
  write_file(path, oversized_payload);

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderFilesystemFailClosed, StrictLoadRejectsDirectoryPath) {
  ScopedTempDir temp_dir;

  auto result = StealthParamsLoader::try_load_strict(temp_dir.path());
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderFilesystemFailClosed, ReloadKeepsLastGoodSnapshotOnOversizedConfig) {
  RuntimeParamsGuard guard;
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path, valid_config_json());
  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), get_runtime_stealth_params_snapshot().bulk_threshold_bytes);

  td::string oversized_payload(2u * 1024u * 1024u, 'B');
  write_file(path, oversized_payload);

  ASSERT_FALSE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), get_runtime_stealth_params_snapshot().bulk_threshold_bytes);
}

}  // namespace