//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/NotificationType.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VoiceNotesManager.h"

#include "td/utils/misc.h"
#include "td/utils/Slice.h"

#include <tuple>

namespace td {

class NotificationTypeMessage : public NotificationType {
  bool can_be_delayed() const override {
    return message_id_.is_valid() && message_id_.is_server();
  }

  bool is_temporary() const override {
    return false;
  }

  MessageId get_message_id() const override {
    return message_id_;
  }

  vector<FileId> get_file_ids(const Td *td) const override {
    return {};
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    auto message_object = G()->td().get_actor_unsafe()->messages_manager_->get_message_object({dialog_id, message_id_});
    if (message_object == nullptr) {
      return nullptr;
    }
    return td_api::make_object<td_api::notificationTypeNewMessage>(std::move(message_object));
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewMessageNotification[" << message_id_ << ']';
  }

  MessageId message_id_;

 public:
  explicit NotificationTypeMessage(MessageId message_id) : message_id_(message_id) {
  }
};

class NotificationTypeSecretChat : public NotificationType {
  bool can_be_delayed() const override {
    return false;
  }

  bool is_temporary() const override {
    return false;
  }

  MessageId get_message_id() const override {
    return MessageId();
  }

  vector<FileId> get_file_ids(const Td *td) const override {
    return {};
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    return td_api::make_object<td_api::notificationTypeNewSecretChat>();
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewSecretChatNotification[]";
  }

 public:
  NotificationTypeSecretChat() {
  }
};

class NotificationTypeCall : public NotificationType {
  bool can_be_delayed() const override {
    return false;
  }

  bool is_temporary() const override {
    return false;
  }

  MessageId get_message_id() const override {
    return MessageId::max();
  }

  vector<FileId> get_file_ids(const Td *td) const override {
    return {};
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    return td_api::make_object<td_api::notificationTypeNewCall>(call_id_.get());
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewCallNotification[" << call_id_ << ']';
  }

  CallId call_id_;

 public:
  explicit NotificationTypeCall(CallId call_id) : call_id_(call_id) {
  }
};

class NotificationTypePushMessage : public NotificationType {
  bool can_be_delayed() const override {
    return false;
  }

  bool is_temporary() const override {
    return true;
  }

  MessageId get_message_id() const override {
    return message_id_;
  }

  vector<FileId> get_file_ids(const Td *td) const override {
    if (!document_.empty()) {
      return document_.get_file_ids(td);
    }

    return photo_get_file_ids(photo_);
  }

