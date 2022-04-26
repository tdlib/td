//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StickerFormat.h"

#include "td/utils/logging.h"

namespace td {

StickerFormat get_sticker_format_by_mime_type(Slice mime_type) {
  if (mime_type == "application/x-tgsticker") {
    return StickerFormat::Tgs;
  }
  if (mime_type == "image/webp") {
    return StickerFormat::Webp;
  }
  if (mime_type == "video/webm") {
    return StickerFormat::Webm;
  }
  return StickerFormat::Unknown;
}

StickerFormat get_sticker_format_by_extension(Slice extension) {
  if (extension == "tgs") {
    return StickerFormat::Tgs;
  }
  if (extension == "webp") {
    return StickerFormat::Webp;
  }
  if (extension == "webm") {
    return StickerFormat::Webm;
  }
  return StickerFormat::Unknown;
}

td_api::object_ptr<td_api::StickerType> get_sticker_type_object(
    StickerFormat sticker_format, bool is_masks, td_api::object_ptr<td_api::maskPosition> mask_position) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
      LOG(ERROR) << "Have a sticker of unknown format";
      return td_api::make_object<td_api::stickerTypeStatic>();
    case StickerFormat::Webp:
      if (is_masks) {
        return td_api::make_object<td_api::stickerTypeMask>(std::move(mask_position));
      }
      return td_api::make_object<td_api::stickerTypeStatic>();
    case StickerFormat::Tgs:
      return td_api::make_object<td_api::stickerTypeAnimated>();
    case StickerFormat::Webm:
      return td_api::make_object<td_api::stickerTypeVideo>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

string get_sticker_format_mime_type(StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
    case StickerFormat::Webp:
      return "image/webp";
    case StickerFormat::Tgs:
      return "application/x-tgsticker";
    case StickerFormat::Webm:
      return "video/webm";
    default:
      UNREACHABLE();
      return string();
  }
}

Slice get_sticker_format_extension(StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
      return Slice();
    case StickerFormat::Webp:
      return Slice(".webp");
    case StickerFormat::Tgs:
      return Slice(".tgs");
    case StickerFormat::Webm:
      return Slice(".webm");
    default:
      UNREACHABLE();
      return Slice();
  }
}

PhotoFormat get_sticker_format_photo_format(StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
    case StickerFormat::Webp:
      return PhotoFormat::Webp;
    case StickerFormat::Tgs:
      return PhotoFormat::Tgs;
    case StickerFormat::Webm:
      return PhotoFormat::Webm;
    default:
      UNREACHABLE();
      return PhotoFormat::Webp;
  }
}

bool is_sticker_format_animated(StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
      return false;
    case StickerFormat::Webp:
      return false;
    case StickerFormat::Tgs:
      return true;
    case StickerFormat::Webm:
      return true;
    default:
      UNREACHABLE();
      return false;
  }
}

bool is_sticker_format_vector(StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
      return false;
    case StickerFormat::Webp:
      return false;
    case StickerFormat::Tgs:
      return true;
    case StickerFormat::Webm:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

int64 get_max_sticker_file_size(StickerFormat sticker_format, bool for_thumbnail) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
    case StickerFormat::Webp:
      return for_thumbnail ? (1 << 17) : (1 << 19);
    case StickerFormat::Tgs:
      return for_thumbnail ? (1 << 15) : (1 << 16);
    case StickerFormat::Webm:
      return for_thumbnail ? (1 << 15) : (1 << 18);
    default:
      UNREACHABLE();
      return 0;
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
      return string_builder << "unknown";
    case StickerFormat::Webp:
      return string_builder << "WEBP";
    case StickerFormat::Tgs:
      return string_builder << "TGS";
    case StickerFormat::Webm:
      return string_builder << "WEBM";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
