//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/AiComposeToneExample.h"

namespace td {

AiComposeToneExample::AiComposeToneExample(telegram_api::object_ptr<telegram_api::aiComposeToneExample> &&example) {
  if (example != nullptr) {
    from_text_ = get_formatted_text(nullptr, std::move(example->from_), true, false, "AiComposeToneExample from");
    to_text_ = get_formatted_text(nullptr, std::move(example->to_), true, false, "AiComposeToneExample to");
  }
}

td_api::object_ptr<td_api::textCompositionStyleExample>
AiComposeToneExample::get_text_composition_style_example_object() const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::textCompositionStyleExample>(
      get_formatted_text_object(nullptr, from_text_, true, -1), get_formatted_text_object(nullptr, to_text_, true, -1));
}

bool operator==(const AiComposeToneExample &lhs, const AiComposeToneExample &rhs) {
  return lhs.from_text_ == rhs.from_text_ && lhs.to_text_ == rhs.to_text_;
}

}  // namespace td
