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

#include <sys/stat.h>

#if !_WIN32
#include <unistd.h>
#endif

namespace stealth_params_loader_relative_ancestor_security_adversarial {

using td::FileFd;
using td::mtproto::stealth::StealthParamsLoader;

class ScopedTempDir final {
 public:
  ScopedTempDir() {
    dir_ = td::mkdtemp(td::get_temporary_dir(), "stealth-loader-relative-ancestor-sec").move_as_ok();
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

td::Slice valid_config_json() {
  return R"json({"version":1,"profile_weights":{"chrome133":50,"chrome131":20,"chrome120":15,"firefox148":15,"safari26_3":20,"ios14":70,"android11_okhttp_advisory":30},"route_policy":{"unknown":{"ech_mode":"disabled"},"ru":{"ech_mode":"disabled"},"non_ru":{"ech_mode":"rfc9180_outer"}},"route_failure":{"ech_failure_threshold":3,"ech_disable_ttl_seconds":300.0,"persist_across_restart":true},"bulk_threshold_bytes":8192})json";
}

#if !_WIN32

class ScopedCurrentDirectory final {
 public:
  explicit ScopedCurrentDirectory(td::Slice path) {
    auto current_directory = td::realpath(".");
    CHECK(current_directory.is_ok());
    old_cwd_ = current_directory.move_as_ok();
    auto directory = path.str();
    CHECK(td::chdir(directory).is_ok());
  }

  ~ScopedCurrentDirectory() {
    if (!old_cwd_.empty()) {
      CHECK(td::chdir(old_cwd_).is_ok());
    }
  }

 private:
  td::string old_cwd_;
};

TEST(StealthParamsLoaderRelativeAncestorSecurityAdversarial,
     StrictLoadRejectsRelativePathUnderNonStickyWorldWritableAncestorDirectory) {
  ScopedTempDir temp_dir;

  auto unsafe_ancestor = join_path(temp_dir.path(), "unsafe_ancestor");
  ASSERT_EQ(0, ::mkdir(unsafe_ancestor.c_str(), 0700));
  ASSERT_EQ(0, ::chmod(unsafe_ancestor.c_str(), 0777));

  auto secure_parent = join_path(unsafe_ancestor, "cfg");
  ASSERT_EQ(0, ::mkdir(secure_parent.c_str(), 0700));

  auto path = join_path(secure_parent, "stealth-params.json");
  write_file(path, valid_config_json());

  ScopedCurrentDirectory cwd(secure_parent);
  auto result = StealthParamsLoader::try_load_strict("stealth-params.json");
  ASSERT_TRUE(result.is_error());
}

TEST(StealthParamsLoaderRelativeAncestorSecurityAdversarial,
     StrictLoadAllowsRelativePathUnderStickyWorldWritableAncestorDirectory) {
  ScopedTempDir temp_dir;

  auto sticky_ancestor = join_path(temp_dir.path(), "sticky_ancestor");
  ASSERT_EQ(0, ::mkdir(sticky_ancestor.c_str(), 0700));
  ASSERT_EQ(0, ::chmod(sticky_ancestor.c_str(), 01777));

  auto secure_parent = join_path(sticky_ancestor, "cfg");
  ASSERT_EQ(0, ::mkdir(secure_parent.c_str(), 0700));

  auto path = join_path(secure_parent, "stealth-params.json");
  write_file(path, valid_config_json());

  ScopedCurrentDirectory cwd(secure_parent);
  auto result = StealthParamsLoader::try_load_strict("./stealth-params.json");
  ASSERT_TRUE(result.is_ok());
}

#endif  // !_WIN32

}  // namespace stealth_params_loader_relative_ancestor_security_adversarial