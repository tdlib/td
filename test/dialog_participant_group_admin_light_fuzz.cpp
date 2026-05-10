// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogParticipant.h"

#include "td/utils/tests.h"

#include <cstdint>
#include <random>

namespace {

td::string random_rank(std::mt19937_64 &rng) {
  std::uniform_int_distribution len_dist(0, 24);
  std::uniform_int_distribution char_dist(0, 25);

  td::string rank;
  rank.reserve(static_cast<size_t>(len_dist(rng)));
  const auto target_len = len_dist(rng);
  for (int i = 0; i < target_len; ++i) {
    rank.push_back(static_cast<char>('a' + char_dist(rng)));
  }
  return rank;
}

TEST(DialogParticipantGroupAdminLightFuzz, LegacyFactoryMaintainsLeastPrivilegeAcrossRandomRanks) {
  std::mt19937_64 rng(0x386ECA6FEULL);
  std::bernoulli_distribution creator_flag(0.5);

  constexpr std::int32_t kIterations = 10000;
  for (std::int32_t i = 0; i < kIterations; ++i) {
    const auto is_current_user_creator = creator_flag(rng);
    auto rank = random_rank(rng);
    auto expected_rank = rank;
    if (expected_rank.size() > 16) {
      expected_rank.resize(16);
    }

    auto status = td::DialogParticipantStatus::GroupAdministrator(is_current_user_creator, std::move(rank));

    ASSERT_TRUE(status.is_administrator());
    ASSERT_FALSE(status.can_promote_members());
    ASSERT_EQ(status.can_be_edited(), is_current_user_creator);
    ASSERT_EQ(status.get_rank(), expected_rank);
  }
}

}  // namespace
