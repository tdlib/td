//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogAction.h"

#include "td/telegram/misc.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/emoji.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"

namespace td {

bool DialogAction::is_valid_emoji(string &emoji) {
  if (!clean_input_string(emoji)) {
    return false;
  }
  return is_emoji(emoji);
}

void DialogAction::init(Type type) {
  type_ = type;
  progress_ = 0;
  emoji_.clear();
}

void DialogAction::init(Type type, int32 progress) {
  type_ = type;
  progress_ = clamp(progress, 0, 100);
  emoji_.clear();
}

void DialogAction::init(Type type, string emoji) {
  if (is_valid_emoji(emoji)) {
    type_ = type;
    progress_ = 0;
    emoji_ = std::move(emoji);
  } else {
    init(Type::Cancel);
  }
}

void DialogAction::init(Type type, int32 message_id, string emoji, const string &data) {
  if (ServerMessageId(message_id).is_valid() && is_valid_emoji(emoji) && check_utf8(data)) {
    type_ = type;
    progress_ = message_id;
    emoji_ = PSTRING() << emoji << '\xFF' << data;
  } else {
    init(Type::Cancel);
  }
}

void DialogAction::init(Type type, int64 random_id, FormattedText &&text) {
  CHECK(type == Type::TextDraft);
  type_ = type;
  random_id_ = random_id;
  text_ = std::move(text);
}

DialogAction::DialogAction(Type type, int32 progress) {
  init(type, progress);
}

DialogAction::DialogAction(td_api::object_ptr<td_api::ChatAction> &&action_ptr) {
  if (action_ptr == nullptr) {
    return;
  }

  switch (action_ptr->get_id()) {
    case td_api::chatActionCancel::ID:
      init(Type::Cancel);
      break;
    case td_api::chatActionTyping::ID:
      init(Type::Typing);
      break;
    case td_api::chatActionRecordingVideo::ID:
      init(Type::RecordingVideo);
      break;
    case td_api::chatActionUploadingVideo::ID: {
      auto action = td_api::move_object_as<td_api::chatActionUploadingVideo>(action_ptr);
      init(Type::UploadingVideo, action->progress_);
      break;
    }
    case td_api::chatActionRecordingVoiceNote::ID:
      init(Type::RecordingVoiceNote);
      break;
    case td_api::chatActionUploadingVoiceNote::ID: {
      auto action = td_api::move_object_as<td_api::chatActionUploadingVoiceNote>(action_ptr);
      init(Type::UploadingVoiceNote, action->progress_);
      break;
    }
    case td_api::chatActionUploadingPhoto::ID: {
      auto action = td_api::move_object_as<td_api::chatActionUploadingPhoto>(action_ptr);
      init(Type::UploadingPhoto, action->progress_);
      break;
    }
    case td_api::chatActionUploadingDocument::ID: {
      auto action = td_api::move_object_as<td_api::chatActionUploadingDocument>(action_ptr);
      init(Type::UploadingDocument, action->progress_);
      break;
    }
    case td_api::chatActionChoosingLocation::ID:
      init(Type::ChoosingLocation);
      break;
    case td_api::chatActionChoosingContact::ID:
      init(Type::ChoosingContact);
      break;
    case td_api::chatActionStartPlayingGame::ID:
      init(Type::StartPlayingGame);
      break;
    case td_api::chatActionRecordingVideoNote::ID:
      init(Type::RecordingVideoNote);
      break;
    case td_api::chatActionUploadingVideoNote::ID: {
      auto action = td_api::move_object_as<td_api::chatActionUploadingVideoNote>(action_ptr);
      init(Type::UploadingVideoNote, action->progress_);
      break;
    }
    case td_api::chatActionChoosingSticker::ID:
      init(Type::ChoosingSticker);
      break;
    case td_api::chatActionWatchingAnimations::ID: {
      auto action = td_api::move_object_as<td_api::chatActionWatchingAnimations>(action_ptr);
      init(Type::WatchingAnimations, std::move(action->emoji_));
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

DialogAction::DialogAction(const UserManager *user_manager,
                           telegram_api::object_ptr<telegram_api::SendMessageAction> &&action_ptr) {
  switch (action_ptr->get_id()) {
    case telegram_api::sendMessageCancelAction::ID:
      init(Type::Cancel);
      break;
    case telegram_api::sendMessageTypingAction::ID:
      init(Type::Typing);
      break;
    case telegram_api::sendMessageRecordVideoAction::ID:
      init(Type::RecordingVideo);
      break;
    case telegram_api::sendMessageUploadVideoAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageUploadVideoAction>(action_ptr);
      init(Type::UploadingVideo, action->progress_);
      break;
    }
    case telegram_api::sendMessageRecordAudioAction::ID:
      init(Type::RecordingVoiceNote);
      break;
    case telegram_api::sendMessageUploadAudioAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageUploadAudioAction>(action_ptr);
      init(Type::UploadingVoiceNote, action->progress_);
      break;
    }
    case telegram_api::sendMessageUploadPhotoAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageUploadPhotoAction>(action_ptr);
      init(Type::UploadingPhoto, action->progress_);
      break;
    }
    case telegram_api::sendMessageUploadDocumentAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageUploadDocumentAction>(action_ptr);
      init(Type::UploadingDocument, action->progress_);
      break;
    }
    case telegram_api::sendMessageGeoLocationAction::ID:
      init(Type::ChoosingLocation);
      break;
    case telegram_api::sendMessageChooseContactAction::ID:
      init(Type::ChoosingContact);
      break;
    case telegram_api::sendMessageGamePlayAction::ID:
      init(Type::StartPlayingGame);
      break;
    case telegram_api::sendMessageRecordRoundAction::ID:
      init(Type::RecordingVideoNote);
      break;
    case telegram_api::sendMessageUploadRoundAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageUploadRoundAction>(action_ptr);
      init(Type::UploadingVideoNote, action->progress_);
      break;
    }
    case telegram_api::speakingInGroupCallAction::ID:
      init(Type::SpeakingInVoiceChat);
      break;
    case telegram_api::sendMessageHistoryImportAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageHistoryImportAction>(action_ptr);
      init(Type::ImportingMessages, action->progress_);
      break;
    }
    case telegram_api::sendMessageChooseStickerAction::ID:
      init(Type::ChoosingSticker);
      break;
    case telegram_api::sendMessageEmojiInteractionSeen::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageEmojiInteractionSeen>(action_ptr);
      init(Type::WatchingAnimations, std::move(action->emoticon_));
      break;
    }
    case telegram_api::sendMessageEmojiInteraction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageEmojiInteraction>(action_ptr);
      init(Type::ClickingAnimatedEmoji, action->msg_id_, std::move(action->emoticon_), action->interaction_->data_);
      break;
    }
    case telegram_api::sendMessageTextDraftAction::ID: {
      auto action = telegram_api::move_object_as<telegram_api::sendMessageTextDraftAction>(action_ptr);
      init(Type::TextDraft, action->random_id_,
           get_formatted_text(user_manager, std::move(action->text_), true, false, "sendMessageTextDraftAction"));
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

tl_object_ptr<telegram_api::SendMessageAction> DialogAction::get_input_send_message_action(
    const UserManager *user_manager) const {
  switch (type_) {
    case Type::Cancel:
      return telegram_api::make_object<telegram_api::sendMessageCancelAction>();
    case Type::Typing:
      return telegram_api::make_object<telegram_api::sendMessageTypingAction>();
    case Type::RecordingVideo:
      return telegram_api::make_object<telegram_api::sendMessageRecordVideoAction>();
    case Type::UploadingVideo:
      return telegram_api::make_object<telegram_api::sendMessageUploadVideoAction>(progress_);
    case Type::RecordingVoiceNote:
      return telegram_api::make_object<telegram_api::sendMessageRecordAudioAction>();
    case Type::UploadingVoiceNote:
      return telegram_api::make_object<telegram_api::sendMessageUploadAudioAction>(progress_);
    case Type::UploadingPhoto:
      return telegram_api::make_object<telegram_api::sendMessageUploadPhotoAction>(progress_);
    case Type::UploadingDocument:
      return telegram_api::make_object<telegram_api::sendMessageUploadDocumentAction>(progress_);
    case Type::ChoosingLocation:
      return telegram_api::make_object<telegram_api::sendMessageGeoLocationAction>();
    case Type::ChoosingContact:
      return telegram_api::make_object<telegram_api::sendMessageChooseContactAction>();
    case Type::StartPlayingGame:
      return telegram_api::make_object<telegram_api::sendMessageGamePlayAction>();
    case Type::RecordingVideoNote:
      return telegram_api::make_object<telegram_api::sendMessageRecordRoundAction>();
    case Type::UploadingVideoNote:
      return telegram_api::make_object<telegram_api::sendMessageUploadRoundAction>(progress_);
    case Type::SpeakingInVoiceChat:
      return telegram_api::make_object<telegram_api::speakingInGroupCallAction>();
    case Type::ImportingMessages:
      return telegram_api::make_object<telegram_api::sendMessageHistoryImportAction>(progress_);
    case Type::ChoosingSticker:
      return telegram_api::make_object<telegram_api::sendMessageChooseStickerAction>();
    case Type::WatchingAnimations:
      return telegram_api::make_object<telegram_api::sendMessageEmojiInteractionSeen>(emoji_);
    case Type::TextDraft:
      return telegram_api::make_object<telegram_api::sendMessageTextDraftAction>(
          random_id_, get_input_text_with_entities(user_manager, text_, "sendMessageTextDraftAction"));
    case Type::ClickingAnimatedEmoji:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<secret_api::SendMessageAction> DialogAction::get_secret_input_send_message_action() const {
  switch (type_) {
    case Type::Cancel:
      return secret_api::make_object<secret_api::sendMessageCancelAction>();
    case Type::Typing:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::RecordingVideo:
      return secret_api::make_object<secret_api::sendMessageRecordVideoAction>();
    case Type::UploadingVideo:
      return secret_api::make_object<secret_api::sendMessageUploadVideoAction>();
    case Type::RecordingVoiceNote:
      return secret_api::make_object<secret_api::sendMessageRecordAudioAction>();
    case Type::UploadingVoiceNote:
      return secret_api::make_object<secret_api::sendMessageUploadAudioAction>();
    case Type::UploadingPhoto:
      return secret_api::make_object<secret_api::sendMessageUploadPhotoAction>();
    case Type::UploadingDocument:
      return secret_api::make_object<secret_api::sendMessageUploadDocumentAction>();
    case Type::ChoosingLocation:
      return secret_api::make_object<secret_api::sendMessageGeoLocationAction>();
    case Type::ChoosingContact:
      return secret_api::make_object<secret_api::sendMessageChooseContactAction>();
    case Type::StartPlayingGame:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::RecordingVideoNote:
      return secret_api::make_object<secret_api::sendMessageRecordRoundAction>();
    case Type::UploadingVideoNote:
      return secret_api::make_object<secret_api::sendMessageUploadRoundAction>();
    case Type::SpeakingInVoiceChat:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::ImportingMessages:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::ChoosingSticker:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::WatchingAnimations:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::TextDraft:
      return secret_api::make_object<secret_api::sendMessageTypingAction>();
    case Type::ClickingAnimatedEmoji:
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<td_api::ChatAction> DialogAction::get_chat_action_object(const UserManager *user_manager) const {
  switch (type_) {
    case Type::Cancel:
      return td_api::make_object<td_api::chatActionCancel>();
    case Type::Typing:
      return td_api::make_object<td_api::chatActionTyping>();
    case Type::RecordingVideo:
      return td_api::make_object<td_api::chatActionRecordingVideo>();
    case Type::UploadingVideo:
      return td_api::make_object<td_api::chatActionUploadingVideo>(progress_);
    case Type::RecordingVoiceNote:
      return td_api::make_object<td_api::chatActionRecordingVoiceNote>();
    case Type::UploadingVoiceNote:
      return td_api::make_object<td_api::chatActionUploadingVoiceNote>(progress_);
    case Type::UploadingPhoto:
      return td_api::make_object<td_api::chatActionUploadingPhoto>(progress_);
    case Type::UploadingDocument:
      return td_api::make_object<td_api::chatActionUploadingDocument>(progress_);
    case Type::ChoosingLocation:
      return td_api::make_object<td_api::chatActionChoosingLocation>();
    case Type::ChoosingContact:
      return td_api::make_object<td_api::chatActionChoosingContact>();
    case Type::StartPlayingGame:
      return td_api::make_object<td_api::chatActionStartPlayingGame>();
    case Type::RecordingVideoNote:
      return td_api::make_object<td_api::chatActionRecordingVideoNote>();
    case Type::UploadingVideoNote:
      return td_api::make_object<td_api::chatActionUploadingVideoNote>(progress_);
    case Type::ChoosingSticker:
      return td_api::make_object<td_api::chatActionChoosingSticker>();
    case Type::WatchingAnimations:
      return td_api::make_object<td_api::chatActionWatchingAnimations>(emoji_);
    case Type::TextDraft:
    case Type::ImportingMessages:
    case Type::SpeakingInVoiceChat:
    case Type::ClickingAnimatedEmoji:
    default:
      UNREACHABLE();
      return td_api::make_object<td_api::chatActionCancel>();
  }
}

bool DialogAction::is_canceled_by_message_of_type(MessageContentType message_content_type) const {
  if (message_content_type == MessageContentType::None) {
    return true;
  }

  if (type_ == Type::Typing) {
    return message_content_type == MessageContentType::Text || message_content_type == MessageContentType::Game ||
           can_have_message_content_caption(message_content_type);
  }

  switch (message_content_type) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Document:
      return type_ == Type::UploadingDocument;
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::Photo:
      return type_ == Type::UploadingPhoto;
    case MessageContentType::ExpiredVideo:
    case MessageContentType::Video:
      return type_ == Type::RecordingVideo || type_ == Type::UploadingVideo;
    case MessageContentType::ExpiredVideoNote:
    case MessageContentType::VideoNote:
      return type_ == Type::RecordingVideoNote || type_ == Type::UploadingVideoNote;
    case MessageContentType::ExpiredVoiceNote:
    case MessageContentType::VoiceNote:
      return type_ == Type::RecordingVoiceNote || type_ == Type::UploadingVoiceNote;
    case MessageContentType::Contact:
      return type_ == Type::ChoosingContact;
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Venue:
      return type_ == Type::ChoosingLocation;
    case MessageContentType::Sticker:
      return type_ == Type::ChoosingSticker;
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::PaidMedia:
    case MessageContentType::Text:
    case MessageContentType::Unsupported:
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
    case MessageContentType::GiveawayResults:
    case MessageContentType::GiveawayWinners:
    case MessageContentType::BoostApply:
    case MessageContentType::DialogShared:
    case MessageContentType::PaymentRefunded:
    case MessageContentType::GiftStars:
    case MessageContentType::PrizeStars:
    case MessageContentType::StarGift:
    case MessageContentType::StarGiftUnique:
    case MessageContentType::PaidMessagesRefunded:
    case MessageContentType::PaidMessagesPrice:
    case MessageContentType::ConferenceCall:
    case MessageContentType::ToDoList:
    case MessageContentType::TodoCompletions:
    case MessageContentType::TodoAppendTasks:
    case MessageContentType::GiftTon:
    case MessageContentType::SuggestedPostSuccess:
    case MessageContentType::SuggestedPostRefund:
    case MessageContentType::SuggestedPostApproval:
    case MessageContentType::SuggestBirthday:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

DialogAction DialogAction::get_uploading_action(MessageContentType message_content_type, int32 progress) {
  switch (message_content_type) {
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Document:
    case MessageContentType::PaidMedia:
      return DialogAction(Type::UploadingDocument, progress);
    case MessageContentType::Photo:
      return DialogAction(Type::UploadingPhoto, progress);
    case MessageContentType::Video:
      return DialogAction(Type::UploadingVideo, progress);
    case MessageContentType::VideoNote:
      return DialogAction(Type::UploadingVideoNote, progress);
    case MessageContentType::VoiceNote:
      return DialogAction(Type::UploadingVoiceNote, progress);
    default:
      return DialogAction();
  }
}

DialogAction DialogAction::get_typing_action() {
  return DialogAction(Type::Typing, 0);
}

DialogAction DialogAction::get_speaking_action() {
  return DialogAction(Type::SpeakingInVoiceChat, 0);
}

int32 DialogAction::get_importing_messages_action_progress() const {
  if (type_ != Type::ImportingMessages) {
    return -1;
  }
  return progress_;
}

string DialogAction::get_watching_animations_emoji() const {
  if (type_ == Type::WatchingAnimations) {
    return emoji_;
  }
  return string();
}

DialogAction::ClickingAnimateEmojiInfo DialogAction::get_clicking_animated_emoji_action_info() const {
  ClickingAnimateEmojiInfo result;
  if (type_ == Type::ClickingAnimatedEmoji) {
    auto pos = emoji_.find('\xFF');
    CHECK(pos < emoji_.size());
    result.message_id = progress_;
    result.emoji = emoji_.substr(0, pos);
    result.data = emoji_.substr(pos + 1);
  }
  return result;
}

DialogAction::TextDraftInfo DialogAction::get_text_draft_info() const {
  TextDraftInfo result;
  if (type_ == Type::TextDraft) {
    result.is_text_draft_ = true;
    result.random_id_ = random_id_;
    result.text_ = text_;
  }
  return result;
}

StringBuilder &operator<<(StringBuilder &string_builder, const DialogAction &action) {
  string_builder << "ChatAction";
  const char *type = [action_type = action.type_] {
    switch (action_type) {
      case DialogAction::Type::Cancel:
        return "Cancel";
      case DialogAction::Type::Typing:
        return "Typing";
      case DialogAction::Type::RecordingVideo:
        return "RecordingVideo";
      case DialogAction::Type::UploadingVideo:
        return "UploadingVideo";
      case DialogAction::Type::RecordingVoiceNote:
        return "RecordingVoiceNote";
      case DialogAction::Type::UploadingVoiceNote:
        return "UploadingVoiceNote";
      case DialogAction::Type::UploadingPhoto:
        return "UploadingPhoto";
      case DialogAction::Type::UploadingDocument:
        return "UploadingDocument";
      case DialogAction::Type::ChoosingLocation:
        return "ChoosingLocation";
      case DialogAction::Type::ChoosingContact:
        return "ChoosingContact";
      case DialogAction::Type::StartPlayingGame:
        return "StartPlayingGame";
      case DialogAction::Type::RecordingVideoNote:
        return "RecordingVideoNote";
      case DialogAction::Type::UploadingVideoNote:
        return "UploadingVideoNote";
      case DialogAction::Type::SpeakingInVoiceChat:
        return "SpeakingInVoiceChat";
      case DialogAction::Type::ImportingMessages:
        return "ImportingMessages";
      case DialogAction::Type::ChoosingSticker:
        return "ChoosingSticker";
      case DialogAction::Type::WatchingAnimations:
        return "WatchingAnimations";
      case DialogAction::Type::ClickingAnimatedEmoji:
        return "ClickingAnimatedEmoji";
      case DialogAction::Type::TextDraft:
        return "SendingTextDraft";
      default:
        UNREACHABLE();
        return "Cancel";
    }
  }();
  string_builder << type << "Action";
  if (action.type_ == DialogAction::Type::ClickingAnimatedEmoji) {
    auto pos = action.emoji_.find('\xFF');
    CHECK(pos < action.emoji_.size());
    string_builder << '(' << action.progress_ << ")(" << Slice(action.emoji_).substr(0, pos) << ")("
                   << Slice(action.emoji_).substr(pos + 1) << ')';
  } else {
    if (action.progress_ != 0) {
      string_builder << '(' << action.progress_ << "%)";
    }
    if (!action.emoji_.empty()) {
      string_builder << '(' << action.emoji_ << ')';
    }
    if (action.type_ == DialogAction::Type::TextDraft) {
      string_builder << '(' << action.random_id_ << ": " << action.text_ << ')';
    }
  }
  return string_builder;
}

}  // namespace td
