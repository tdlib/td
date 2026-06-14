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
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-profile-weight-bridge-adversarial").move_as_ok();
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

TEST(StealthParamsLoaderProfileWeightBridgeAdversarial,
     StrictLoadRejectsWindowsPlatformWhenExplicitWindowsLanesAreZeroed) {
  ScopedTempDir temp_dir;
  auto path = join_path(temp_dir.path(), "stealth-params.json");

  write_file(path,
             R"json({
  "version": 1,
  "platform_hints": {
    "device_class": "desktop",
    "mobile_os": "none",
    "desktop_os": "windows"
  },
  "profile_weights": {
    "chrome133": 50,
    "chrome131": 20,
    "chrome120": 15,
    "chrome147_windows": 0,
    "chrome147_ios_chromium": 30,
    "firefox148": 15,
    "firefox149_windows": 0,
    "safari26_3": 20,
    "ios14": 70,
    "android11_okhttp_advisory": 30
  },
  "route_policy": {
    "unknown": {"ech_mode": "disabled"},
    "ru": {"ech_mode": "disabled"},
    "non_ru": {"ech_mode": "rfc9180_outer"}
  },
  "route_failure": {
    "ech_failure_threshold": 3,
    "ech_disable_ttl_seconds": 300.0,
    "persist_across_restart": true
  },
  "bulk_threshold_bytes": 8192
})json");

  auto result = StealthParamsLoader::try_load_strict(path);
  ASSERT_TRUE(result.is_error());
  ASSERT_TRUE(result.error().public_message().find(
                  "platform_hints must keep at least one allowed profile weight enabled") != td::string::npos);
}

}  // namespace
