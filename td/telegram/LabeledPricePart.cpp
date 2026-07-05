//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/LabeledPricePart.h"

#include "td/telegram/misc.h"

#include "td/utils/algorithm.h"

namespace td {

telegram_api::object_ptr<telegram_api::labeledPrice> LabeledPricePart::get_input_labeled_price() const {
  return telegram_api::make_object<telegram_api::labeledPrice>(label_, amount_);
}

vector<telegram_api::object_ptr<telegram_api::labeledPrice>> LabeledPricePart::get_input_labeled_prices(
    const vector<LabeledPricePart> &parts) {
  return transform(parts, [](const LabeledPricePart &price) { return price.get_input_labeled_price(); });
}

Result<vector<LabeledPricePart>> LabeledPricePart::get_labeled_price_parts(
    vector<td_api::object_ptr<td_api::labeledPricePart>> &&parts, int64 *result_total_amount) {
  if (parts.empty()) {
    return Status::Error(400, "There must be at least one price part");
  }
  vector<LabeledPricePart> result;
  result.reserve(parts.size());
  int64 total_amount = 0;
  for (auto &part : parts) {
    if (part == nullptr) {
      return Status::Error(400, "Price part must be non-empty");
    }
    if (!clean_input_string(part->label_)) {
      return Status::Error(400, "Price part label must be encoded in UTF-8");
    }
    if (!check_currency_amount(part->amount_)) {
      return Status::Error(400, "Too big amount of the currency specified");
    }
    result.emplace_back(std::move(part->label_), part->amount_);
    total_amount += part->amount_;
  }
  if (total_amount <= 0) {
    return Status::Error(400, "Total price must be positive");
  }
  if (!check_currency_amount(total_amount)) {
    return Status::Error(400, "Total price is too big");
  }
  if (result_total_amount != nullptr) {
    *result_total_amount = total_amount;
  }
  return std::move(result);
}

StringBuilder &operator<<(StringBuilder &string_builder, const LabeledPricePart &labeled_price_part) {
  return string_builder << '[' << labeled_price_part.label_ << ": " << labeled_price_part.amount_ << ']';
}

}  // namespace td
