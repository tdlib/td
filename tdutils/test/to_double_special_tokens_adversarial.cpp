// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tests.h"

#include <array>

namespace to_double_special_tokens_adversarial {

td::string render_to_double(td::Slice input, int precision = 6) {
  return PSTRING() << td::StringBuilder::FixedDouble(td::to_double(input), precision);
}

void assert_rejected_special_value_bypass(td::Slice input) {
  auto result = render_to_double(input);
  if (result != "0.000000") {
    LOG(ERROR) << "Expected to reject special token bypass input " << input << ", got " << result;
  }
  ASSERT_EQ("0.000000", result);
}

}  // namespace to_double_special_tokens_adversarial

TEST(ToDoubleSpecialTokensAdversarial, rejects_nan_payload_spellings) {
  constexpr std::array<td::Slice, 4> inputs = {
      td::Slice("nan(payload)"),
      td::Slice("NaN(payload)"),
      td::Slice("+nan(payload)"),
      td::Slice("-nan(payload)"),
  };

  for (auto input : inputs) {
    to_double_special_tokens_adversarial::assert_rejected_special_value_bypass(input);
  }
}

TEST(ToDoubleSpecialTokensAdversarial, rejects_signed_nan_spellings) {
  constexpr std::array<td::Slice, 4> inputs = {
      td::Slice("+nan"),
      td::Slice("-nan"),
      td::Slice("+NaN"),
      td::Slice("-NaN"),
  };

  for (auto input : inputs) {
    to_double_special_tokens_adversarial::assert_rejected_special_value_bypass(input);
  }
}

TEST(ToDoubleSpecialTokensAdversarial, rejects_infinity_suffix_bypass_spellings) {
  constexpr std::array<td::Slice, 4> inputs = {
      td::Slice("infinity"),
      td::Slice("Infinity"),
      td::Slice("+infinity"),
      td::Slice("-infinity"),
  };

  for (auto input : inputs) {
    to_double_special_tokens_adversarial::assert_rejected_special_value_bypass(input);
  }
}