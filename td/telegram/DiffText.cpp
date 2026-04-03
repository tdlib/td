//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DiffText.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/utf8.h"

namespace td {

td_api::object_ptr<td_api::DiffEntityType> DiffText::DiffEntity::get_diff_entity_type_object() const {
  switch (type_) {
    case Type::Insert:
      return td_api::make_object<td_api::diffEntityTypeInsert>();
    case Type::Replace:
      return td_api::make_object<td_api::diffEntityTypeReplace>(old_text_);
    case Type::Delete:
      return td_api::make_object<td_api::diffEntityTypeDelete>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::diffEntity> DiffText::DiffEntity::get_diff_entity_object() const {
  return td_api::make_object<td_api::diffEntity>(offset_, length_, get_diff_entity_type_object());
}

bool DiffText::check_entities() const {
  // entities must not intersect and must not begin and end in the middle of characters
  auto text_len = narrow_cast<int32>(utf8_utf16_length(text_));
  int32 cur_pos = 0;
  for (auto &entity : entities_) {
    if (entity.offset_ < cur_pos) {
      return false;
    }
    if (entity.length_ < 0 || entity.length_ > text_len - entity.offset_) {
      return false;
    }
    cur_pos = entity.offset_ + entity.length_;
  }
  return true;
}

DiffText::DiffText(telegram_api::object_ptr<telegram_api::textWithEntities> &&text_with_entities) {
  if (text_with_entities == nullptr) {
    LOG(ERROR) << "Receive no diff text";
    return;
  }

  text_ = std::move(text_with_entities->text_);
  entities_.reserve(text_with_entities->entities_.size());
  for (auto &server_entity : text_with_entities->entities_) {
    switch (server_entity->get_id()) {
      case telegram_api::messageEntityUnknown::ID:
        break;
      case telegram_api::messageEntityDiffInsert::ID: {
        auto entity = static_cast<const telegram_api::messageEntityDiffInsert *>(server_entity.get());
        entities_.emplace_back(DiffEntity::Type::Insert, entity->offset_, entity->length_);
        break;
      }
      case telegram_api::messageEntityDiffReplace::ID: {
        auto entity = static_cast<const telegram_api::messageEntityDiffReplace *>(server_entity.get());
        entities_.emplace_back(DiffEntity::Type::Replace, entity->offset_, entity->length_, entity->old_text_);
        break;
      }
      case telegram_api::messageEntityDiffDelete::ID: {
        auto entity = static_cast<const telegram_api::messageEntityDiffDelete *>(server_entity.get());
        entities_.emplace_back(DiffEntity::Type::Delete, entity->offset_, entity->length_);
        break;
      }
      default:
        LOG(ERROR) << "Receive diff entity " << to_string(server_entity);
    }
  }

  if (!check_entities()) {
    LOG(ERROR) << "Receive invalid diff entities in " << to_string(text_with_entities);
    entities_.clear();
  } else {
    td::remove_if(entities_, [](const auto &entity) { return entity.length_ <= 0; });
  }
}

td_api::object_ptr<td_api::diffText> DiffText::get_diff_text_object() const {
  return td_api::make_object<td_api::diffText>(
      text_, transform(entities_, [](const DiffEntity &entity) { return entity.get_diff_entity_object(); }));
}

}  // namespace td
