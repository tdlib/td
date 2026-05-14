// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
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

}  // namespace

TEST(PollOptionVoterCountIntegration, StorePathWritesVoterCountFlagBeforeConditionalPayload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollOption.hpp");
  auto region = normalize_for_contract(
      extract_region(source, "void PollOption::store(StorerT &storer) const {", "template <class ParserT>"));

  auto local_flag_pos = region.find("boolhas_no_voter_count=voter_count_==0;");
  auto store_flag_pos = region.find("STORE_FLAG(has_no_voter_count);");
  auto text_pos = region.find("store(text_.text,storer);");
  auto payload_pos = region.find("if(!has_no_voter_count){store(voter_count_,storer);}");

  ASSERT_NE(td::string::npos, local_flag_pos);
  ASSERT_NE(td::string::npos, store_flag_pos);
  ASSERT_NE(td::string::npos, text_pos);
  ASSERT_NE(td::string::npos, payload_pos);

  ASSERT_TRUE(local_flag_pos < store_flag_pos);
  ASSERT_TRUE(store_flag_pos < text_pos);
  ASSERT_TRUE(text_pos < payload_pos);
}

TEST(PollOptionVoterCountIntegration, ParsePathReadsVoterCountFlagBeforeConditionalPayload) {
  auto source = td::mtproto::test::read_repo_text_file("td/telegram/PollOption.hpp");
  auto region =
      normalize_for_contract(extract_region(source, "void PollOption::parse(ParserT &parser) {", "}  // namespace td"));

  auto local_flag_pos = region.find("boolhas_no_voter_count;");
  auto parse_flag_pos = region.find("PARSE_FLAG(has_no_voter_count);");
  auto text_pos = region.find("parse(text_.text,parser);");
  auto payload_pos = region.find("if(!has_no_voter_count){parse(voter_count_,parser);}");

  ASSERT_NE(td::string::npos, local_flag_pos);
  ASSERT_NE(td::string::npos, parse_flag_pos);
  ASSERT_NE(td::string::npos, text_pos);
  ASSERT_NE(td::string::npos, payload_pos);

  ASSERT_TRUE(local_flag_pos < parse_flag_pos);
  ASSERT_TRUE(parse_flag_pos < text_pos);
  ASSERT_TRUE(text_pos < payload_pos);
}