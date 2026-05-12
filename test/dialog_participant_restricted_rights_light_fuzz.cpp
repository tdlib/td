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
  const auto target_len = len_dist(rng);
  rank.reserve(static_cast<size_t>(target_len));
  for (int i = 0; i < target_len; ++i) {
    rank.push_back(static_cast<char>('a' + char_dist(rng)));
  }
  return rank;
}

TEST(DialogParticipantRestrictedRightsLightFuzz, UnknownChannelRestrictedRightsAlwaysDropCreateTopics) {
  std::mt19937_64 rng(0x562BCE098ULL);
  std::bernoulli_distribution bit(0.5);

  constexpr std::int32_t kIterations = 10000;
  for (std::int32_t i = 0; i < kIterations; ++i) {
    const bool can_edit_rank = bit(rng);
    auto rights = td::RestrictedRights(false, bit(rng), bit(rng), bit(rng), bit(rng), bit(rng), bit(rng), bit(rng),
                                       bit(rng), bit(rng), bit(rng), bit(rng), bit(rng), bit(rng), bit(rng), bit(rng),
                                       true /* force attacker-controlled can_manage_topics */, can_edit_rank,
                                       td::ChannelType::Unknown);

    auto status =
        td::DialogParticipantStatus::Restricted(std::move(rights), true, 0, td::ChannelType::Unknown, random_rank(rng));

    ASSERT_TRUE(status.is_restricted());
    ASSERT_FALSE(status.can_create_topics());
    ASSERT_EQ(status.can_edit_rank(), can_edit_rank);
  }
}

}  // namespace