  static td_api::object_ptr<td_api::PushMessageContent> get_push_message_content_object(Slice key, const string &arg,
                                                                                        const Photo &photo,
                                                                                        const Document &document) {
    bool is_pinned = false;
    if (begins_with(key, "PINNED_")) {
      is_pinned = true;
      key = key.substr(7);
    }
    if (key == "MESSAGE") {
      return td_api::make_object<td_api::pushMessageContentHidden>(is_pinned);
    }
    if (key == "MESSAGES") {
      return td_api::make_object<td_api::pushMessageContentMediaAlbum>(to_integer<int32>(arg), true, true, false,
                                                                       false);
    }
    CHECK(key.size() > 8);
    switch (key[8]) {
      case 'A':
        if (key == "MESSAGE_ANIMATION") {
          auto animations_manager = G()->td().get_actor_unsafe()->animations_manager_.get();
          return td_api::make_object<td_api::pushMessageContentAnimation>(
              animations_manager->get_animation_object(document.file_id, "MESSAGE_ANIMATION"), arg, is_pinned);
        }
        if (key == "MESSAGE_AUDIO") {
          auto audios_manager = G()->td().get_actor_unsafe()->audios_manager_.get();
          return td_api::make_object<td_api::pushMessageContentAudio>(
              audios_manager->get_audio_object(document.file_id), is_pinned);
        }
        if (key == "MESSAGE_AUDIOS") {
          return td_api::make_object<td_api::pushMessageContentMediaAlbum>(to_integer<int32>(arg), false, false, true,
                                                                           false);
        }
        break;
      case 'B':
        if (key == "MESSAGE_BASIC_GROUP_CHAT_CREATE") {
          return td_api::make_object<td_api::pushMessageContentBasicGroupChatCreate>();
        }
        break;
      case 'C':
        if (key == "MESSAGE_CHAT_ADD_MEMBERS") {
          return td_api::make_object<td_api::pushMessageContentChatAddMembers>(arg, false, false);
        }
        if (key == "MESSAGE_CHAT_ADD_MEMBERS_RETURNED") {
          return td_api::make_object<td_api::pushMessageContentChatAddMembers>(arg, false, true);
        }
        if (key == "MESSAGE_CHAT_ADD_MEMBERS_YOU") {
          return td_api::make_object<td_api::pushMessageContentChatAddMembers>(arg, true, false);
        }
        if (key == "MESSAGE_CHAT_CHANGE_PHOTO") {
          return td_api::make_object<td_api::pushMessageContentChatChangePhoto>();
        }
        if (key == "MESSAGE_CHAT_CHANGE_TITLE") {
          return td_api::make_object<td_api::pushMessageContentChatChangeTitle>(arg);
        }
        if (key == "MESSAGE_CHAT_DELETE_MEMBER") {
          return td_api::make_object<td_api::pushMessageContentChatDeleteMember>(arg, false, false);
        }
        if (key == "MESSAGE_CHAT_DELETE_MEMBER_LEFT") {
          return td_api::make_object<td_api::pushMessageContentChatDeleteMember>(arg, false, true);
        }
        if (key == "MESSAGE_CHAT_DELETE_MEMBER_YOU") {
          return td_api::make_object<td_api::pushMessageContentChatDeleteMember>(arg, true, false);
        }
        if (key == "MESSAGE_CHAT_JOIN_BY_LINK") {
          return td_api::make_object<td_api::pushMessageContentChatJoinByLink>();
        }
        if (key == "MESSAGE_CONTACT") {
          return td_api::make_object<td_api::pushMessageContentContact>(arg, is_pinned);
        }
        if (key == "MESSAGE_CONTACT_REGISTERED") {
          return td_api::make_object<td_api::pushMessageContentContactRegistered>();
        }
        break;
      case 'D':
        if (key == "MESSAGE_DOCUMENT") {
          auto documents_manager = G()->td().get_actor_unsafe()->documents_manager_.get();
          return td_api::make_object<td_api::pushMessageContentDocument>(
              documents_manager->get_document_object(document.file_id, PhotoFormat::Jpeg), is_pinned);
        }
        if (key == "MESSAGE_DOCUMENTS") {
          return td_api::make_object<td_api::pushMessageContentMediaAlbum>(to_integer<int32>(arg), false, false, false,
                                                                           true);
        }
        break;
      case 'F':
        if (key == "MESSAGE_FORWARDS") {
          return td_api::make_object<td_api::pushMessageContentMessageForwards>(to_integer<int32>(arg));
        }
        break;
      case 'G':
        if (key == "MESSAGE_GAME") {
          return td_api::make_object<td_api::pushMessageContentGame>(arg, is_pinned);
        }
        if (key == "MESSAGE_GAME_SCORE") {
          int32 score = 0;
          string title;
          if (!is_pinned) {
            string score_str;
            std::tie(score_str, title) = split(arg);
            score = to_integer<int32>(score_str);
          }
          return td_api::make_object<td_api::pushMessageContentGameScore>(title, score, is_pinned);
        }
        break;
      case 'I':
        if (key == "MESSAGE_INVOICE") {
          return td_api::make_object<td_api::pushMessageContentInvoice>(arg, is_pinned);
        }
        break;
      case 'L':
        if (key == "MESSAGE_LIVE_LOCATION") {
          return td_api::make_object<td_api::pushMessageContentLocation>(false, is_pinned);
        }
        if (key == "MESSAGE_LOCATION") {
          return td_api::make_object<td_api::pushMessageContentLocation>(true, is_pinned);
        }
        break;
      case 'P':
        if (key == "MESSAGE_PHOTO") {
          auto file_manager = G()->td().get_actor_unsafe()->file_manager_.get();
          return td_api::make_object<td_api::pushMessageContentPhoto>(get_photo_object(file_manager, photo), arg, false,
                                                                      is_pinned);
        }
        if (key == "MESSAGE_PHOTOS") {
          return td_api::make_object<td_api::pushMessageContentMediaAlbum>(to_integer<int32>(arg), true, false, false,
                                                                           false);
        }
        if (key == "MESSAGE_POLL") {
          return td_api::make_object<td_api::pushMessageContentPoll>(arg, true, is_pinned);
        }
        break;
      case 'Q':
        if (key == "MESSAGE_QUIZ") {
          return td_api::make_object<td_api::pushMessageContentPoll>(arg, false, is_pinned);
        }
        break;
      case 'S':
        if (key == "MESSAGE_SECRET_PHOTO") {
          return td_api::make_object<td_api::pushMessageContentPhoto>(nullptr, arg, true, false);
        }
        if (key == "MESSAGE_SECRET_VIDEO") {
          return td_api::make_object<td_api::pushMessageContentVideo>(nullptr, arg, true, false);
        }
        if (key == "MESSAGE_SCREENSHOT_TAKEN") {
          return td_api::make_object<td_api::pushMessageContentScreenshotTaken>();
        }
        if (key == "MESSAGE_STICKER") {
          auto stickers_manager = G()->td().get_actor_unsafe()->stickers_manager_.get();
          return td_api::make_object<td_api::pushMessageContentSticker>(
              stickers_manager->get_sticker_object(document.file_id), trim(arg), is_pinned);
        }
        break;
      case 'T':
        if (key == "MESSAGE_TEXT") {
          return td_api::make_object<td_api::pushMessageContentText>(arg, is_pinned);
        }
        break;
      case 'V':
        if (key == "MESSAGE_VIDEO") {
          auto videos_manager = G()->td().get_actor_unsafe()->videos_manager_.get();
          return td_api::make_object<td_api::pushMessageContentVideo>(
              videos_manager->get_video_object(document.file_id), arg, false, is_pinned);
        }
        if (key == "MESSAGE_VIDEO_NOTE") {
          auto video_notes_manager = G()->td().get_actor_unsafe()->video_notes_manager_.get();
          return td_api::make_object<td_api::pushMessageContentVideoNote>(
              video_notes_manager->get_video_note_object(document.file_id), is_pinned);
        }
        if (key == "MESSAGE_VIDEOS") {
          return td_api::make_object<td_api::pushMessageContentMediaAlbum>(to_integer<int32>(arg), false, true, false,
                                                                           false);
        }
        if (key == "MESSAGE_VOICE_NOTE") {
          auto voice_notes_manager = G()->td().get_actor_unsafe()->voice_notes_manager_.get();
          return td_api::make_object<td_api::pushMessageContentVoiceNote>(
              voice_notes_manager->get_voice_note_object(document.file_id), is_pinned);
        }
        break;
      default:
        break;
    }
    UNREACHABLE();
  }

