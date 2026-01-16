//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftAttributeRarity.h"

namespace td {

StarGiftAttributeRarity::StarGiftAttributeRarity(
    telegram_api::object_ptr<telegram_api::StarGiftAttributeRarity> &&rarity) {
  CHECK(rarity != nullptr);
  switch (rarity->get_id()) {
    case telegram_api::starGiftAttributeRarity::ID:
      type_ = Type::Permille;
      rarity_permille_ = static_cast<const telegram_api::starGiftAttributeRarity *>(rarity.get())->permille_;
      break;
    case telegram_api::starGiftAttributeRarityUncommon::ID:
      type_ = Type::Uncommon;
      break;
    case telegram_api::starGiftAttributeRarityRare::ID:
      type_ = Type::Rare;
      break;
    case telegram_api::starGiftAttributeRarityEpic::ID:
      type_ = Type::Epic;
      break;
    case telegram_api::starGiftAttributeRarityLegendary::ID:
      type_ = Type::Legendary;
      break;
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::UpgradedGiftAttributeRarity>
StarGiftAttributeRarity::get_upgraded_gift_attribute_rarity_object() const {
  switch (type_) {
    case Type::Permille:
      return td_api::make_object<td_api::upgradedGiftAttributeRarityPerMille>(rarity_permille_);
    case Type::Uncommon:
      return td_api::make_object<td_api::upgradedGiftAttributeRarityUncommon>();
    case Type::Rare:
      return td_api::make_object<td_api::upgradedGiftAttributeRarityRare>();
    case Type::Epic:
      return td_api::make_object<td_api::upgradedGiftAttributeRarityEpic>();
    case Type::Legendary:
      return td_api::make_object<td_api::upgradedGiftAttributeRarityLegendary>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const StarGiftAttributeRarity &lhs, const StarGiftAttributeRarity &rhs) {
  if (lhs.type_ == StarGiftAttributeRarity::Type::Permille) {
    return rhs.type_ == StarGiftAttributeRarity::Type::Permille && lhs.rarity_permille_ == rhs.rarity_permille_;
  }
  return lhs.type_ == rhs.type_;
}

}  // namespace td
