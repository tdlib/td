//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

namespace td {

class LabeledPricePart {
  string label_;
  int64 amount_ = 0;

  telegram_api::object_ptr<telegram_api::labeledPrice> get_input_labeled_price() const;

  friend bool operator==(const LabeledPricePart &lhs, const LabeledPricePart &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const LabeledPricePart &labeled_price_part);

 public:
  LabeledPricePart() = default;

  LabeledPricePart(string &&label, int64 amount) : label_(std::move(label)), amount_(amount) {
  }

  static Result<vector<LabeledPricePart>> get_labeled_price_parts(
      vector<td_api::object_ptr<td_api::labeledPricePart>> &&parts, int64 *result_total_amount);

  static vector<telegram_api::object_ptr<telegram_api::labeledPrice>> get_input_labeled_prices(
      const vector<LabeledPricePart> &parts);

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_string(label_);
    storer.store_binary(amount_);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    label_ = parser.template fetch_string<string>();
    amount_ = parser.fetch_long();
  }
};

inline bool operator==(const LabeledPricePart &lhs, const LabeledPricePart &rhs) {
  return lhs.label_ == rhs.label_ && lhs.amount_ == rhs.amount_;
}

inline bool operator!=(const LabeledPricePart &lhs, const LabeledPricePart &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const LabeledPricePart &labeled_price_part);

}  // namespace td
