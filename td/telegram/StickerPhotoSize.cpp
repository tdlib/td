//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StickerPhotoSize.h"

#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"

namespace td {

Result<unique_ptr<StickerPhotoSize>> get_sticker_photo_size(
    Td *td, const td_api::object_ptr<td_api::chatPhotoSticker> &sticker) {
  if (sticker == nullptr) {
    return nullptr;
  }
  if (sticker->type_ == nullptr) {
    return Status::Error(400, "Type must be non-null");
  }
  if (sticker->background_fill_ == nullptr) {
    return Status::Error(400, "Background must be non-null");
  }
  auto result = make_unique<StickerPhotoSize>();
  switch (sticker->type_->get_id()) {
    case td_api::chatPhotoStickerTypeRegularOrMask::ID: {
      auto type = static_cast<const td_api::chatPhotoStickerTypeRegularOrMask *>(sticker->type_.get());
      result->type = StickerPhotoSize::Type::Sticker;
      result->sticker_set_id = StickerSetId(type->sticker_set_id_);
      result->sticker_id = type->sticker_id_;
      //if (!td->stickers_manager_->have_sticker(result->sticker_set_id, result->sticker_id)) {
      //  return Status::Error(400, "Sticker not found");
      //}
      break;
    }
    case td_api::chatPhotoStickerTypeCustomEmoji::ID: {
      auto type = static_cast<const td_api::chatPhotoStickerTypeCustomEmoji *>(sticker->type_.get());
      result->type = StickerPhotoSize::Type::CustomEmoji;
      result->custom_emoji_id = CustomEmojiId(type->custom_emoji_id_);
      //if (!td->stickers_manager_->have_custom_emoji_id(result->custom_emoji_id)) {
      //  return Status::Error(400, "Custom emoji not found");
      //}
      break;
    }
  }
  auto fill = sticker->background_fill_.get();
  switch (fill->get_id()) {
    case td_api::backgroundFillSolid::ID: {
      auto solid = static_cast<const td_api::backgroundFillSolid *>(fill);
      result->background_colors.push_back(solid->color_);
      break;
    }
    case td_api::backgroundFillGradient::ID: {
      auto gradient = static_cast<const td_api::backgroundFillGradient *>(fill);
      result->background_colors.push_back(gradient->top_color_);
      result->background_colors.push_back(gradient->bottom_color_);
      break;
    }
    case td_api::backgroundFillFreeformGradient::ID: {
      auto freeform = static_cast<const td_api::backgroundFillFreeformGradient *>(fill);
      if (freeform->colors_.size() != 3 && freeform->colors_.size() != 4) {
        return Status::Error(400, "Invalid number of colors specified");
      }
      result->background_colors = freeform->colors_;
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
  for (auto &color : result->background_colors) {
    color &= 0xFFFFFF;
  }
  return std::move(result);
}

telegram_api::object_ptr<telegram_api::VideoSize> get_input_video_size_object(
    Td *td, const unique_ptr<StickerPhotoSize> &sticker_photo_size) {
  if (sticker_photo_size == nullptr) {
    return nullptr;
  }
  switch (sticker_photo_size->type) {
    case StickerPhotoSize::Type::Sticker:
      return telegram_api::make_object<telegram_api::videoSizeStickerMarkup>(
          td->stickers_manager_->get_input_sticker_set(sticker_photo_size->sticker_set_id),
          sticker_photo_size->sticker_id, vector<int32>(sticker_photo_size->background_colors));
    case StickerPhotoSize::Type::CustomEmoji:
      return telegram_api::make_object<telegram_api::videoSizeEmojiMarkup>(
          sticker_photo_size->custom_emoji_id.get(), vector<int32>(sticker_photo_size->background_colors));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const StickerPhotoSize &lhs, const StickerPhotoSize &rhs) {
  return lhs.type == rhs.type && lhs.sticker_set_id == rhs.sticker_set_id && lhs.sticker_id == rhs.sticker_id &&
         lhs.custom_emoji_id == rhs.custom_emoji_id && lhs.background_colors == rhs.background_colors;
}

bool operator!=(const StickerPhotoSize &lhs, const StickerPhotoSize &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const StickerPhotoSize &sticker_photo_size) {
  switch (sticker_photo_size.type) {
    case StickerPhotoSize::Type::Sticker:
      return string_builder << sticker_photo_size.sticker_id << " from " << sticker_photo_size.sticker_set_id << " on "
                            << sticker_photo_size.background_colors;
    case StickerPhotoSize::Type::CustomEmoji:
      return string_builder << sticker_photo_size.custom_emoji_id << " on " << sticker_photo_size.background_colors;
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
