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

TEST(PollOptionVoterCountStress, RepeatedSourceReadsKeepOptionalVoterCountSerializationStable) {
  constexpr int kIterations = 5000;
  td::uint32 checksum = 0;

  for (int i = 0; i < kIterations; i++) {
    auto source = read_normalized("td/telegram/PollOption.hpp");

    ASSERT_TRUE(source.contains("boolhas_no_voter_count=voter_count_==0;"));
    ASSERT_TRUE(source.contains("STORE_FLAG(has_no_voter_count);"));
    ASSERT_TRUE(source.contains("if(!has_no_voter_count){store(voter_count_,storer);}"));
    ASSERT_TRUE(source.contains("boolhas_no_voter_count;"));
    ASSERT_TRUE(source.contains("PARSE_FLAG(has_no_voter_count);"));
    ASSERT_TRUE(source.contains("if(!has_no_voter_count){parse(voter_count_,parser);}"));

    ASSERT_FALSE(source.contains("if(has_no_voter_count){store(voter_count_,storer);}"));
    ASSERT_FALSE(source.contains("if(has_no_voter_count){parse(voter_count_,parser);}"));
    ASSERT_FALSE(source.contains("if(!has_no_data){store(voter_count_,storer);}"));
    ASSERT_FALSE(source.contains("if(!has_no_data){parse(voter_count_,parser);}"));
    ASSERT_FALSE(source.contains("boolhas_voter_count=voter_count_!=0;"));
    ASSERT_FALSE(source.contains("STORE_FLAG(has_voter_count);"));
    ASSERT_FALSE(source.contains("PARSE_FLAG(has_voter_count);"));

    checksum += static_cast<td::uint32>(source.size() + static_cast<size_t>(i));
  }

  ASSERT_TRUE(checksum != 0);
}