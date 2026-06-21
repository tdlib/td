// SPDX-FileCopyrightText: Copyright 2026 telemt community
// SPDX-License-Identifier: MIT
// telemt: https://github.com/telemt
// telemt: https://t.me/telemtrs
//

#include "td/telegram/DialogAction.h"
#include "td/telegram/MessageEntity.h"

#include "td/utils/tests.h"

namespace {

td::FormattedText make_text(td::Slice text) {
  td::FormattedText result;
  result.text = text.str();
  return result;
}

TEST(DialogActionEqualityFieldsRuntime, TextDraftActionsWithIdenticalFieldsCompareEqual) {
  auto text = make_text("draft");
  td::DialogAction lhs(42, std::move(text));
  td::DialogAction rhs(42, make_text("draft"));

  ASSERT_TRUE(lhs == rhs);
  ASSERT_FALSE(lhs != rhs);
}

TEST(DialogActionEqualityFieldsRuntime, TextDraftActionsWithDifferentRandomIdsCompareNotEqual) {
  td::DialogAction lhs(42, make_text("draft"));
  td::DialogAction rhs(43, make_text("draft"));

  ASSERT_FALSE(lhs == rhs);
  ASSERT_TRUE(lhs != rhs);
}

TEST(DialogActionEqualityFieldsRuntime, TextDraftActionsWithDifferentTextCompareNotEqual) {
  td::DialogAction lhs(42, make_text("draft one"));
  td::DialogAction rhs(42, make_text("draft two"));

  ASSERT_FALSE(lhs == rhs);
  ASSERT_TRUE(lhs != rhs);
}

TEST(DialogActionEqualityFieldsRuntime, TextDraftActionsWithDifferentEntitiesCompareNotEqual) {
  td::FormattedText lhs_text;
  lhs_text.text = "hello";
  lhs_text.entities.emplace_back(td::MessageEntity::Type::Bold, 0, 5);

  td::FormattedText rhs_text;
  rhs_text.text = "hello";

  td::DialogAction lhs(7, std::move(lhs_text));
  td::DialogAction rhs(7, std::move(rhs_text));

  ASSERT_FALSE(lhs == rhs);
  ASSERT_TRUE(lhs != rhs);
}

}  // namespace
