//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/stealth/StealthParamsLoader.h"

#include "td/utils/common.h"
#include "td/utils/port/FileFd.h"
#include "td/utils/port/path.h"
#include "td/utils/tests.h"

#include <sys/stat.h>
#include <unistd.h>

namespace {

using td::FileFd;
using td::mtproto::stealth::StealthParamsLoader;

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-sec").move_as_ok();
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

TEST(StealthParamsLoaderSecurity, StrictLoadRejectsSymlinkedConfigPath) {
  ScopedTempDir temp_dir;
  auto target_path = join_path(temp_dir.path(), "target.json");
  auto symlink_path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(target_path,
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
  ASSERT_EQ(0, ::symlink(target_path.c_str(), symlink_path.c_str()));

  auto result = StealthParamsLoader::try_load_strict(symlink_path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderSecurity, StrictLoadRejectsWorldWritableConfigPath) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

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
  ASSERT_EQ(0, ::chmod(path.c_str(), 0666));

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderSecurity, StrictLoadRejectsGroupWritableConfigPath) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

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
  ASSERT_EQ(0, ::chmod(path.c_str(), 0660));

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderSecurity, StrictLoadRejectsGroupWritableParentDirectory) {
  ScopedTempDir temp_dir;
  auto secure_dir = join_path(temp_dir.path(), "cfg");
  ASSERT_EQ(0, ::mkdir(secure_dir.c_str(), 0700));

  auto path = join_path(secure_dir, "stealth-params.json");
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
  ASSERT_EQ(0, ::chmod(secure_dir.c_str(), 0770));

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderSecurity, StrictLoadRejectsDuplicateTopLevelFields) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

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
             "\"bulk_threshold_bytes\":16384}");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
}

}  // namespace