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
using td::mtproto::stealth::StealthParamsLoader;

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-profile-weight-bridge-light-fuzz").move_as_ok();
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

td::string make_legacy_windows_flat_config(td::uint32 seed) {
  td::uint32 chrome133 = seed % 100;
  td::uint32 firefox148 = 100 - chrome133;
  if (firefox148 == 0) {
    firefox148 = 1;
    chrome133 = 99;
  }

  td::string json = "{";
  json += "\"version\":1,";
  json += "\"platform_hints\":{\"device_class\":\"desktop\",\"mobile_os\":\"none\",\"desktop_os\":\"windows\"},";
  json += "\"profile_weights\":{";
  json += "\"chrome133\":" + td::to_string(chrome133) + ",";
  json += "\"chrome131\":0,";
  json += "\"chrome120\":0,";
  json += "\"firefox148\":" + td::to_string(firefox148) + ",";
  json += "\"safari26_3\":0,";
  json += "\"ios14\":70,";
  json += "\"android11_okhttp_advisory\":30},";
  json += "\"route_policy\":{";
  json += "\"unknown\":{\"ech_mode\":\"disabled\"},";
  json += "\"ru\":{\"ech_mode\":\"disabled\"},";
  json += "\"non_ru\":{\"ech_mode\":\"rfc9180_outer\"}},";
  json += "\"route_failure\":{";
  json += "\"ech_failure_threshold\":3,";
  json += "\"ech_disable_ttl_seconds\":300.0,";
  json += "\"persist_across_restart\":true},";
  json += "\"bulk_threshold_bytes\":8192";
  json += "}";
  return json;
}

TEST(StealthParamsLoaderProfileWeightBridgeLightFuzz,
     LegacyFlatSchemaWindowsLoadsWithoutFailingPlatformAllowedWeightGate) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  for (td::uint32 seed = 1; seed <= 64; seed++) {
    auto config = make_legacy_windows_flat_config(seed);
    write_file(path, config);

    auto result = StealthParamsLoader::try_load_strict(path);
    ASSERT_TRUE(result.is_ok());

    auto params = result.move_as_ok();
    ASSERT_EQ(params.profile_weights.chrome133, params.profile_weights.chrome147_windows);
    ASSERT_EQ(params.profile_weights.firefox148, params.profile_weights.firefox149_windows);
  }
}

}  // namespace
