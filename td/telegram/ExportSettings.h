//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"

#include "td/utils/common.h"

namespace td {

enum class ExportFormat : int32 { Html, Json, HtmlAndJson };

struct ExportSettings {
  enum Type : int32 {
    PersonalInfo = 1 << 0,
    Userpics = 1 << 1,
    Contacts = 1 << 2,
    Sessions = 1 << 3,
    OtherData = 1 << 4,
    PersonalChats = 1 << 5,
    BotChats = 1 << 6,
    PrivateGroups = 1 << 7,
    PublicGroups = 1 << 8,
    PrivateChannels = 1 << 9,
    PublicChannels = 1 << 10,
    Stories = 1 << 11,
    ProfileMusic = 1 << 12,

    GroupsMask = PrivateGroups | PublicGroups,
    ChannelsMask = PrivateChannels | PublicChannels,
    GroupsChannelsMask = GroupsMask | ChannelsMask,
    // the chats, which have messages outside the last range of message identifiers
    NonChannelChatsMask = PersonalChats | BotChats | PrivateGroups,
    AnyChatsMask = PersonalChats | BotChats | GroupsChannelsMask,
  };

  enum MediaType : int32 {
    Photo = 1 << 0,
    Video = 1 << 1,
    VoiceMessage = 1 << 2,
    VideoMessage = 1 << 3,
    Sticker = 1 << 4,
    Gif = 1 << 5,
    File = 1 << 6,
  };

  static constexpr int64 MAX_FILE_SIZE = static_cast<int64>(4000) << 20;

  // the directory chosen by the application, in which the directory with the exported data is created
  string directory;
  // the path of the directory with the exported data, ending with a directory separator; created when
  // the takeout session is received
  string path;
  ExportFormat format = ExportFormat::Html;
  // the style sheet of HTML pages, which is applied after the default one
  string html_stylesheet;

  int32 types = 0;
  // the chat types, which are exported with messages of all members; other chats are exported with own messages only
  int32 full_chats = 0;

  int32 media_types = 0;
  int64 media_size_limit = 0;

  DialogId single_dialog_id;
  MessageId single_topic_top_thread_message_id;
  int32 single_dialog_min_date = 0;
  int32 single_dialog_max_date = 0;

  // the offset of the local time from UTC, in seconds; dates in the exported data are written in the local time
  int32 utc_time_offset = 0;

  bool only_single_dialog() const {
    return single_dialog_id.is_valid();
  }

  bool only_single_topic() const {
    return only_single_dialog() && single_topic_top_thread_message_id.is_valid();
  }

  bool is_inside_date_range(int32 date) const {
    return (single_dialog_min_date <= 0 || single_dialog_min_date <= date) &&
           (single_dialog_max_date <= 0 || date < single_dialog_max_date);
  }

  bool has_media() const {
    return media_types != 0 && media_size_limit > 0;
  }
};

}  // namespace td
