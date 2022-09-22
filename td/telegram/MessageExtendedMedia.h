//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Dimensions.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/Photo.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;
class Td;

class MessageExtendedMedia {
  enum class Type : int32 { Empty, Unsupported, Preview, Photo, Video };
  Type type_ = Type::Empty;
  FormattedText caption_;

  // for Preview
  int32 duration_ = 0;
  Dimensions dimensions_;
  string minithumbnail_;

  // for Photo
  Photo photo_;

  // for Video
  FileId video_file_id_;

  friend bool operator==(const MessageExtendedMedia &lhs, const MessageExtendedMedia &rhs);

  bool is_media() const {
    return type_ != Type::Empty && type_ != Type::Preview;
  }

 public:
  MessageExtendedMedia() = default;

  MessageExtendedMedia(Td *td, telegram_api::object_ptr<telegram_api::MessageExtendedMedia> &&extended_media,
                       FormattedText &&caption, DialogId owner_dialog_id);

  bool is_empty() const {
    return type_ == Type::Empty;
  }

  void update_from(const MessageExtendedMedia &old_extended_media);

  td_api::object_ptr<td_api::MessageExtendedMedia> get_message_extended_media_object(Td *td, bool skip_bot_commands,
                                                                                     int32 max_media_timestamp) const;

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MessageExtendedMedia &lhs, const MessageExtendedMedia &rhs);

bool operator!=(const MessageExtendedMedia &lhs, const MessageExtendedMedia &rhs);

}  // namespace td
