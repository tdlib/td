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

class DiffText {
  class DiffEntity {
   public:
    enum class Type : int32 { Insert, Replace, Delete };
    Type type_ = Type::Insert;
    int32 offset_ = -1;
    int32 length_ = -1;
    string old_text_;

    DiffEntity(Type type, int32 offset, int32 length, string old_text = "")
        : type_(type), offset_(offset), length_(length), old_text_(std::move(old_text)) {
    }

    td_api::object_ptr<td_api::diffEntity> get_diff_entity_object() const;

   private:
    td_api::object_ptr<td_api::DiffEntityType> get_diff_entity_type_object() const;
  };

  string text_;
  vector<DiffEntity> entities_;

  bool check_entities() const;

 public:
  DiffText() = default;

  explicit DiffText(telegram_api::object_ptr<telegram_api::textWithEntities> &&text_with_entities);

  td_api::object_ptr<td_api::diffText> get_diff_text_object() const;
};

}  // namespace td
