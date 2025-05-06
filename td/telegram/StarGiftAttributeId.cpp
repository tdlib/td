//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StarGiftAttributeId.h"

#include "td/utils/algorithm.h"

namespace td {

Result<StarGiftAttributeId> StarGiftAttributeId::get_star_gift_attribute_id(
    const td_api::object_ptr<td_api::UpgradedGiftAttributeId> &attribute) {
  if (attribute == nullptr) {
    return Status::Error(400, "Attribute identifier must be non-empty");
  }
  StarGiftAttributeId result;
  switch (attribute->get_id()) {
    case td_api::upgradedGiftAttributeIdModel::ID:
      result.type_ = Type::Model;
      result.sticker_id_ = static_cast<const td_api::upgradedGiftAttributeIdModel *>(attribute.get())->sticker_id_;
      break;
    case td_api::upgradedGiftAttributeIdSymbol::ID:
      result.type_ = Type::Pattern;
      result.sticker_id_ = static_cast<const td_api::upgradedGiftAttributeIdSymbol *>(attribute.get())->sticker_id_;
      break;
    case td_api::upgradedGiftAttributeIdBackdrop::ID:
      result.type_ = Type::Backdrop;
      result.backdrop_id_ = static_cast<const td_api::upgradedGiftAttributeIdBackdrop *>(attribute.get())->backdrop_id_;
      break;
    default:
      UNREACHABLE();
  }
  return result;
}

Result<vector<StarGiftAttributeId>> StarGiftAttributeId::get_star_gift_attribute_ids(
    const vector<td_api::object_ptr<td_api::UpgradedGiftAttributeId>> &attributes) {
  vector<StarGiftAttributeId> result;
  for (auto &attribute : attributes) {
    TRY_RESULT(attribute_id, get_star_gift_attribute_id(attribute));
    result.push_back(attribute_id);
  }
  return result;
}

StarGiftAttributeId::StarGiftAttributeId(telegram_api::object_ptr<telegram_api::StarGiftAttributeId> attribute) {
  CHECK(attribute != nullptr);
  switch (attribute->get_id()) {
    case telegram_api::starGiftAttributeIdModel::ID:
      type_ = Type::Model;
      sticker_id_ = static_cast<const telegram_api::starGiftAttributeIdModel *>(attribute.get())->document_id_;
      break;
    case telegram_api::starGiftAttributeIdPattern::ID:
      type_ = Type::Pattern;
      sticker_id_ = static_cast<const telegram_api::starGiftAttributeIdPattern *>(attribute.get())->document_id_;
      break;
    case telegram_api::starGiftAttributeIdBackdrop::ID:
      type_ = Type::Backdrop;
      backdrop_id_ = static_cast<const telegram_api::starGiftAttributeIdBackdrop *>(attribute.get())->backdrop_id_;
      break;
    default:
      UNREACHABLE();
  }
}

telegram_api::object_ptr<telegram_api::StarGiftAttributeId>
StarGiftAttributeId::get_input_star_gift_attribute_id_object() const {
  switch (type_) {
    case Type::Model:
      return telegram_api::make_object<telegram_api::starGiftAttributeIdModel>(sticker_id_);
    case Type::Pattern:
      return telegram_api::make_object<telegram_api::starGiftAttributeIdPattern>(sticker_id_);
    case Type::Backdrop:
      return telegram_api::make_object<telegram_api::starGiftAttributeIdBackdrop>(backdrop_id_);
    case Type::None:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

vector<telegram_api::object_ptr<telegram_api::StarGiftAttributeId>>
StarGiftAttributeId::get_input_star_gift_attribute_ids_object(const vector<StarGiftAttributeId> &attributes) {
  return transform(attributes, [](const StarGiftAttributeId &attribute) {
    return attribute.get_input_star_gift_attribute_id_object();
  });
}

bool operator==(const StarGiftAttributeId &lhs, const StarGiftAttributeId &rhs) {
  return lhs.type_ == rhs.type_ && lhs.sticker_id_ == rhs.sticker_id_ && lhs.backdrop_id_ == rhs.backdrop_id_;
}

}  // namespace td
