// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "test/stealth/SourceContractFileReader.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

#if TD_PORT_POSIX
#include <unistd.h>
#endif

namespace {

#if TD_PORT_POSIX

td::string get_self_executable_path() {
  td::string path(4096, '\0');
  auto size = ::readlink("/proc/self/exe", &path[0], path.size() - 1);
  CHECK(size > 0);
  path.resize(static_cast<size_t>(size));
  return path;
}

td::string read_binary_file(td::Slice path) {
  auto content = td::mtproto::test::read_existing_binary_file(path.str());
  CHECK(!content.empty());
  return content;
}

const char *const kRetainedMaterialMarkers[] = {
    "MIIBCgKCAQEA6LszBcC1LGzyr992NzE0",
    "MIIBCgKCAQEAyMEdY1aR+sCR3ZSJrtzt",
    "MIIBCgKCAQEAyr+18Rex2ohtVy8sroGP",
};

TEST(SourceLayoutImageContract, ProcessImageContainsPrimaryRetainedMaterial) {
  auto image = read_binary_file(get_self_executable_path());
  ASSERT_TRUE(image.find("MIIBCgKCAQEA6LszBcC1LGzyr992NzE0") != td::string::npos);
}

TEST(SourceLayoutImageContract, ProcessImageContainsSecondaryRetainedMaterial) {
  auto image = read_binary_file(get_self_executable_path());
  ASSERT_TRUE(image.find("MIIBCgKCAQEAyMEdY1aR+sCR3ZSJrtzt") != td::string::npos);
}

TEST(SourceLayoutImageContract, ProcessImageContainsAuxiliaryRetainedMaterial) {
  auto image = read_binary_file(get_self_executable_path());
  ASSERT_TRUE(image.find("MIIBCgKCAQEAyr+18Rex2ohtVy8sroGP") != td::string::npos);
}

TEST(SourceLayoutImageContract, ProcessImageContainsAtLeastThreeRetainedBlockMarkers) {
  auto image = read_binary_file(get_self_executable_path());
  size_t marker_count = 0;
  for (auto marker : kRetainedMaterialMarkers) {
    marker_count += image.find(marker) != td::string::npos ? 1 : 0;
  }
  ASSERT_TRUE(marker_count >= 3);
}

#else

TEST(SourceLayoutImageContract, ProcessImageContainsPrimaryRetainedMaterial) {
  ASSERT_TRUE(true);
}

TEST(SourceLayoutImageContract, ProcessImageContainsSecondaryRetainedMaterial) {
  ASSERT_TRUE(true);
}

TEST(SourceLayoutImageContract, ProcessImageContainsAuxiliaryRetainedMaterial) {
  ASSERT_TRUE(true);
}

TEST(SourceLayoutImageContract, ProcessImageContainsAtLeastThreeRetainedBlockMarkers) {
  ASSERT_TRUE(true);
}

#endif

}  // namespace
