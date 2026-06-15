// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <string_view>

namespace {

td::string extract_region(std::string_view source, td::Slice begin_marker, td::Slice end_marker) {
  auto begin = source.find(begin_marker.str());
  CHECK(begin != td::string::npos);
  auto end = source.find(end_marker.str(), begin + begin_marker.size());
  CHECK(end != td::string::npos);
  CHECK(end > begin);
  return td::string(source.substr(begin, end - begin));
}

td::string normalize_for_contract(td::Slice source) {
  td::string normalized;
  normalized.reserve(source.size());
  for (auto c : source) {
    if (auto byte = static_cast<unsigned char>(c); byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n') {
      continue;
    }
    normalized.push_back(c);
  }
  return normalized;
}

// Upstream tdlib e95e1fd0d ("Fix DialogAction comparison"), adapted as a fork-local equivalent.
// Upstream also compares a RichMessage `message_` field; this fork does not carry the rich-message
// feature (no RichMessage member on DialogAction), so the local-equivalent compares only the fields
// the fork has: random_id_ and text_.
// Contract: two DialogActions that differ only in random_id_ or text_ must NOT compare equal
// (the prior comparison ignored both, conflating distinct typing/draft actions).
TEST(DialogActionEqualityFieldsContract, EqualityComparesRandomIdAndText) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/DialogAction.h");
  auto region =
      extract_region(source, "friend bool operator==(const DialogAction &lhs, const DialogAction &rhs) {", "}");
  auto normalized = normalize_for_contract(region);

  ASSERT_TRUE(normalized.find("lhs.emoji_==rhs.emoji_&&lhs.random_id_==rhs.random_id_&&lhs.text_==rhs.text_") !=
              td::string::npos);
  // Guard against an accidental cherry-pick of the absent rich-message field.
  ASSERT_TRUE(normalized.find("message_") == td::string::npos);
}

}  // namespace