  td_api::object_ptr<td_api::NotificationType> get_notification_type_object(DialogId dialog_id) const override {
    auto sender =
        G()->td().get_actor_unsafe()->messages_manager_->get_message_sender_object(sender_user_id_, sender_dialog_id_);
    return td_api::make_object<td_api::notificationTypeNewPushMessage>(
        message_id_.get(), std::move(sender), sender_name_, is_outgoing_,
        get_push_message_content_object(key_, arg_, photo_, document_));
  }

  StringBuilder &to_string_builder(StringBuilder &string_builder) const override {
    return string_builder << "NewPushMessageNotification[" << sender_user_id_ << "/" << sender_dialog_id_ << "/\""
                          << sender_name_ << "\", " << message_id_ << ", " << key_ << ", " << arg_ << ", " << photo_
                          << ", " << document_ << ']';
  }

  UserId sender_user_id_;
  DialogId sender_dialog_id_;
  MessageId message_id_;
  string sender_name_;
  string key_;
  string arg_;
  Photo photo_;
  Document document_;
  bool is_outgoing_;

 public:
  NotificationTypePushMessage(UserId sender_user_id, DialogId sender_dialog_id, string sender_name, bool is_outgoing,
                              MessageId message_id, string key, string arg, Photo photo, Document document)
      : sender_user_id_(sender_user_id)
      , sender_dialog_id_(sender_dialog_id)
      , message_id_(message_id)
      , sender_name_(std::move(sender_name))
      , key_(std::move(key))
      , arg_(std::move(arg))
      , photo_(std::move(photo))
      , document_(std::move(document))
      , is_outgoing_(is_outgoing) {
  }
};

unique_ptr<NotificationType> create_new_message_notification(MessageId message_id) {
  return make_unique<NotificationTypeMessage>(message_id);
}

unique_ptr<NotificationType> create_new_secret_chat_notification() {
  return make_unique<NotificationTypeSecretChat>();
}

unique_ptr<NotificationType> create_new_call_notification(CallId call_id) {
  return make_unique<NotificationTypeCall>(call_id);
}

unique_ptr<NotificationType> create_new_push_message_notification(UserId sender_user_id, DialogId sender_dialog_id,
                                                                  string sender_name, bool is_outgoing,
                                                                  MessageId message_id, string key, string arg,
                                                                  Photo photo, Document document) {
  return td::make_unique<NotificationTypePushMessage>(sender_user_id, sender_dialog_id, std::move(sender_name),
                                                      is_outgoing, message_id, std::move(key), std::move(arg),
                                                      std::move(photo), std::move(document));
}

}  // namespace td
