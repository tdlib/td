//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

struct LabeledPricePart {
  string label;
  int64 amount = 0;

  telegram_api::object_ptr<telegram_api::labeledPrice> get_input_labeled_price() const;

  LabeledPricePart() = default;

  LabeledPricePart(string &&label, int64 amount) : label(std::move(label)), amount(amount) {
  }

  static vector<telegram_api::object_ptr<telegram_api::labeledPrice>> get_input_labeled_prices(
      const vector<LabeledPricePart> &parts);

  template <class StorerT>
  void store(StorerT &storer) const {
    storer.store_string(label);
    storer.store_binary(amount);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    label = parser.template fetch_string<string>();
    amount = parser.fetch_long();
  }
};

inline bool operator==(const LabeledPricePart &lhs, const LabeledPricePart &rhs) {
  return lhs.label == rhs.label && lhs.amount == rhs.amount;
}

inline bool operator!=(const LabeledPricePart &lhs, const LabeledPricePart &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const LabeledPricePart &labeled_price_part);

}  // namespace td
