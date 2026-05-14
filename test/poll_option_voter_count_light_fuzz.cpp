// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/Random.h"
#include "td/utils/tests.h"

#include "test/stealth/SourceContractFileReader.h"

#include <array>

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

TEST(PollOptionVoterCountLightFuzz, RandomizedProbeOrderPinsOptionalVoterCountSerializationPatterns) {
  auto source = read_normalized("td/telegram/PollOption.hpp");

  const std::array<td::Slice, 6> required = {
      td::Slice("boolhas_no_voter_count=voter_count_==0;"),
      td::Slice("STORE_FLAG(has_no_voter_count);"),
      td::Slice("if(!has_no_voter_count){store(voter_count_,storer);}"),
      td::Slice("boolhas_no_voter_count;"),
      td::Slice("PARSE_FLAG(has_no_voter_count);"),
      td::Slice("if(!has_no_voter_count){parse(voter_count_,parser);}"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(required.size()) - 1));
    ASSERT_TRUE(source.contains(required[idx].str()));
  }
}

TEST(PollOptionVoterCountLightFuzz, RandomizedProbeOrderKeepsLegacyVoterCountSerializationPatternsAbsent) {
  auto source = read_normalized("td/telegram/PollOption.hpp");

  const std::array<td::Slice, 7> forbidden = {
      td::Slice("if(has_no_voter_count){store(voter_count_,storer);}"),
      td::Slice("if(has_no_voter_count){parse(voter_count_,parser);}"),
      td::Slice("if(!has_no_data){store(voter_count_,storer);}"),
      td::Slice("if(!has_no_data){parse(voter_count_,parser);}"),
      td::Slice("boolhas_voter_count=voter_count_!=0;"),
      td::Slice("STORE_FLAG(has_voter_count);"),
      td::Slice("PARSE_FLAG(has_voter_count);"),
  };

  for (int i = 0; i < 10000; i++) {
    auto idx = static_cast<size_t>(td::Random::fast(0, static_cast<int>(forbidden.size()) - 1));
    ASSERT_FALSE(source.contains(forbidden[idx].str()));
  }
}