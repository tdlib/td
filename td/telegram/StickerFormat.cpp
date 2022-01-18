//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/StickerFormat.h"

namespace td {

StickerFormat get_sticker_format(Slice mime_type) {
  if (mime_type == "application/x-tgsticker") {
    return StickerFormat::Tgs;
  }
  if (mime_type == "image/webp") {
    return StickerFormat::Webp;
  }
  return StickerFormat::Unknown;
}

string get_sticker_format_mime_type(StickerFormat sticker_format) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
    case StickerFormat::Webp:
      return "image/webp";
    case StickerFormat::Tgs:
      return "application/x-tgsticker";
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
    default:
      UNREACHABLE();
      return Slice();
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
    default:
      UNREACHABLE();
      return false;
  }
}

size_t get_max_sticker_file_size(StickerFormat sticker_format, bool for_thumbnail) {
  switch (sticker_format) {
    case StickerFormat::Unknown:
    case StickerFormat::Webp:
      return for_thumbnail ? (1 << 17) : (1 << 19);
    case StickerFormat::Tgs:
      return for_thumbnail ? (1 << 15) : (1 << 16);
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
    default:
      UNREACHABLE();
      return string_builder;
  }
}

}  // namespace td
