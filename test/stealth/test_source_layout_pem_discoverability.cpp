// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include <fstream>
#include <iterator>

namespace {

td::string read_source(td::Slice path) {
  std::ifstream input(path.str(), std::ios::binary);
  CHECK(input.is_open());
  return td::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

size_t count_begin_markers(const td::string &source) {
  size_t count = 0;
  size_t position = 0;
  while (true) {
    auto rsa_pos = source.find("BEGIN RSA", position);
    auto pub_pos = source.find("BEGIN PUBLIC KEY", position);
    auto next_pos = std::min(rsa_pos, pub_pos);
    if (next_pos == td::string::npos) {
      break;
    }
    count++;
    position = next_pos + 1;
  }
  return count;
}

size_t count_end_markers(const td::string &source) {
  size_t count = 0;
  size_t position = 0;
  while (true) {
    auto rsa_pos = source.find("END RSA", position);
    auto pub_pos = source.find("END PUBLIC KEY", position);
    auto next_pos = std::min(rsa_pos, pub_pos);
    if (next_pos == td::string::npos) {
      break;
    }
    count++;
    position = next_pos + 1;
  }
  return count;
}

TEST(BundleRecoveryDiscoverability, KeySharedMainSourceExposesTwoBlocks) {
  auto source = read_source("td/telegram/net/PublicRsaKeySharedMain.cpp");
  ASSERT_EQ(static_cast<size_t>(2), count_begin_markers(source));
  ASSERT_EQ(static_cast<size_t>(2), count_end_markers(source));
}

TEST(BundleRecoveryDiscoverability, ConfigManagerSourceExposesOneBlock) {
  auto source = read_source("td/telegram/ConfigManager.cpp");
  ASSERT_EQ(static_cast<size_t>(1), count_begin_markers(source));
  ASSERT_EQ(static_cast<size_t>(1), count_end_markers(source));
}

TEST(BundleRecoveryDiscoverability, TotalBlockCountAcrossRetainedSourcesIsThree) {
  auto key_source = read_source("td/telegram/net/PublicRsaKeySharedMain.cpp");
  auto config_source = read_source("td/telegram/ConfigManager.cpp");
  auto total_begin = count_begin_markers(key_source) + count_begin_markers(config_source);
  auto total_end = count_end_markers(key_source) + count_end_markers(config_source);
  ASSERT_EQ(static_cast<size_t>(3), total_begin);
  ASSERT_EQ(static_cast<size_t>(3), total_end);
}

}  // namespace
