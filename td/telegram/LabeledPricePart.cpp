//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LabeledPricePart.h"

#include "td/utils/algorithm.h"

namespace td {

telegram_api::object_ptr<telegram_api::labeledPrice> LabeledPricePart::get_input_labeled_price() const {
  return telegram_api::make_object<telegram_api::labeledPrice>(label, amount);
}

vector<telegram_api::object_ptr<telegram_api::labeledPrice>> LabeledPricePart::get_input_labeled_prices(
    const vector<LabeledPricePart> &parts) {
  return transform(parts, [](const LabeledPricePart &price) { return price.get_input_labeled_price(); });
}

StringBuilder &operator<<(StringBuilder &string_builder, const LabeledPricePart &labeled_price_part) {
  return string_builder << '[' << labeled_price_part.label << ": " << labeled_price_part.amount << ']';
}

}  // namespace td
