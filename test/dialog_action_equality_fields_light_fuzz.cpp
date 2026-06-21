// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogAction.h"
#include "td/telegram/MessageEntity.h"

#include "td/utils/common.h"
#include "td/utils/tests.h"

namespace {

td::FormattedText make_fuzz_text(td::int32 seed) {
  td::FormattedText text;
  text.text = "draft-" + std::to_string(seed);
  if ((seed % 4) == 0) {
    text.entities.emplace_back(td::MessageEntity::Type::Italic, 0, static_cast<td::int32>(text.text.size()));
  }
  return text;
}

TEST(DialogActionEqualityFieldsLightFuzz, RandomIdAndTextAlwaysParticipateInEquality) {
  constexpr td::int32 kIterations = 4096;
  for (td::int32 i = 1; i <= kIterations; ++i) {
    auto base_text = make_fuzz_text(i);

    td::DialogAction baseline(i, std::move(base_text));
    td::DialogAction same(i, make_fuzz_text(i));
    td::DialogAction different_random(i + 1, make_fuzz_text(i));
    td::DialogAction different_text(i, make_fuzz_text(i + 1));

    ASSERT_TRUE(baseline == same);
    ASSERT_FALSE(baseline != same);
    ASSERT_FALSE(baseline == different_random);
    ASSERT_FALSE(baseline == different_text);
  }
}

}  // namespace
