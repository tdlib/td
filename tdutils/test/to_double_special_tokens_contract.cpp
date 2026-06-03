// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"

namespace to_double_special_tokens_contract {

td::string render_to_double(td::Slice input, int precision = 6) {
  return PSTRING() << td::StringBuilder::FixedDouble(td::to_double(input), precision);
}

}  // namespace to_double_special_tokens_contract

TEST(ToDoubleSpecialTokensContract, accepts_exact_inf_and_nan_tokens) {
  ASSERT_EQ("inf", to_double_special_tokens_contract::render_to_double("inf"));
  ASSERT_EQ("-inf", to_double_special_tokens_contract::render_to_double("-inf"));
  ASSERT_EQ("nan", to_double_special_tokens_contract::render_to_double("NaN"));
}

TEST(ToDoubleSpecialTokensContract, accepts_delimited_special_token_with_trailing_garbage) {
  ASSERT_EQ("inf", to_double_special_tokens_contract::render_to_double("  inF  asdasd"));
}