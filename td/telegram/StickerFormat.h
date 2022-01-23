//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

namespace td {

// update store_sticker/store_sticker_set when this type changes
enum class StickerFormat : int32 { Unknown, Webp, Tgs };

StickerFormat get_sticker_format(Slice mime_type);

string get_sticker_format_mime_type(StickerFormat sticker_format);

Slice get_sticker_format_extension(StickerFormat sticker_format);

bool is_sticker_format_animated(StickerFormat sticker_format);

bool is_sticker_format_vector(StickerFormat sticker_format);

int64 get_max_sticker_file_size(StickerFormat sticker_format, bool for_thumbnail);

StringBuilder &operator<<(StringBuilder &string_builder, StickerFormat sticker_format);

}  // namespace td
