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
#include "td/utils/Time.h"

namespace {

using td::FileFd;
using td::mtproto::stealth::StealthParamsLoader;

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-cooldown").move_as_ok();
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

td::string valid_loader_config() {
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

TEST(StealthParamsLoaderCooldown, ReloadBelowFailureThresholdStillAcceptsImmediateRecovery) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");
  write_file(path, valid_loader_config());

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), loader.get_snapshot().bulk_threshold_bytes);

  write_file(path, "{");
  for (int i = 0; i < 4; i++) {
    ASSERT_FALSE(loader.try_reload());
  }

  write_file(path,
             "{"
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
             "\"bulk_threshold_bytes\":16384}");

  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(16384), loader.get_snapshot().bulk_threshold_bytes);
}

TEST(StealthParamsLoaderCooldown, ReloadEntersCooldownAfterRepeatedMalformedUpdates) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");
  write_file(path, valid_loader_config());

  StealthParamsLoader loader(path);
  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), loader.get_snapshot().bulk_threshold_bytes);

  write_file(path, "{");
  for (int i = 0; i < 5; i++) {
    ASSERT_FALSE(loader.try_reload());
  }

  write_file(path,
             "{"
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
             "\"bulk_threshold_bytes\":16384}");

  ASSERT_FALSE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(12288), loader.get_snapshot().bulk_threshold_bytes);

  td::Time::jump_in_future(td::Time::now() + 61.0);

  ASSERT_TRUE(loader.try_reload());
  ASSERT_EQ(static_cast<size_t>(16384), loader.get_snapshot().bulk_threshold_bytes);
}

}  // namespace