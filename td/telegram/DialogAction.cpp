//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogAction.h"

namespace td {

void DialogAction::init(Type type) {
  type_ = type;
  progress_ = 0;
}

void DialogAction::init(Type type, int32 progress) {
  if (progress < 0 || progress > 100) {
    progress = 0;
  }
  type_ = type;
  progress_ = progress;
}

DialogAction::DialogAction(Type type, int32 progress) {
  init(type, progress);
}

DialogAction::DialogAction(tl_object_ptr<td_api::ChatAction> &&action) {
  if (action == nullptr) {
    return;
  }

  switch (action->get_id()) {
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
      auto uploading_action = move_tl_object_as<td_api::chatActionUploadingVideo>(action);
      init(Type::UploadingVideo, uploading_action->progress_);
      break;
    }
    case td_api::chatActionRecordingVoiceNote::ID:
      init(Type::RecordingVoiceNote);
      break;
    case td_api::chatActionUploadingVoiceNote::ID: {
      auto uploading_action = move_tl_object_as<td_api::chatActionUploadingVoiceNote>(action);
      init(Type::UploadingVoiceNote, uploading_action->progress_);
      break;
    }
    case td_api::chatActionUploadingPhoto::ID: {
      auto uploading_action = move_tl_object_as<td_api::chatActionUploadingPhoto>(action);
      init(Type::UploadingPhoto, uploading_action->progress_);
      break;
    }
    case td_api::chatActionUploadingDocument::ID: {
      auto uploading_action = move_tl_object_as<td_api::chatActionUploadingDocument>(action);
      init(Type::UploadingDocument, uploading_action->progress_);
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
      auto uploading_action = move_tl_object_as<td_api::chatActionUploadingVideoNote>(action);
      init(Type::UploadingVideoNote, uploading_action->progress_);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

DialogAction::DialogAction(tl_object_ptr<telegram_api::SendMessageAction> &&action) {
  switch (action->get_id()) {
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
      auto upload_video_action = move_tl_object_as<telegram_api::sendMessageUploadVideoAction>(action);
      init(Type::UploadingVideo, upload_video_action->progress_);
      break;
    }
    case telegram_api::sendMessageRecordAudioAction::ID:
      init(Type::RecordingVoiceNote);
      break;
    case telegram_api::sendMessageUploadAudioAction::ID: {
      auto upload_audio_action = move_tl_object_as<telegram_api::sendMessageUploadAudioAction>(action);
      init(Type::UploadingVoiceNote, upload_audio_action->progress_);
      break;
    }
    case telegram_api::sendMessageUploadPhotoAction::ID: {
      auto upload_photo_action = move_tl_object_as<telegram_api::sendMessageUploadPhotoAction>(action);
      init(Type::UploadingPhoto, upload_photo_action->progress_);
      break;
    }
    case telegram_api::sendMessageUploadDocumentAction::ID: {
      auto upload_document_action = move_tl_object_as<telegram_api::sendMessageUploadDocumentAction>(action);
      init(Type::UploadingDocument, upload_document_action->progress_);
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
      auto upload_round_action = move_tl_object_as<telegram_api::sendMessageUploadRoundAction>(action);
      init(Type::UploadingVideoNote, upload_round_action->progress_);
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

tl_object_ptr<telegram_api::SendMessageAction> DialogAction::get_input_send_message_action() const {
  switch (type_) {
    case Type::Cancel:
      return make_tl_object<telegram_api::sendMessageCancelAction>();
    case Type::Typing:
      return make_tl_object<telegram_api::sendMessageTypingAction>();
    case Type::RecordingVideo:
      return make_tl_object<telegram_api::sendMessageRecordVideoAction>();
    case Type::UploadingVideo:
      return make_tl_object<telegram_api::sendMessageUploadVideoAction>(progress_);
    case Type::RecordingVoiceNote:
      return make_tl_object<telegram_api::sendMessageRecordAudioAction>();
    case Type::UploadingVoiceNote:
      return make_tl_object<telegram_api::sendMessageUploadAudioAction>(progress_);
    case Type::UploadingPhoto:
      return make_tl_object<telegram_api::sendMessageUploadPhotoAction>(progress_);
    case Type::UploadingDocument:
      return make_tl_object<telegram_api::sendMessageUploadDocumentAction>(progress_);
    case Type::ChoosingLocation:
      return make_tl_object<telegram_api::sendMessageGeoLocationAction>();
    case Type::ChoosingContact:
      return make_tl_object<telegram_api::sendMessageChooseContactAction>();
    case Type::RecordingVideoNote:
      return make_tl_object<telegram_api::sendMessageRecordRoundAction>();
    case Type::UploadingVideoNote:
      return make_tl_object<telegram_api::sendMessageUploadRoundAction>(progress_);
    case Type::StartPlayingGame:
      return make_tl_object<telegram_api::sendMessageTypingAction>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<secret_api::SendMessageAction> DialogAction::get_secret_input_send_message_action() const {
  switch (type_) {
    case Type::Cancel:
      return make_tl_object<secret_api::sendMessageCancelAction>();
    case Type::Typing:
      return make_tl_object<secret_api::sendMessageTypingAction>();
    case Type::RecordingVideo:
      return make_tl_object<secret_api::sendMessageRecordVideoAction>();
    case Type::UploadingVideo:
      return make_tl_object<secret_api::sendMessageUploadVideoAction>();
    case Type::RecordingVoiceNote:
      return make_tl_object<secret_api::sendMessageRecordAudioAction>();
    case Type::UploadingVoiceNote:
      return make_tl_object<secret_api::sendMessageUploadAudioAction>();
    case Type::UploadingPhoto:
      return make_tl_object<secret_api::sendMessageUploadPhotoAction>();
    case Type::UploadingDocument:
      return make_tl_object<secret_api::sendMessageUploadDocumentAction>();
    case Type::ChoosingLocation:
      return make_tl_object<secret_api::sendMessageGeoLocationAction>();
    case Type::ChoosingContact:
      return make_tl_object<secret_api::sendMessageChooseContactAction>();
    case Type::RecordingVideoNote:
      return make_tl_object<secret_api::sendMessageRecordRoundAction>();
    case Type::UploadingVideoNote:
      return make_tl_object<secret_api::sendMessageUploadRoundAction>();
    case Type::StartPlayingGame:
      return make_tl_object<secret_api::sendMessageTypingAction>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<td_api::ChatAction> DialogAction::get_chat_action_object() const {
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
    default:
      UNREACHABLE();
      return td_api::make_object<td_api::chatActionCancel>();
  }
}

bool DialogAction::is_cancelled_by_message_of_type(MessageContentType message_content_type) const {
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
    case MessageContentType::VideoNote:
      return type_ == Type::RecordingVideoNote || type_ == Type::UploadingVideoNote;
    case MessageContentType::VoiceNote:
      return type_ == Type::RecordingVoiceNote || type_ == Type::UploadingVoiceNote;
    case MessageContentType::Contact:
      return type_ == Type::ChoosingContact;
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Venue:
      return type_ == Type::ChoosingLocation;
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::Sticker:
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
      default:
        UNREACHABLE();
        return "Cancel";
    }
  }();
  string_builder << type << "Action";
  if (action.progress_ != 0) {
    string_builder << '(' << action.progress_ << "%)";
  }
  return string_builder;
}

}  // namespace td
