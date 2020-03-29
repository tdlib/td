//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageContentType.h"

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, MessageContentType content_type) {
  switch (content_type) {
    case MessageContentType::None:
      return string_builder << "None";
    case MessageContentType::Animation:
      return string_builder << "Animation";
    case MessageContentType::Audio:
      return string_builder << "Audio";
    case MessageContentType::Document:
      return string_builder << "Document";
    case MessageContentType::ExpiredPhoto:
      return string_builder << "ExpiredPhoto";
    case MessageContentType::Photo:
      return string_builder << "Photo";
    case MessageContentType::ExpiredVideo:
      return string_builder << "ExpiredVideo";
    case MessageContentType::Video:
      return string_builder << "Video";
    case MessageContentType::VideoNote:
      return string_builder << "VideoNote";
    case MessageContentType::VoiceNote:
      return string_builder << "VoiceNote";
    case MessageContentType::Contact:
      return string_builder << "Contact";
    case MessageContentType::LiveLocation:
      return string_builder << "LiveLocation";
    case MessageContentType::Location:
      return string_builder << "Location";
    case MessageContentType::Venue:
      return string_builder << "Venue";
    case MessageContentType::Game:
      return string_builder << "Game";
    case MessageContentType::Invoice:
      return string_builder << "Invoice";
    case MessageContentType::Sticker:
      return string_builder << "Sticker";
    case MessageContentType::Text:
      return string_builder << "Text";
    case MessageContentType::Unsupported:
      return string_builder << "Unsupported";
    case MessageContentType::ChatCreate:
      return string_builder << "ChatCreate";
    case MessageContentType::ChatChangeTitle:
      return string_builder << "ChatChangeTitle";
    case MessageContentType::ChatChangePhoto:
      return string_builder << "ChatChangePhoto";
    case MessageContentType::ChatDeletePhoto:
      return string_builder << "ChatDeletePhoto";
    case MessageContentType::ChatDeleteHistory:
      return string_builder << "ChatDeleteHistory";
    case MessageContentType::ChatAddUsers:
      return string_builder << "ChatAddUsers";
    case MessageContentType::ChatJoinedByLink:
      return string_builder << "ChatJoinedByLink";
    case MessageContentType::ChatDeleteUser:
      return string_builder << "ChatDeleteUser";
    case MessageContentType::ChatMigrateTo:
      return string_builder << "ChatMigrateTo";
    case MessageContentType::ChannelCreate:
      return string_builder << "ChannelCreate";
    case MessageContentType::ChannelMigrateFrom:
      return string_builder << "ChannelMigrateFrom";
    case MessageContentType::PinMessage:
      return string_builder << "PinMessage";
    case MessageContentType::GameScore:
      return string_builder << "GameScore";
    case MessageContentType::ScreenshotTaken:
      return string_builder << "ScreenshotTaken";
    case MessageContentType::ChatSetTtl:
      return string_builder << "ChatSetTtl";
    case MessageContentType::Call:
      return string_builder << "Call";
    case MessageContentType::PaymentSuccessful:
      return string_builder << "PaymentSuccessful";
    case MessageContentType::ContactRegistered:
      return string_builder << "ContactRegistered";
    case MessageContentType::CustomServiceAction:
      return string_builder << "CustomServiceAction";
    case MessageContentType::WebsiteConnected:
      return string_builder << "WebsiteConnected";
    case MessageContentType::PassportDataSent:
      return string_builder << "PassportDataSent";
    case MessageContentType::PassportDataReceived:
      return string_builder << "PassportDataReceived";
    case MessageContentType::Poll:
      return string_builder << "Poll";
    case MessageContentType::Dice:
      return string_builder << "Dice";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

bool is_allowed_media_group_content(MessageContentType content_type) {
  switch (content_type) {
    case MessageContentType::Photo:
    case MessageContentType::Video:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
      return true;
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Contact:
    case MessageContentType::Document:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Sticker:
    case MessageContentType::Text:
    case MessageContentType::Unsupported:
    case MessageContentType::Venue:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
    case MessageContentType::ChatCreate:
    case MessageContentType::ChatChangeTitle:
    case MessageContentType::ChatChangePhoto:
    case MessageContentType::ChatDeletePhoto:
    case MessageContentType::ChatDeleteHistory:
    case MessageContentType::ChatAddUsers:
    case MessageContentType::ChatJoinedByLink:
    case MessageContentType::ChatDeleteUser:
    case MessageContentType::ChatMigrateTo:
    case MessageContentType::ChannelCreate:
    case MessageContentType::ChannelMigrateFrom:
    case MessageContentType::PinMessage:
    case MessageContentType::GameScore:
    case MessageContentType::ScreenshotTaken:
    case MessageContentType::ChatSetTtl:
    case MessageContentType::Call:
    case MessageContentType::PaymentSuccessful:
    case MessageContentType::ContactRegistered:
    case MessageContentType::CustomServiceAction:
    case MessageContentType::WebsiteConnected:
    case MessageContentType::PassportDataSent:
    case MessageContentType::PassportDataReceived:
    case MessageContentType::Poll:
    case MessageContentType::Dice:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

bool is_secret_message_content(int32 ttl, MessageContentType content_type) {
  if (ttl <= 0 || ttl > 60) {
    return false;
  }
  switch (content_type) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Photo:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
      return true;
    case MessageContentType::Contact:
    case MessageContentType::Document:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Sticker:
    case MessageContentType::Text:
    case MessageContentType::Unsupported:
    case MessageContentType::Venue:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
    case MessageContentType::ChatCreate:
    case MessageContentType::ChatChangeTitle:
    case MessageContentType::ChatChangePhoto:
    case MessageContentType::ChatDeletePhoto:
    case MessageContentType::ChatDeleteHistory:
    case MessageContentType::ChatAddUsers:
    case MessageContentType::ChatJoinedByLink:
    case MessageContentType::ChatDeleteUser:
    case MessageContentType::ChatMigrateTo:
    case MessageContentType::ChannelCreate:
    case MessageContentType::ChannelMigrateFrom:
    case MessageContentType::PinMessage:
    case MessageContentType::GameScore:
    case MessageContentType::ScreenshotTaken:
    case MessageContentType::ChatSetTtl:
    case MessageContentType::Call:
    case MessageContentType::PaymentSuccessful:
    case MessageContentType::ContactRegistered:
    case MessageContentType::CustomServiceAction:
    case MessageContentType::WebsiteConnected:
    case MessageContentType::PassportDataSent:
    case MessageContentType::PassportDataReceived:
    case MessageContentType::Poll:
    case MessageContentType::Dice:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

bool is_service_message_content(MessageContentType content_type) {
  switch (content_type) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Contact:
    case MessageContentType::Document:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Photo:
    case MessageContentType::Sticker:
    case MessageContentType::Text:
    case MessageContentType::Unsupported:
    case MessageContentType::Venue:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
    case MessageContentType::Poll:
    case MessageContentType::Dice:
      return false;
    case MessageContentType::ChatCreate:
    case MessageContentType::ChatChangeTitle:
    case MessageContentType::ChatChangePhoto:
    case MessageContentType::ChatDeletePhoto:
    case MessageContentType::ChatDeleteHistory:
    case MessageContentType::ChatAddUsers:
    case MessageContentType::ChatJoinedByLink:
    case MessageContentType::ChatDeleteUser:
    case MessageContentType::ChatMigrateTo:
    case MessageContentType::ChannelCreate:
    case MessageContentType::ChannelMigrateFrom:
    case MessageContentType::PinMessage:
    case MessageContentType::GameScore:
    case MessageContentType::ScreenshotTaken:
    case MessageContentType::ChatSetTtl:
    case MessageContentType::Call:
    case MessageContentType::PaymentSuccessful:
    case MessageContentType::ContactRegistered:
    case MessageContentType::CustomServiceAction:
    case MessageContentType::WebsiteConnected:
    case MessageContentType::PassportDataSent:
    case MessageContentType::PassportDataReceived:
      return true;
    default:
      UNREACHABLE();
      return false;
  }
}

bool can_have_message_content_caption(MessageContentType content_type) {
  switch (content_type) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Document:
    case MessageContentType::Photo:
    case MessageContentType::Video:
    case MessageContentType::VoiceNote:
      return true;
    case MessageContentType::Contact:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Sticker:
    case MessageContentType::Text:
    case MessageContentType::Unsupported:
    case MessageContentType::Venue:
    case MessageContentType::VideoNote:
    case MessageContentType::ChatCreate:
    case MessageContentType::ChatChangeTitle:
    case MessageContentType::ChatChangePhoto:
    case MessageContentType::ChatDeletePhoto:
    case MessageContentType::ChatDeleteHistory:
    case MessageContentType::ChatAddUsers:
    case MessageContentType::ChatJoinedByLink:
    case MessageContentType::ChatDeleteUser:
    case MessageContentType::ChatMigrateTo:
    case MessageContentType::ChannelCreate:
    case MessageContentType::ChannelMigrateFrom:
    case MessageContentType::PinMessage:
    case MessageContentType::GameScore:
    case MessageContentType::ScreenshotTaken:
    case MessageContentType::ChatSetTtl:
    case MessageContentType::Call:
    case MessageContentType::PaymentSuccessful:
    case MessageContentType::ContactRegistered:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
    case MessageContentType::CustomServiceAction:
    case MessageContentType::WebsiteConnected:
    case MessageContentType::PassportDataSent:
    case MessageContentType::PassportDataReceived:
    case MessageContentType::Poll:
    case MessageContentType::Dice:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

}  // namespace td
