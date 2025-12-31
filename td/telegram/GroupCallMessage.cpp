//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/GroupCallMessage.h"

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/misc.h"
#include "td/telegram/StarManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/Version.h"

#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"
#include "td/utils/utf8.h"

namespace td {

static Result<MessageEntity> parse_message_entity(JsonValue &value) {
  if (value.type() != JsonValue::Type::Object) {
    return Status::Error("Expected object for MessageEntity");
  }

  auto &object = value.get_object();
  TRY_RESULT(type, object.get_required_string_field("_"));
  TRY_RESULT(min_layer, object.get_optional_int_field("_min_layer"));
  TRY_RESULT(offset, object.get_required_int_field("offset"));
  TRY_RESULT(length, object.get_required_int_field("length"));
  if (type == "messageEntityUnknown" || type == "messageEntityMention" || type == "messageEntityHashtag" ||
      type == "messageEntityCashtag" || type == "messageEntityPhone" || type == "messageEntityBotCommand" ||
      type == "messageEntityBankCard" || type == "messageEntityUrl" || type == "messageEntityEmail" ||
      type == "messageEntityMentionName" || min_layer > MTPROTO_LAYER) {
    return Status::Error("Skip");
  }
  if (type == "messageEntityPre") {
    TRY_RESULT(language, object.get_optional_string_field("language"));
    if (!clean_input_string(language)) {
      return Status::Error("Receive invalid UTF-8");
    }
    if (language.empty()) {
      return MessageEntity(MessageEntity::Type::Pre, offset, length);
    } else {
      return MessageEntity(MessageEntity::Type::PreCode, offset, length, std::move(language));
    }
  }
  if (type == "messageEntityTextUrl") {
    TRY_RESULT(url, object.get_required_string_field("url"));
    if (!clean_input_string(url)) {
      return Status::Error("Receive invalid UTF-8");
    }
    return MessageEntity(MessageEntity::Type::TextUrl, offset, length, std::move(url));
  }
  if (type == "messageEntityCustomEmoji") {
    TRY_RESULT(document_id, object.get_required_long_field("document_id"));
    return MessageEntity(MessageEntity::Type::CustomEmoji, offset, length, CustomEmojiId(document_id));
  }

  MessageEntity::Type entity_type = MessageEntity::Type::Size;
  if (type == "messageEntityBold") {
    entity_type = MessageEntity::Type::Bold;
  } else if (type == "messageEntityItalic") {
    entity_type = MessageEntity::Type::Italic;
  } else if (type == "messageEntityUnderline") {
    entity_type = MessageEntity::Type::Underline;
  } else if (type == "messageEntityStrike") {
    entity_type = MessageEntity::Type::Strikethrough;
  } else if (type == "messageEntityBlockquote") {
    entity_type = MessageEntity::Type::BlockQuote;
  } else if (type == "messageEntityCode") {
    entity_type = MessageEntity::Type::Code;
  } else if (type == "messageEntitySpoiler") {
    entity_type = MessageEntity::Type::Spoiler;
  } else {
    return Status::Error("Receive invalid message entity type");
  }
  return MessageEntity(entity_type, offset, length);
}

static Result<FormattedText> parse_text_with_entities(JsonObject &object) {
  TRY_RESULT(type, object.get_required_string_field("_"));
  if (type != "textWithEntities") {
    return Status::Error("Expected textWithEntities");
  }
  TRY_RESULT(min_layer, object.get_optional_int_field("_min_layer"));
  if (min_layer > MTPROTO_LAYER) {
    return Status::Error("Unsupported object");
  }
  TRY_RESULT(text, object.get_required_string_field("text"));
  if (!clean_input_string(text)) {
    return Status::Error("Receive invalid UTF-8");
  }
  if (static_cast<int64>(utf8_length(text)) > G()->get_option_integer("group_call_message_text_length_max")) {
    return Status::Error("Text is too long");
  }
  auto input_entities = object.extract_field("entities");
  vector<MessageEntity> entities;
  if (input_entities.type() == JsonValue::Type::Array) {
    for (auto &input_entity : input_entities.get_array()) {
      auto r_entity = parse_message_entity(input_entity);
      if (r_entity.is_error()) {
        if (r_entity.error().message() == "Skip") {
          continue;
        }
        return r_entity.move_as_error();
      }
      if (entities.size() > 1000u) {
        return Status::Error("Too many entities");
      }
      entities.push_back(r_entity.move_as_ok());
    }
  } else if (input_entities.type() != JsonValue::Type::Null) {
    return Status::Error("Invalid entities type");
  }
  return FormattedText{std::move(text), std::move(entities)};
}

static Result<FormattedText> parse_group_call_message(JsonObject &object) {
  TRY_RESULT(type, object.get_required_string_field("_"));
  if (type != "groupCallMessage") {
    return Status::Error("Expected groupCallMessage");
  }
  TRY_RESULT(min_layer, object.get_optional_int_field("_min_layer"));
  if (min_layer > MTPROTO_LAYER) {
    return Status::Error("Unsupported object");
  }
  auto message = object.extract_field("message");
  if (message.type() != JsonValue::Type::Object) {
    return Status::Error("Message expected to be an object");
  }
  return parse_text_with_entities(message.get_object());
}

GroupCallMessage::GroupCallMessage(Td *td, DialogId sender_dialog_id, string json_message) {
  LOG(INFO) << "Receive group call message from " << sender_dialog_id << ": " << json_message;
  auto r_value = json_decode(json_message);
  if (r_value.is_error()) {
    LOG(INFO) << "Failed to decode JSON object: " << r_value.error();
    return;
  }
  auto value = r_value.move_as_ok();
  if (value.type() != JsonValue::Type::Object) {
    return;
  }

  auto &object = value.get_object();
  auto r_random_id = object.get_required_long_field("random_id");
  if (!r_random_id.is_ok()) {
    LOG(INFO) << "Ignore message with invalid random identifier " << sender_dialog_id;
    return;
  }

  auto r_text = parse_group_call_message(object);
  if (r_text.is_error()) {
    LOG(INFO) << "Failed to parse group call message object: " << r_text.error();
    return;
  }
  auto text = r_text.move_as_ok();
  auto status = fix_formatted_text(text.text, text.entities, false, false, true, true, false);
  if (status.is_error()) {
    LOG(INFO) << "Ignore invalid formatted text: " << status;
    return;
  }
  if (sender_dialog_id.get_type() != DialogType::User ||
      !td->user_manager_->is_user_premium(sender_dialog_id.get_user_id())) {
    remove_premium_custom_emoji_entities(td, text.entities, true);
  }

  random_id_ = r_random_id.ok();
  date_ = G()->unix_time();
  sender_dialog_id_ = sender_dialog_id;
  text_ = std::move(text);
  paid_message_star_count_ = 0;
}

GroupCallMessage::GroupCallMessage(Td *td, telegram_api::object_ptr<telegram_api::groupCallMessage> &&message)
    : server_id_(message->id_)
    , date_(max(1000000000, message->date_))
    , sender_dialog_id_(message->from_id_)
    , text_(get_formatted_text(td->user_manager_.get(), std::move(message->message_), true, false, "GroupCallMessage"))
    , paid_message_star_count_(StarManager::get_star_count(message->paid_message_stars_))
    , from_admin_(message->from_admin_) {
  if (server_id_ <= 0) {
    LOG(ERROR) << "Receive group call message " << server_id_;
    sender_dialog_id_ = {};
  }
}

GroupCallMessage::GroupCallMessage(DialogId sender_dialog_id, FormattedText text, int64 paid_message_star_count,
                                   bool from_admin)
    : date_(G()->unix_time())
    , sender_dialog_id_(sender_dialog_id)
    , text_(std::move(text))
    , paid_message_star_count_(paid_message_star_count)
    , from_admin_(from_admin)
    , is_local_(true) {
}

string GroupCallMessage::encode_to_json() const {
  return json_encode<string>(json_object([message = &text_](auto &o) {
    o("_", "groupCallMessage");
    o("random_id", to_string(Random::secure_int64()));
    o("message", json_object([message](auto &o) {
        o("_", "textWithEntities");
        o("text", message->text);
        o("entities", json_array(message->entities, [](auto &entity) {
            return json_object([&entity](auto &o) {
              switch (entity.type) {
                case MessageEntity::Type::Mention:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::Hashtag:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::Cashtag:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::BotCommand:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::PhoneNumber:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::BankCardNumber:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::Url:
                  o("_", "messageEntityUrl");
                  break;
                case MessageEntity::Type::EmailAddress:
                  o("_", "messageEntityEmail");
                  break;
                case MessageEntity::Type::Bold:
                  o("_", "messageEntityBold");
                  break;
                case MessageEntity::Type::Italic:
                  o("_", "messageEntityItalic");
                  break;
                case MessageEntity::Type::Underline:
                  o("_", "messageEntityUnderline");
                  break;
                case MessageEntity::Type::Strikethrough:
                  o("_", "messageEntityStrike");
                  break;
                case MessageEntity::Type::BlockQuote:
                  o("_", "messageEntityBlockquote");
                  break;
                case MessageEntity::Type::Code:
                  o("_", "messageEntityCode");
                  break;
                case MessageEntity::Type::Pre:
                  o("_", "messageEntityPre");
                  o("language", string());
                  break;
                case MessageEntity::Type::PreCode:
                  o("_", "messageEntityPre");
                  o("language", entity.argument);
                  break;
                case MessageEntity::Type::TextUrl:
                  o("_", "messageEntityTextUrl");
                  o("url", entity.argument);
                  break;
                case MessageEntity::Type::MentionName:
                  o("_", "messageEntityMentionName");
                  o("user_id", 0);
                  break;
                case MessageEntity::Type::MediaTimestamp:
                  o("_", "messageEntityUnknown");
                  break;
                case MessageEntity::Type::Spoiler:
                  o("_", "messageEntitySpoiler");
                  break;
                case MessageEntity::Type::CustomEmoji:
                  o("_", "messageEntityCustomEmoji");
                  o("document_id", to_string(entity.custom_emoji_id.get()));
                  break;
                case MessageEntity::Type::ExpandableBlockQuote:
                  o("_", "messageEntityBlockquote");
                  break;
                default:
                  UNREACHABLE();
              }
              o("offset", entity.offset);
              o("length", entity.length);
            });
          }));
      }));
  }));
}

td_api::object_ptr<td_api::groupCallMessage> GroupCallMessage::get_group_call_message_object(
    Td *td, int32 message_id, bool can_be_deleted) const {
  return td_api::make_object<td_api::groupCallMessage>(
      message_id, get_message_sender_object(td, sender_dialog_id_, "get_group_call_message_object"), date_,
      get_formatted_text_object(td->user_manager_.get(), text_, true, -1), paid_message_star_count_, from_admin_,
      can_be_deleted);
}

StringBuilder &operator<<(StringBuilder &string_builder, const GroupCallMessage &group_call_message) {
  return string_builder << "GroupCallMessage[" << group_call_message.server_id_ << '/' << group_call_message.random_id_
                        << " by " << group_call_message.sender_dialog_id_ << ": " << group_call_message.text_ << ']';
}

}  // namespace td
