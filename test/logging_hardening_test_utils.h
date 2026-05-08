// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT

#pragma once

#include "td/utils/common.h"

#include "test/stealth/SourceContractFileReader.h"

#include <cctype>

#ifndef TELEMT_TEST_REPO_ROOT
#define TELEMT_TEST_REPO_ROOT ""
#endif

namespace td {
namespace logging_hardening {
namespace test {

inline td::string load_repo_text(td::Slice relative_path) {
  CHECK(!relative_path.empty());
  return td::mtproto::test::read_repo_text_file(relative_path);
}

inline td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    auto byte = static_cast<unsigned char>(c);
    if (byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

inline td::string extract_source_region(td::Slice source, td::Slice begin_marker, td::Slice end_marker) {
  auto source_text = source.str();
  auto begin = source_text.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source_text.find(end_marker.str(), begin);
  CHECK(end != td::string::npos);
  CHECK(begin < end);
  return source_text.substr(begin, end - begin);
}

inline td::string repo_root() {
  return td::string(TELEMT_TEST_REPO_ROOT);
}

inline td::string repo_path(td::Slice relative_path) {
  auto path = repo_root();
  path += '/';
  path += relative_path.str();
  return path;
}

inline bool contains_any(td::Slice source, const td::vector<td::string> &needles) {
  auto source_text = source.str();
  for (const auto &needle : needles) {
    if (source_text.find(needle) != td::string::npos) {
      return true;
    }
  }
  return false;
}

inline size_t count_occurrences(td::Slice source, td::Slice needle) {
  if (needle.empty()) {
    return 0;
  }

  auto source_text = source.str();
  auto needle_text = needle.str();
  size_t count = 0;
  size_t position = 0;
  while (true) {
    position = source_text.find(needle_text, position);
    if (position == td::string::npos) {
      break;
    }
    count++;
    position += needle.size();
  }
  return count;
}

inline bool contains_identifier_assignment(td::Slice source, td::Slice identifier) {
  if (identifier.empty()) {
    return false;
  }

  auto source_text = source.str();
  auto identifier_text = identifier.str();
  size_t position = 0;
  while (true) {
    position = source_text.find(identifier_text, position);
    if (position == td::string::npos) {
      break;
    }

    const bool has_left_boundary =
        position == 0 ||
        (!std::isalnum(static_cast<unsigned char>(source_text[position - 1])) && source_text[position - 1] != '_');

    size_t right = position + identifier_text.size();
    while (right < source_text.size() && (source_text[right] == ' ' || source_text[right] == '\t')) {
      right++;
    }
    const bool has_assignment = right < source_text.size() && source_text[right] == '=';

    if (has_left_boundary && has_assignment) {
      return true;
    }

    position += identifier_text.size();
  }
  return false;
}

}  // namespace test
}  // namespace logging_hardening
}  // namespace td
