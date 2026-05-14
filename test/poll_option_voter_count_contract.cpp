// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

namespace {

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

td::string read_normalized(td::Slice path) {
  return normalize_for_contract(td::mtproto::test::read_repo_text_file(path));
}

}  // namespace

TEST(PollOptionVoterCountContract, StorePathMakesVoterCountOptionalViaDedicatedFlag) {
  auto source = read_normalized("td/telegram/PollOption.hpp");

  ASSERT_TRUE(source.contains("boolhas_no_voter_count=voter_count_==0;"));
  ASSERT_TRUE(source.contains("STORE_FLAG(has_no_voter_count);"));
  ASSERT_TRUE(source.contains("if(!has_no_voter_count){store(voter_count_,storer);}"));
}

TEST(PollOptionVoterCountContract, ParsePathReadsOptionalVoterCountViaDedicatedFlag) {
  auto source = read_normalized("td/telegram/PollOption.hpp");

  ASSERT_TRUE(source.contains("boolhas_no_voter_count;"));
  ASSERT_TRUE(source.contains("PARSE_FLAG(has_no_voter_count);"));
  ASSERT_TRUE(source.contains("if(!has_no_voter_count){parse(voter_count_,parser);}"));
}