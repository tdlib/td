//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

enum class MessageContentType : int32 {
  None = -1,
  Text,
  Animation,
  Audio,
  Document,
  Photo,
  Sticker,
  Video,
  VoiceNote,
  Contact,
  Location,
  Venue,
  ChatCreate,
  ChatChangeTitle,
  ChatChangePhoto,
  ChatDeletePhoto,
  ChatDeleteHistory,
  ChatAddUsers,
  ChatJoinedByLink,
  ChatDeleteUser,
  ChatMigrateTo,
  ChannelCreate,
  ChannelMigrateFrom,
  PinMessage,
  Game,
  GameScore,
  ScreenshotTaken,
  ChatSetTtl,
  Unsupported,
  Call,
  Invoice,
  PaymentSuccessful,
  VideoNote,
  ContactRegistered,
  ExpiredPhoto,
  ExpiredVideo,
  LiveLocation,
  CustomServiceAction,
  WebsiteConnected,
  PassportDataSent,
  PassportDataReceived,
  Poll,
  Dice
};

StringBuilder &operator<<(StringBuilder &string_builder, MessageContentType content_type);

bool is_allowed_media_group_content(MessageContentType content_type);

bool is_secret_message_content(int32 ttl, MessageContentType content_type);

bool is_service_message_content(MessageContentType content_type);

bool can_have_message_content_caption(MessageContentType content_type);

}  // namespace td
