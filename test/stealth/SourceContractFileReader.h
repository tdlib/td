// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#pragma once

#include "td/utils/common.h"

#include <array>
#include <cstdio>

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

namespace td {
namespace mtproto {
namespace test {

inline td::string read_existing_binary_file(const td::string &path) {
  auto *file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return {};
  }
  if (std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return {};
  }
  auto size = std::ftell(file);
  if (size < 0) {
    std::fclose(file);
    return {};
  }
  std::rewind(file);

  td::string content(static_cast<size_t>(size), '\0');
  if (!content.empty()) {
    auto read_size = std::fread(content.data(), 1, content.size(), file);
    if (read_size != content.size()) {
      std::fclose(file);
      return {};
    }
  }
  std::fclose(file);
  return content;
}

inline td::string read_repo_text_file(td::Slice path) {
  const td::string path_str = path.str();
  const td::string repo_root = TELEMT_TEST_REPO_ROOT;
  if (!repo_root.empty()) {
    const td::string anchored_path = repo_root + "/" + path_str;
    if (auto content = read_existing_binary_file(anchored_path); !content.empty()) {
      return content;
    }
    LOG(FATAL) << "Failed to open source file from repository root path: " << anchored_path;
    UNREACHABLE();
  }

  const std::array<td::string, 5> candidates = {
      path_str,
      td::string("./") + path_str,
      td::string("../") + path_str,
      td::string("../../") + path_str,
      td::string("../../../") + path_str,
  };

  for (const auto &candidate : candidates) {
    if (auto content = read_existing_binary_file(candidate); !content.empty()) {
      return content;
    }
  }

  LOG(FATAL) << "Failed to open source file from repository path: " << path;
  UNREACHABLE();
}

}  // namespace test
}  // namespace mtproto
}  // namespace td
