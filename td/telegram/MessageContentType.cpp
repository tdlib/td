//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
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
    case MessageContentType::ProximityAlertTriggered:
      return string_builder << "ProximityAlertTriggered";
    case MessageContentType::GroupCall:
      return string_builder << "GroupCall";
    case MessageContentType::InviteToGroupCall:
      return string_builder << "InviteToGroupCall";
    case MessageContentType::ChatSetTheme:
      return string_builder << "ChatSetTheme";
    case MessageContentType::WebViewDataSent:
      return string_builder << "WebViewDataSent";
    case MessageContentType::WebViewDataReceived:
      return string_builder << "WebViewDataReceived";
    case MessageContentType::GiftPremium:
      return string_builder << "GiftPremium";
    case MessageContentType::TopicCreate:
      return string_builder << "TopicCreate";
    case MessageContentType::TopicEdit:
      return string_builder << "TopicEdit";
    case MessageContentType::SuggestProfilePhoto:
      return string_builder << "SuggestProfilePhoto";
    case MessageContentType::WriteAccessAllowed:
      return string_builder << "WriteAccessAllowed";
    case MessageContentType::RequestedDialog:
      return string_builder << "ChatShared";
    case MessageContentType::WebViewWriteAccessAllowed:
      return string_builder << "WebAppWriteAccessAllowed";
    case MessageContentType::SetBackground:
      return string_builder << "SetBackground";
    case MessageContentType::Story:
      return string_builder << "Story";
    case MessageContentType::WriteAccessAllowedByRequest:
      return string_builder << "WriteAccessAllowedByRequest";
    case MessageContentType::GiftCode:
      return string_builder << "GiftCode";
    case MessageContentType::Giveaway:
      return string_builder << "Giveaway";
    case MessageContentType::GiveawayLaunch:
      return string_builder << "GiveawayLaunch";
    default:
      return string_builder << "Invalid type " << static_cast<int32>(content_type);
  }
}

bool is_allowed_media_group_content(MessageContentType content_type) {
  switch (content_type) {
    case MessageContentType::Audio:
    case MessageContentType::Document:
    case MessageContentType::Photo:
    case MessageContentType::Video:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
      return true;
    case MessageContentType::Animation:
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
    case MessageContentType::ProximityAlertTriggered:
    case MessageContentType::GroupCall:
    case MessageContentType::InviteToGroupCall:
    case MessageContentType::ChatSetTheme:
    case MessageContentType::WebViewDataSent:
    case MessageContentType::WebViewDataReceived:
    case MessageContentType::GiftPremium:
    case MessageContentType::TopicCreate:
    case MessageContentType::TopicEdit:
    case MessageContentType::SuggestProfilePhoto:
    case MessageContentType::WriteAccessAllowed:
    case MessageContentType::RequestedDialog:
    case MessageContentType::WebViewWriteAccessAllowed:
    case MessageContentType::SetBackground:
    case MessageContentType::Story:
    case MessageContentType::WriteAccessAllowedByRequest:
    case MessageContentType::GiftCode:
    case MessageContentType::Giveaway:
    case MessageContentType::GiveawayLaunch:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

bool is_homogenous_media_group_content(MessageContentType content_type) {
  return content_type == MessageContentType::Audio || content_type == MessageContentType::Document;
}

bool is_secret_message_content(int32 ttl, MessageContentType content_type) {
  if (ttl <= 0 || ttl > 60) {
    return ttl == 0x7FFFFFFF;
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
    case MessageContentType::ProximityAlertTriggered:
    case MessageContentType::GroupCall:
    case MessageContentType::InviteToGroupCall:
    case MessageContentType::ChatSetTheme:
    case MessageContentType::WebViewDataSent:
    case MessageContentType::WebViewDataReceived:
    case MessageContentType::GiftPremium:
    case MessageContentType::TopicCreate:
    case MessageContentType::TopicEdit:
    case MessageContentType::SuggestProfilePhoto:
    case MessageContentType::WriteAccessAllowed:
    case MessageContentType::RequestedDialog:
    case MessageContentType::WebViewWriteAccessAllowed:
    case MessageContentType::SetBackground:
    case MessageContentType::Story:
    case MessageContentType::WriteAccessAllowedByRequest:
    case MessageContentType::GiftCode:
    case MessageContentType::Giveaway:
    case MessageContentType::GiveawayLaunch:
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
    case MessageContentType::Dice:
    case MessageContentType::Document:
    case MessageContentType::Game:
    case MessageContentType::Giveaway:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Photo:
    case MessageContentType::Poll:
    case MessageContentType::Sticker:
    case MessageContentType::Story:
    case MessageContentType::Text:
    case MessageContentType::Unsupported:
    case MessageContentType::Venue:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
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
    case MessageContentType::ProximityAlertTriggered:
    case MessageContentType::GroupCall:
    case MessageContentType::InviteToGroupCall:
    case MessageContentType::ChatSetTheme:
    case MessageContentType::WebViewDataSent:
    case MessageContentType::WebViewDataReceived:
    case MessageContentType::GiftPremium:
    case MessageContentType::TopicCreate:
    case MessageContentType::TopicEdit:
    case MessageContentType::SuggestProfilePhoto:
    case MessageContentType::WriteAccessAllowed:
    case MessageContentType::RequestedDialog:
    case MessageContentType::WebViewWriteAccessAllowed:
    case MessageContentType::SetBackground:
    case MessageContentType::WriteAccessAllowedByRequest:
    case MessageContentType::GiftCode:
    case MessageContentType::GiveawayLaunch:
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
    case MessageContentType::ProximityAlertTriggered:
    case MessageContentType::GroupCall:
    case MessageContentType::InviteToGroupCall:
    case MessageContentType::ChatSetTheme:
    case MessageContentType::WebViewDataSent:
    case MessageContentType::WebViewDataReceived:
    case MessageContentType::GiftPremium:
    case MessageContentType::TopicCreate:
    case MessageContentType::TopicEdit:
    case MessageContentType::SuggestProfilePhoto:
    case MessageContentType::WriteAccessAllowed:
    case MessageContentType::RequestedDialog:
    case MessageContentType::WebViewWriteAccessAllowed:
    case MessageContentType::SetBackground:
    case MessageContentType::Story:
    case MessageContentType::WriteAccessAllowedByRequest:
    case MessageContentType::GiftCode:
    case MessageContentType::Giveaway:
    case MessageContentType::GiveawayLaunch:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

uint64 get_message_content_chain_id(MessageContentType content_type) {
  switch (content_type) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Document:
    case MessageContentType::Invoice:
    case MessageContentType::Photo:
    case MessageContentType::Sticker:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
      return 1;
    default:
      return 2;
  }
}

}  // namespace td
