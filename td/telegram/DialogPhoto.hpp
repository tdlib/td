//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogPhoto.h"
#include "td/telegram/files/FileId.hpp"
#include "td/telegram/PhotoSize.hpp"
#include "td/telegram/Version.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const DialogPhoto &dialog_photo, StorerT &storer) {
  bool has_file_ids = dialog_photo.small_file_id.is_valid() || dialog_photo.big_file_id.is_valid();
  bool has_minithumbnail = !dialog_photo.minithumbnail.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(has_file_ids);
  STORE_FLAG(dialog_photo.has_animation);
  STORE_FLAG(has_minithumbnail);
  STORE_FLAG(dialog_photo.is_personal);
  END_STORE_FLAGS();
  if (has_file_ids) {
    store(dialog_photo.small_file_id, storer);
    store(dialog_photo.big_file_id, storer);
  }
  if (has_minithumbnail) {
    store(dialog_photo.minithumbnail, storer);
  }
}

template <class ParserT>
void parse(DialogPhoto &dialog_photo, ParserT &parser) {
  bool has_file_ids = true;
  bool has_minithumbnail = false;
  if (parser.version() >= static_cast<int32>(Version::AddDialogPhotoHasAnimation)) {
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_file_ids);
    PARSE_FLAG(dialog_photo.has_animation);
    PARSE_FLAG(has_minithumbnail);
    PARSE_FLAG(dialog_photo.is_personal);
    END_PARSE_FLAGS();
  }
  if (has_file_ids) {
    parse(dialog_photo.small_file_id, parser);
    parse(dialog_photo.big_file_id, parser);
  }
  if (has_minithumbnail) {
    parse(dialog_photo.minithumbnail, parser);
  }
}

template <class StorerT>
void store(const ProfilePhoto &profile_photo, StorerT &storer) {
  store(static_cast<const DialogPhoto &>(profile_photo), storer);
  store(profile_photo.id, storer);
}

template <class ParserT>
void parse(ProfilePhoto &profile_photo, ParserT &parser) {
  parse(static_cast<DialogPhoto &>(profile_photo), parser);
  parse(profile_photo.id, parser);
}

}  // namespace td
