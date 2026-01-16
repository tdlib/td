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

namespace td {

class StarGiftAttributeRarity {
  enum class Type : int32 { Permille, Uncommon, Rare, Epic, Legendary };
  Type type_ = Type::Permille;
  int32 rarity_permille_ = -1;

  friend bool operator==(const StarGiftAttributeRarity &lhs, const StarGiftAttributeRarity &rhs);

 public:
  StarGiftAttributeRarity() = default;

  explicit StarGiftAttributeRarity(telegram_api::object_ptr<telegram_api::StarGiftAttributeRarity> &&rarity);

  bool is_valid() const {
    return type_ != Type::Permille || (0 <= rarity_permille_ && rarity_permille_ <= 1000);
  }

  td_api::object_ptr<td_api::UpgradedGiftAttributeRarity> get_upgraded_gift_attribute_rarity_object() const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const StarGiftAttributeRarity &lhs, const StarGiftAttributeRarity &rhs);

inline bool operator!=(const StarGiftAttributeRarity &lhs, const StarGiftAttributeRarity &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
