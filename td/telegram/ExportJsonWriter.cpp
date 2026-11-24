//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportJsonWriter.h"

#include "td/telegram/ChatManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/base64.h"
#include "td/utils/filesystem.h"
#include "td/utils/JsonBuilder.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StackAllocator.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/utf8.h"

#include <utility>

namespace td {

// the nesting level of the items of the "messages" array of the root object
static constexpr int32 MESSAGE_JSON_OFFSET = 2;

static const char FILE_NOT_INCLUDED[] = "(File not included. Change data exporting settings to download.)";

static const char FILE_UNAVAILABLE[] = "(File unavailable, please try again later)";

static const char FILE_TOO_BIG[] = "(File exceeds maximum size. Change data exporting settings to download.)";

static Slice get_file_path(const ExportFile &file) {
  switch (file.skip_reason) {
    case ExportFile::SkipReason::None:
      return file.relative_path;
    case ExportFile::SkipReason::Unavailable:
      return Slice(FILE_UNAVAILABLE);
    case ExportFile::SkipReason::FileType:
      return Slice(FILE_NOT_INCLUDED);
    case ExportFile::SkipReason::FileSize:
      return Slice(FILE_TOO_BIG);
    default:
      UNREACHABLE();
      return Slice();
  }
}

static Slice get_dialog_type_string(ExportDialogInfo::Type type) {
  using Type = ExportDialogInfo::Type;
  switch (type) {
    case Type::Unknown:
      return Slice();
    case Type::Self:
      return Slice("saved_messages");
    case Type::Replies:
      return Slice("replies");
    case Type::VerifyCodes:
      return Slice("verification_codes");
    case Type::Personal:
      return Slice("personal_chat");
    case Type::Bot:
      return Slice("bot_chat");
    case Type::PrivateGroup:
      return Slice("private_group");
    case Type::PrivateSupergroup:
      return Slice("private_supergroup");
    case Type::PublicSupergroup:
      return Slice("public_supergroup");
    case Type::PrivateChannel:
      return Slice("private_channel");
    case Type::PublicChannel:
      return Slice("public_channel");
    default:
      UNREACHABLE();
      return Slice();
  }
}

// the bare identifier with the peer type in the bits 48-55
static int64 get_peer_id_value(DialogId dialog_id) {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return dialog_id.get_user_id().get();
    case DialogType::Chat:
      return dialog_id.get_chat_id().get() | (static_cast<int64>(1) << 48);
    case DialogType::Channel:
      return dialog_id.get_channel_id().get() | (static_cast<int64>(2) << 48);
    default:
      return 0;
  }
}

static string get_peer_id_string(DialogId dialog_id) {
  switch (dialog_id.get_type()) {
    case DialogType::Chat:
      return PSTRING() << "chat" << dialog_id.get_chat_id().get();
    case DialogType::Channel:
      return PSTRING() << "channel" << dialog_id.get_channel_id().get();
    case DialogType::User:
      return PSTRING() << "user" << dialog_id.get_user_id().get();
    default:
      return "user0";
  }
}

static Slice get_call_discard_reason_string(const td_api::CallDiscardReason *reason) {
  if (reason == nullptr) {
    return Slice();
  }
  switch (reason->get_id()) {
    case td_api::callDiscardReasonEmpty::ID:
      return Slice();
    case td_api::callDiscardReasonMissed::ID:
      return Slice("missed");
    case td_api::callDiscardReasonDeclined::ID:
      return Slice("busy");
    case td_api::callDiscardReasonDisconnected::ID:
      return Slice("disconnect");
    case td_api::callDiscardReasonHungUp::ID:
      return Slice("hangup");
    case td_api::callDiscardReasonUpgradeToGroupCall::ID:
      return Slice("migrate_conference_all");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static Slice get_passport_element_type_string(const td_api::PassportElementType *type) {
  if (type == nullptr) {
    return Slice();
  }
  switch (type->get_id()) {
    case td_api::passportElementTypePersonalDetails::ID:
      return Slice("personal_details");
    case td_api::passportElementTypePassport::ID:
      return Slice("passport");
    case td_api::passportElementTypeDriverLicense::ID:
      return Slice("driver_license");
    case td_api::passportElementTypeIdentityCard::ID:
      return Slice("identity_card");
    case td_api::passportElementTypeInternalPassport::ID:
      return Slice("internal_passport");
    case td_api::passportElementTypeAddress::ID:
      return Slice("address_information");
    case td_api::passportElementTypeUtilityBill::ID:
      return Slice("utility_bill");
    case td_api::passportElementTypeBankStatement::ID:
      return Slice("bank_statement");
    case td_api::passportElementTypeRentalAgreement::ID:
      return Slice("rental_agreement");
    case td_api::passportElementTypePassportRegistration::ID:
      return Slice("passport_registration");
    case td_api::passportElementTypeTemporaryRegistration::ID:
      return Slice("temporary_registration");
    case td_api::passportElementTypePhoneNumber::ID:
      return Slice("phone_number");
    case td_api::passportElementTypeEmailAddress::ID:
      return Slice("email");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static Slice get_text_entity_type_string(const td_api::TextEntityType *type) {
  if (type == nullptr) {
    return Slice("plain");
  }
  switch (type->get_id()) {
    case td_api::textEntityTypeMention::ID:
      return Slice("mention");
    case td_api::textEntityTypeHashtag::ID:
      return Slice("hashtag");
    case td_api::textEntityTypeCashtag::ID:
      return Slice("cashtag");
    case td_api::textEntityTypeBotCommand::ID:
      return Slice("bot_command");
    case td_api::textEntityTypeUrl::ID:
      return Slice("link");
    case td_api::textEntityTypeEmailAddress::ID:
      return Slice("email");
    case td_api::textEntityTypePhoneNumber::ID:
      return Slice("phone");
    case td_api::textEntityTypeBankCardNumber::ID:
      return Slice("bank_card");
    case td_api::textEntityTypeTonAddress::ID:
      return Slice("ton_address");
    case td_api::textEntityTypeBold::ID:
      return Slice("bold");
    case td_api::textEntityTypeItalic::ID:
      return Slice("italic");
    case td_api::textEntityTypeUnderline::ID:
      return Slice("underline");
    case td_api::textEntityTypeStrikethrough::ID:
      return Slice("strikethrough");
    case td_api::textEntityTypeSpoiler::ID:
      return Slice("spoiler");
    case td_api::textEntityTypeCode::ID:
      return Slice("code");
    case td_api::textEntityTypePre::ID:
    case td_api::textEntityTypePreCode::ID:
      return Slice("pre");
    case td_api::textEntityTypeBlockQuote::ID:
    case td_api::textEntityTypeExpandableBlockQuote::ID:
      return Slice("blockquote");
    case td_api::textEntityTypeTextUrl::ID:
      return Slice("text_link");
    case td_api::textEntityTypeMentionName::ID:
      return Slice("mention_name");
    case td_api::textEntityTypeCustomEmoji::ID:
      return Slice("custom_emoji");
    case td_api::textEntityTypeMediaTimestamp::ID:
    case td_api::textEntityTypeDateTime::ID:
      return Slice("unknown");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static void store_string(JsonObjectScope &object, Slice key, Slice value) {
  if (!value.empty()) {
    object(key, JsonRawString(value));
  }
}

static void store_price(JsonObjectScope &object, int64 whole, int64 nano, bool is_ton) {
  object("price_amount_whole", JsonRawString(to_string(whole)));
  object("price_amount_nano", JsonRawString(to_string(nano)));
  object("price_currency", is_ton ? "TON" : "Stars");
}

static void store_suggested_post_price(JsonObjectScope &object, const td_api::SuggestedPostPrice *price) {
  if (price == nullptr) {
    return store_price(object, 0, 0, false);
  }
  switch (price->get_id()) {
    case td_api::suggestedPostPriceStar::ID:
      return store_price(object, static_cast<const td_api::suggestedPostPriceStar *>(price)->star_count_, 0, false);
    case td_api::suggestedPostPriceGram::ID: {
      auto cent_count = static_cast<const td_api::suggestedPostPriceGram *>(price)->gram_cent_count_;
      return store_price(object, cent_count / 100, cent_count % 100 * (EXPORT_NANOS_IN_GRAM / 100), true);
    }
    default:
      UNREACHABLE();
  }
}

// the value is indented as a field of the message at the given nesting level
template <class T>
static string get_embedded_json(const T &value, int32 message_offset) {
  auto buf = StackAllocator::alloc(1 << 12);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), message_offset + 1);
  jb.enter_value() << value;
  return jb.string_builder().as_cslice().str();
}

class ExportJsonName final : public Jsonable {
 public:
  explicit ExportJsonName(string name) : name_(std::move(name)) {
  }

  void store(JsonValueScope *scope) const {
    if (name_.empty()) {
      *scope << JsonNull();
    } else {
      *scope << JsonRawString(name_);
    }
  }

 private:
  string name_;
};

class ExportJsonUnsigned final : public Jsonable {
 public:
  explicit ExportJsonUnsigned(int64 value) : value_(static_cast<uint64>(value)) {
  }

  void store(JsonValueScope *scope) const {
    *scope << JsonRaw(to_string(value_));
  }

 private:
  uint64 value_;
};

class ExportJsonTextPart final : public Jsonable {
 public:
  ExportJsonTextPart(const ExportTextPart &part, const ExportCustomEmojiFiles *custom_emoji_files)
      : part_(part), custom_emoji_files_(custom_emoji_files) {
  }

  void store(JsonValueScope *scope) const {
    auto object = scope->enter_object();
    object("type", JsonRawString(get_text_entity_type_string(part_.type)));
    object("text", JsonRawString(part_.text));
    if (part_.type == nullptr) {
      return;
    }
    switch (part_.type->get_id()) {
      case td_api::textEntityTypeMentionName::ID:
        object("user_id", JsonLong(static_cast<const td_api::textEntityTypeMentionName *>(part_.type)->user_id_));
        break;
      case td_api::textEntityTypeCustomEmoji::ID:
        object("document_id",
               JsonRawString(get_export_custom_emoji_path(
                   custom_emoji_files_,
                   static_cast<const td_api::textEntityTypeCustomEmoji *>(part_.type)->custom_emoji_id_)));
        break;
      case td_api::textEntityTypePre::ID:
        object("language", JsonRawString(Slice()));
        break;
      case td_api::textEntityTypePreCode::ID:
        object("language", JsonRawString(static_cast<const td_api::textEntityTypePreCode *>(part_.type)->language_));
        break;
      case td_api::textEntityTypeTextUrl::ID:
        object("href", JsonRawString(static_cast<const td_api::textEntityTypeTextUrl *>(part_.type)->url_));
        break;
      case td_api::textEntityTypeBlockQuote::ID:
        object("collapsed", JsonFalse());
        break;
      case td_api::textEntityTypeExpandableBlockQuote::ID:
        object("collapsed", JsonTrue());
        break;
      default:
        break;
    }
  }

 private:
  const ExportTextPart &part_;
  const ExportCustomEmojiFiles *custom_emoji_files_;
};

class ExportJsonText final : public Jsonable {
 public:
  ExportJsonText(Slice text, const vector<td_api::object_ptr<td_api::textEntity>> *entities, bool as_objects,
                 const ExportCustomEmojiFiles *custom_emoji_files = nullptr)
      : text_(text), entities_(entities), as_objects_(as_objects), custom_emoji_files_(custom_emoji_files) {
  }

  ExportJsonText(const td_api::formattedText *text, bool as_objects)
      : ExportJsonText(text == nullptr ? Slice() : Slice(text->text_), text == nullptr ? nullptr : &text->entities_,
                       as_objects) {
  }

  void store(JsonValueScope *scope) const {
    auto parts = get_export_text_parts(text_, entities_);
    if (!as_objects_) {
      if (parts.empty()) {
        *scope << JsonRawString(Slice());
        return;
      }
      if (parts.size() == 1 && parts[0].type == nullptr) {
        *scope << JsonRawString(parts[0].text);
        return;
      }
    }
    auto array = scope->enter_array();
    for (const auto &part : parts) {
      if (part.type == nullptr && !as_objects_) {
        array(JsonRawString(part.text));
      } else {
        array(ExportJsonTextPart(part, custom_emoji_files_));
      }
    }
  }

 private:
  Slice text_;
  const vector<td_api::object_ptr<td_api::textEntity>> *entities_;
  bool as_objects_;
  const ExportCustomEmojiFiles *custom_emoji_files_;
};

static auto get_checklist_tasks_json(const vector<td_api::object_ptr<td_api::checklistTask>> &tasks) {
  return json_array([&tasks](auto &array) {
    for (const auto &task : tasks) {
      array(json_object([&task](auto &object) {
        object("text", ExportJsonText(task->text_.get(), false));
        object("id", JsonInt(task->id_));
      }));
    }
  });
}

class ExportJsonInlineButton final : public Jsonable {
 public:
  explicit ExportJsonInlineButton(const td_api::inlineKeyboardButton *button) : button_(button) {
  }

  void store(JsonValueScope *scope) const {
    Slice type;
    string data;
    Slice forward_text;
    int64 button_id = 0;
    bool is_callback = false;
    const auto *button_type = button_->type_.get();
    switch (button_type->get_id()) {
      case td_api::inlineKeyboardButtonTypeUrl::ID:
        type = Slice("url");
        data = static_cast<const td_api::inlineKeyboardButtonTypeUrl *>(button_type)->url_;
        break;
      case td_api::inlineKeyboardButtonTypeLoginUrl::ID: {
        const auto *login_url = static_cast<const td_api::inlineKeyboardButtonTypeLoginUrl *>(button_type);
        type = Slice("auth");
        data = login_url->url_;
        forward_text = login_url->forward_text_;
        button_id = login_url->id_;
        break;
      }
      case td_api::inlineKeyboardButtonTypeWebApp::ID:
        type = Slice("web_view");
        data = static_cast<const td_api::inlineKeyboardButtonTypeWebApp *>(button_type)->url_;
        break;
      case td_api::inlineKeyboardButtonTypeCallback::ID:
        type = Slice("callback");
        data = static_cast<const td_api::inlineKeyboardButtonTypeCallback *>(button_type)->data_;
        is_callback = true;
        break;
      case td_api::inlineKeyboardButtonTypeCallbackWithPassword::ID:
        type = Slice("callback_with_password");
        data = static_cast<const td_api::inlineKeyboardButtonTypeCallbackWithPassword *>(button_type)->data_;
        is_callback = true;
        break;
      case td_api::inlineKeyboardButtonTypeCallbackGame::ID:
        type = Slice("game");
        break;
      case td_api::inlineKeyboardButtonTypeSwitchInline::ID: {
        const auto *switch_inline = static_cast<const td_api::inlineKeyboardButtonTypeSwitchInline *>(button_type);
        auto is_same_chat = switch_inline->target_chat_ != nullptr &&
                            switch_inline->target_chat_->get_id() == td_api::targetChatCurrent::ID;
        type = is_same_chat ? Slice("switch_inline_same") : Slice("switch_inline");
        data = switch_inline->query_;
        break;
      }
      case td_api::inlineKeyboardButtonTypeBuy::ID:
        type = Slice("buy");
        break;
      case td_api::inlineKeyboardButtonTypeUser::ID:
        type = Slice("user_profile");
        data = to_string(static_cast<const td_api::inlineKeyboardButtonTypeUser *>(button_type)->user_id_);
        break;
      case td_api::inlineKeyboardButtonTypeCopyText::ID:
        type = Slice("copy_text");
        data = static_cast<const td_api::inlineKeyboardButtonTypeCopyText *>(button_type)->text_;
        break;
      case td_api::inlineKeyboardButtonTypeDisabled::ID:
        type = Slice("disabled");
        break;
      default:
        UNREACHABLE();
    }

    auto object = scope->enter_object();
    object("type", JsonRawString(type));
    store_string(object, "text", button_->text_);
    if (!data.empty()) {
      if (is_callback) {
        object("dataBase64", JsonRawString(base64url_encode(data)));
        object("data", JsonRawString(Slice()));
      } else {
        object("data", JsonRawString(data));
      }
    }
    store_string(object, "forward_text", forward_text);
    if (button_id != 0) {
      object("button_id", JsonLong(button_id));
    }
  }

 private:
  const td_api::inlineKeyboardButton *button_;
};

class ExportJsonMessage final : public Jsonable {
 public:
  ExportJsonMessage(Td *td, const ExportMessage &export_message, bool is_migrated,
                    const ExportCustomEmojiFiles &custom_emoji_files, int32 utc_time_offset, int32 json_offset)
      : td_(td)
      , message_(*export_message.message)
      , file_(export_message.file)
      , thumbnail_(export_message.thumbnail)
      , custom_emoji_files_(custom_emoji_files)
      , is_history_cleared_(export_message.is_history_cleared)
      , is_migrated_(is_migrated)
      , utc_time_offset_(utc_time_offset)
      , json_offset_(json_offset) {
  }

  void store(JsonValueScope *scope) const;

 private:
  Td *td_;
  const td_api::message &message_;
  const ExportFile &file_;
  const ExportFile &thumbnail_;
  const ExportCustomEmojiFiles &custom_emoji_files_;
  bool is_history_cleared_;
  bool is_migrated_;
  int32 utc_time_offset_;
  // the nesting level of the message
  int32 json_offset_;

  string format_date(int32 date) const {
    return get_export_date_time_string(get_export_local_time(date, utc_time_offset_));
  }

  string get_dialog_name(DialogId dialog_id) const {
    if (!dialog_id.is_valid()) {
      return string();
    }
    return td_->dialog_manager_->get_dialog_title(dialog_id);
  }

  string get_user_name(int64 user_id) const {
    return td_->user_manager_->get_user_title(UserId(user_id));
  }

  int32 get_same_chat_message_id(int32 server_message_id) const {
    return is_migrated_ ? server_message_id + EXPORT_MIGRATED_MESSAGE_ID_SHIFT : server_message_id;
  }

  bool is_media_available() const {
    return message_.self_destruct_type_ == nullptr;
  }

  UserId get_game_bot_user_id() const;

  void store_peer(JsonObjectScope &object, Slice name_key, Slice id_key, DialogId dialog_id) const;

  void store_actor(JsonObjectScope &object) const;

  void store_user_names(JsonObjectScope &object, Slice key, const vector<int64> &user_ids) const;

  void store_same_chat_message_id(JsonObjectScope &object, Slice key, int64 message_id) const;

  void store_message_reference(JsonObjectScope &object, Slice key, int64 chat_id, int64 message_id) const;

  void store_message_header(JsonObjectScope &object) const;

  void store_self_destruct_period(JsonObjectScope &object) const;

  void store_photo(JsonObjectScope &object, const td_api::photoSize *size, bool is_available) const;

  void store_document_file(JsonObjectScope &object, const td_api::file *file, Slice file_name,
                           const td_api::thumbnail *thumbnail) const;

  void store_document_details(JsonObjectScope &object, Slice mime_type, int32 duration, int32 width, int32 height,
                              bool has_spoiler) const;

  void store_star_gift(JsonObjectScope &object, int64 gift_id, int64 star_count, bool is_limited, bool is_anonymous,
                       const td_api::formattedText *text) const;

  void store_media_content(JsonObjectScope &object, const td_api::MessageContent *content) const;

  void store_service_content(JsonObjectScope &object, const td_api::MessageContent *content) const;

  void store_text(JsonObjectScope &object, const td_api::MessageContent *content) const;

  void store_inline_buttons(JsonObjectScope &object) const;

  void store_reactions(JsonObjectScope &object) const;
};

UserId ExportJsonMessage::get_game_bot_user_id() const {
  if (message_.via_bot_user_id_ != 0) {
    return UserId(message_.via_bot_user_id_);
  }
  if (message_.forward_info_ != nullptr && message_.forward_info_->origin_ != nullptr &&
      message_.forward_info_->origin_->get_id() == td_api::messageOriginUser::ID) {
    return UserId(
        static_cast<const td_api::messageOriginUser *>(message_.forward_info_->origin_.get())->sender_user_id_);
  }
  auto sender_dialog_id = get_export_message_sender_dialog_id(message_.sender_id_.get());
  return sender_dialog_id.get_type() == DialogType::User ? sender_dialog_id.get_user_id() : UserId();
}

void ExportJsonMessage::store_peer(JsonObjectScope &object, Slice name_key, Slice id_key, DialogId dialog_id) const {
  if (dialog_id == DialogId()) {
    return;
  }
  object(name_key, ExportJsonName(get_dialog_name(dialog_id)));
  object(id_key, JsonRawString(get_peer_id_string(dialog_id)));
}

void ExportJsonMessage::store_actor(JsonObjectScope &object) const {
  store_peer(object, "actor", "actor_id", get_export_message_sender_dialog_id(message_.sender_id_.get()));
}

void ExportJsonMessage::store_user_names(JsonObjectScope &object, Slice key, const vector<int64> &user_ids) const {
  object(key, json_array([&](auto &array) {
           for (auto user_id : user_ids) {
             array(ExportJsonName(get_user_name(user_id)));
           }
         }));
}

void ExportJsonMessage::store_same_chat_message_id(JsonObjectScope &object, Slice key, int64 message_id) const {
  auto server_message_id = get_export_server_message_id(message_id);
  if (server_message_id != 0) {
    object(key, JsonInt(get_same_chat_message_id(server_message_id)));
  }
}

void ExportJsonMessage::store_message_reference(JsonObjectScope &object, Slice key, int64 chat_id,
                                                int64 message_id) const {
  if (chat_id == 0 || chat_id == message_.chat_id_) {
    return store_same_chat_message_id(object, key, message_id);
  }
  auto server_message_id = get_export_server_message_id(message_id);
  if (server_message_id != 0) {
    object(key, JsonInt(server_message_id));
    object("reply_to_peer_id", JsonRawString(get_peer_id_string(DialogId(chat_id))));
  }
}

void ExportJsonMessage::store_message_header(JsonObjectScope &object) const {
  store_peer(object, "from", "from_id", get_export_message_sender_dialog_id(message_.sender_id_.get()));
  store_string(object, "author", message_.author_signature_);

  const auto *forward_info = message_.forward_info_.get();
  if (forward_info != nullptr && forward_info->origin_ != nullptr) {
    const auto *origin = forward_info->origin_.get();
    switch (origin->get_id()) {
      case td_api::messageOriginUser::ID:
        store_peer(object, "forwarded_from", "forwarded_from_id",
                   DialogId(UserId(static_cast<const td_api::messageOriginUser *>(origin)->sender_user_id_)));
        break;
      case td_api::messageOriginHiddenUser::ID:
        object("forwarded_from",
               ExportJsonName(static_cast<const td_api::messageOriginHiddenUser *>(origin)->sender_name_));
        break;
      case td_api::messageOriginChat::ID:
        store_peer(object, "forwarded_from", "forwarded_from_id",
                   DialogId(static_cast<const td_api::messageOriginChat *>(origin)->sender_chat_id_));
        break;
      case td_api::messageOriginChannel::ID:
        store_peer(object, "forwarded_from", "forwarded_from_id",
                   DialogId(static_cast<const td_api::messageOriginChannel *>(origin)->chat_id_));
        break;
      default:
        UNREACHABLE();
    }
  } else if (message_.import_info_ != nullptr) {
    object("forwarded_from", ExportJsonName(message_.import_info_->sender_name_));
  }
  if (forward_info != nullptr && forward_info->source_ != nullptr && forward_info->source_->chat_id_ != 0) {
    object("saved_from", ExportJsonName(get_dialog_name(DialogId(forward_info->source_->chat_id_))));
  }

  if (message_.reply_to_ != nullptr && message_.reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
    const auto *reply_to = static_cast<const td_api::messageReplyToMessage *>(message_.reply_to_.get());
    store_message_reference(object, "reply_to_message_id", reply_to->chat_id_, reply_to->message_id_);
  }

  if (message_.via_bot_user_id_ != 0) {
    auto username = td_->user_manager_->get_user_first_username(UserId(message_.via_bot_user_id_));
    if (!username.empty()) {
      object("via_bot", JsonRawString(PSTRING() << '@' << username));
    }
  }
}

void ExportJsonMessage::store_self_destruct_period(JsonObjectScope &object) const {
  const auto *self_destruct_type = message_.self_destruct_type_.get();
  if (self_destruct_type == nullptr) {
    return;
  }
  switch (self_destruct_type->get_id()) {
    case td_api::messageSelfDestructTypeTimer::ID:
      object(
          "self_destruct_period_seconds",
          JsonInt(static_cast<const td_api::messageSelfDestructTypeTimer *>(self_destruct_type)->self_destruct_time_));
      break;
    case td_api::messageSelfDestructTypeImmediately::ID:
      // the period of the media, which is deleted right after it is opened
      object("self_destruct_period_seconds", JsonInt(0x7FFFFFFF));
      break;
    default:
      UNREACHABLE();
  }
}

void ExportJsonMessage::store_photo(JsonObjectScope &object, const td_api::photoSize *size, bool is_available) const {
  is_available &= size != nullptr;
  object("photo", JsonRawString(is_available ? get_file_path(file_) : Slice(FILE_UNAVAILABLE)));
  object("photo_file_size", JsonLong(is_available ? get_export_file_size(size->photo_.get()) : 0));
  if (size != nullptr && size->width_ != 0 && size->height_ != 0) {
    object("width", JsonInt(size->width_));
    object("height", JsonInt(size->height_));
  }
}

void ExportJsonMessage::store_document_file(JsonObjectScope &object, const td_api::file *file, Slice file_name,
                                            const td_api::thumbnail *thumbnail) const {
  auto is_available = is_media_available();
  object("file", JsonRawString(is_available ? get_file_path(file_) : Slice(FILE_UNAVAILABLE)));
  store_string(object, "file_name", file_name);
  object("file_size", JsonLong(is_available ? get_export_file_size(file) : 0));
  if (thumbnail != nullptr && thumbnail->width_ > 0) {
    // the thumbnail of a self-destructing media is kept for the sender
    auto is_thumbnail_available = is_available || message_.is_outgoing_;
    object("thumbnail", JsonRawString(is_thumbnail_available ? get_file_path(thumbnail_) : Slice(FILE_UNAVAILABLE)));
    object("thumbnail_file_size", JsonLong(is_thumbnail_available ? get_export_file_size(thumbnail->file_.get()) : 0));
  }
}

void ExportJsonMessage::store_document_details(JsonObjectScope &object, Slice mime_type, int32 duration, int32 width,
                                               int32 height, bool has_spoiler) const {
  store_string(object, "mime_type", mime_type);
  if (duration != 0) {
    object("duration_seconds", JsonInt(duration));
  }
  if (width != 0 && height != 0) {
    object("width", JsonInt(width));
    object("height", JsonInt(height));
  }
  if (has_spoiler) {
    object("media_spoiler", JsonTrue());
  }
  store_self_destruct_period(object);
}

void ExportJsonMessage::store_star_gift(JsonObjectScope &object, int64 gift_id, int64 star_count, bool is_limited,
                                        bool is_anonymous, const td_api::formattedText *text) const {
  store_actor(object);
  object("action", "send_star_gift");
  object("gift_id", ExportJsonUnsigned(gift_id));
  object("stars", JsonLong(star_count));
  object("is_limited", JsonBool(is_limited));
  object("is_anonymous", JsonBool(is_anonymous));
  object("gift_text", ExportJsonText(text, false));
}

void ExportJsonMessage::store_media_content(JsonObjectScope &object, const td_api::MessageContent *content) const {
  switch (content->get_id()) {
    case td_api::messageText::ID:
    case td_api::messageAnimatedEmoji::ID:
    case td_api::messageDice::ID:
    case td_api::messageStakeDice::ID:
    case td_api::messageStory::ID:
    case td_api::messageWidget::ID:
      break;
    case td_api::messageAnimation::ID: {
      const auto *m = static_cast<const td_api::messageAnimation *>(content);
      const auto *animation = m->animation_.get();
      store_document_file(object, animation->animation_.get(), animation->file_name_, animation->thumbnail_.get());
      object("media_type", "animation");
      store_document_details(object, animation->mime_type_, animation->duration_, animation->width_, animation->height_,
                             m->has_spoiler_);
      break;
    }
    case td_api::messageAudio::ID: {
      const auto *audio = static_cast<const td_api::messageAudio *>(content)->audio_.get();
      store_document_file(object, audio->audio_.get(), audio->file_name_, audio->album_cover_thumbnail_.get());
      object("media_type", "audio_file");
      store_string(object, "performer", audio->performer_);
      store_string(object, "title", audio->title_);
      store_document_details(object, audio->mime_type_, audio->duration_, 0, 0, false);
      break;
    }
    case td_api::messageDocument::ID: {
      const auto *document = static_cast<const td_api::messageDocument *>(content)->document_.get();
      store_document_file(object, document->document_.get(), document->file_name_, document->thumbnail_.get());
      store_document_details(object, document->mime_type_, 0, 0, 0, false);
      break;
    }
    case td_api::messagePaidMedia::ID:
      object("paid_stars_amount", JsonLong(static_cast<const td_api::messagePaidMedia *>(content)->star_count_));
      break;
    case td_api::messagePhoto::ID: {
      const auto *m = static_cast<const td_api::messagePhoto *>(content);
      store_photo(object, get_export_photo_size(m->photo_->sizes_), is_media_available());
      if (m->has_spoiler_) {
        object("media_spoiler", JsonTrue());
      }
      store_self_destruct_period(object);
      break;
    }
    case td_api::messageSticker::ID: {
      const auto *sticker = static_cast<const td_api::messageSticker *>(content)->sticker_.get();
      store_document_file(object, sticker->sticker_.get(), Slice(), sticker->thumbnail_.get());
      object("media_type", "sticker");
      store_string(object, "sticker_emoji", sticker->emoji_);
      store_document_details(object, get_sticker_format_mime_type(get_sticker_format(sticker->format_)), 0,
                             sticker->width_, sticker->height_, false);
      break;
    }
    case td_api::messageVideo::ID: {
      const auto *m = static_cast<const td_api::messageVideo *>(content);
      const auto *video = m->video_.get();
      store_document_file(object, video->video_.get(), video->file_name_, video->thumbnail_.get());
      object("media_type", "video_file");
      store_document_details(object, video->mime_type_, video->duration_, video->width_, video->height_,
                             m->has_spoiler_);
      break;
    }
    case td_api::messageVideoNote::ID: {
      const auto *video_note = static_cast<const td_api::messageVideoNote *>(content)->video_note_.get();
      store_document_file(object, video_note->video_.get(), Slice(), video_note->thumbnail_.get());
      object("media_type", "video_message");
      store_document_details(object, Slice(), video_note->duration_, video_note->length_, video_note->length_, false);
      break;
    }
    case td_api::messageVoiceNote::ID: {
      const auto *voice_note = static_cast<const td_api::messageVoiceNote *>(content)->voice_note_.get();
      store_document_file(object, voice_note->voice_.get(), Slice(), nullptr);
      object("media_type", "voice_message");
      store_document_details(object, voice_note->mime_type_, voice_note->duration_, 0, 0, false);
      break;
    }
    case td_api::messageExpiredPhoto::ID:
      store_photo(object, nullptr, false);
      break;
    case td_api::messageExpiredVideo::ID:
    case td_api::messageExpiredVideoNote::ID:
    case td_api::messageExpiredVoiceNote::ID:
      object("file", JsonRawString(Slice(FILE_UNAVAILABLE)));
      object("file_size", JsonLong(0));
      break;
    case td_api::messageLiveLocation::ID: {
      const auto *live_location = static_cast<const td_api::messageLiveLocation *>(content)->location_.get();
      const auto *location = live_location->location_.get();
      if (location == nullptr) {
        object("location_information", JsonNull());
      } else {
        object("location_information", json_object([location](auto &o) {
                 o("latitude", JsonFloat(location->latitude_));
                 o("longitude", JsonFloat(location->longitude_));
               }));
      }
      if (live_location->live_period_ != 0) {
        object("live_location_period_seconds", JsonInt(live_location->live_period_));
      }
      break;
    }
    case td_api::messageLocation::ID: {
      const auto *location = static_cast<const td_api::messageLocation *>(content)->location_.get();
      object("location_information", json_object([location](auto &o) {
               o("latitude", JsonFloat(location->latitude_));
               o("longitude", JsonFloat(location->longitude_));
             }));
      break;
    }
    case td_api::messageVenue::ID: {
      const auto *venue = static_cast<const td_api::messageVenue *>(content)->venue_.get();
      store_string(object, "place_name", venue->title_);
      store_string(object, "address", venue->address_);
      const auto *location = venue->location_.get();
      if (location != nullptr) {
        object("location_information", json_object([location](auto &o) {
                 o("latitude", JsonFloat(location->latitude_));
                 o("longitude", JsonFloat(location->longitude_));
               }));
      }
      break;
    }
    case td_api::messageContact::ID: {
      const auto *contact = static_cast<const td_api::messageContact *>(content)->contact_.get();
      object("contact_information", json_object([contact](auto &o) {
               o("first_name", JsonRawString(contact->first_name_));
               o("last_name", JsonRawString(contact->last_name_));
               o("phone_number", JsonRawString(format_export_phone_number(contact->phone_number_)));
             }));
      if (!contact->vcard_.empty()) {
        object("contact_vcard", JsonRawString(get_file_path(file_)));
        object("contact_vcard_file_size", JsonLong(static_cast<int64>(contact->vcard_.size())));
      }
      break;
    }
    case td_api::messageGame::ID: {
      const auto *game = static_cast<const td_api::messageGame *>(content)->game_.get();
      store_string(object, "game_title", game->title_);
      store_string(object, "game_description", game->description_);
      auto bot_user_id = get_game_bot_user_id();
      if (bot_user_id.is_valid() && !game->short_name_.empty() && td_->user_manager_->is_user_bot(bot_user_id)) {
        auto username = td_->user_manager_->get_user_first_username(bot_user_id);
        if (!username.empty()) {
          object("game_link",
                 JsonRawString(PSTRING() << LinkManager::get_t_me_url() << username << "?game=" << game->short_name_));
        }
      }
      break;
    }
    case td_api::messagePoll::ID: {
      const auto *poll = static_cast<const td_api::messagePoll *>(content)->poll_.get();
      object("poll", json_object([poll](auto &o) {
               o("question", ExportJsonText(poll->question_.get(), false));
               o("closed", JsonBool(poll->is_closed_));
               o("total_voters", JsonInt(poll->total_voter_count_));
               o("answers", json_array([poll](auto &array) {
                   for (const auto &option : poll->options_) {
                     array(json_object([&option](auto &answer) {
                       answer("text", ExportJsonText(option->text_.get(), false));
                       answer("voters", JsonInt(option->voter_count_));
                       answer("chosen", JsonBool(option->is_chosen_));
                     }));
                   }
                 }));
             }));
      break;
    }
    case td_api::messageChecklist::ID: {
      const auto *list = static_cast<const td_api::messageChecklist *>(content)->list_.get();
      object("todo_list", json_object([list](auto &o) {
               o("title", ExportJsonText(list->title_.get(), false));
               o("others_can_append", JsonBool(list->others_can_add_tasks_));
               o("others_can_complete", JsonBool(list->others_can_mark_tasks_as_done_));
               o("answers", get_checklist_tasks_json(list->tasks_));
             }));
      break;
    }
    case td_api::messageInvoice::ID: {
      const auto *m = static_cast<const td_api::messageInvoice *>(content);
      const auto *product_info = m->product_info_.get();
      auto receipt_message_id = get_export_server_message_id(m->receipt_message_id_);
      auto invoice = json_object([m, product_info, receipt_message_id](auto &o) {
        o("title", JsonRawString(product_info->title_));
        o("description",
          JsonRawString(product_info->description_ == nullptr ? Slice() : Slice(product_info->description_->text_)));
        o("amount", JsonLong(m->total_amount_));
        o("currency", JsonRawString(m->currency_));
        if (receipt_message_id != 0) {
          o("receipt_message_id", JsonInt(receipt_message_id));
        }
      });
      // the object is written as a string
      object("invoice_information", JsonRawString(get_embedded_json(invoice, json_offset_)));
      break;
    }
    case td_api::messageGiveaway::ID: {
      const auto *m = static_cast<const td_api::messageGiveaway *>(content);
      const auto *parameters = m->parameters_.get();
      int32 month_count;
      int64 star_count;
      get_export_giveaway_prize(m->prize_.get(), month_count, star_count);
      object("giveaway_information", json_object([&](auto &o) {
               o("quantity", JsonInt(m->winner_count_));
               o("months", JsonInt(month_count));
               o("until_date", JsonRawString(format_date(parameters->winners_selection_date_)));
               o("channels", json_array([parameters](auto &array) {
                   array(JsonLong(get_export_dialog_bare_id(DialogId(parameters->boosted_chat_id_))));
                   for (auto chat_id : parameters->additional_chat_ids_) {
                     array(JsonLong(get_export_dialog_bare_id(DialogId(chat_id))));
                   }
                 }));
               o("countries", json_array(parameters->country_codes_,
                                         [](const string &country_code) { return JsonRawString(country_code); }));
               o("additional_prize", JsonRawString(parameters->prize_description_));
               o("stars", JsonLong(star_count));
               o("is_only_new_subscribers", JsonBool(parameters->only_new_members_));
             }));
      break;
    }
    case td_api::messageGiveawayWinners::ID: {
      const auto *m = static_cast<const td_api::messageGiveawayWinners *>(content);
      int32 month_count;
      int64 star_count;
      get_export_giveaway_prize(m->prize_.get(), month_count, star_count);
      object("giveaway_results", json_object([&](auto &o) {
               o("channel", JsonLong(get_export_dialog_bare_id(DialogId(m->boosted_chat_id_))));
               o("winners", json_array(m->winner_user_ids_, [](int64 user_id) { return JsonLong(user_id); }));
               o("additional_prize", JsonRawString(m->prize_description_));
               o("until_date", JsonRawString(format_date(m->actual_winners_selection_date_)));
               o("launch_message_id", JsonInt(get_export_server_message_id(m->giveaway_message_id_)));
               o("additional_peers_count", JsonInt(m->additional_chat_count_));
               o("winners_count", JsonInt(m->winner_count_));
               o("unclaimed_count", JsonInt(m->unclaimed_prize_count_));
               o("months", JsonInt(month_count));
               o("stars", JsonLong(star_count));
               o("is_refunded", JsonBool(m->was_refunded_));
               o("is_only_new_subscribers", JsonBool(m->only_new_members_));
             }));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void ExportJsonMessage::store_service_content(JsonObjectScope &object, const td_api::MessageContent *content) const {
  switch (content->get_id()) {
    case td_api::messageBasicGroupChatCreate::ID: {
      const auto *m = static_cast<const td_api::messageBasicGroupChatCreate *>(content);
      store_actor(object);
      object("action", "create_group");
      store_string(object, "title", m->title_);
      store_user_names(object, "members", m->member_user_ids_);
      break;
    }
    case td_api::messageChatChangeTitle::ID:
      store_actor(object);
      object("action", "edit_group_title");
      store_string(object, "title", static_cast<const td_api::messageChatChangeTitle *>(content)->title_);
      break;
    case td_api::messageChatChangePhoto::ID: {
      const auto *photo = static_cast<const td_api::messageChatChangePhoto *>(content)->photo_.get();
      store_actor(object);
      object("action", "edit_group_photo");
      store_photo(object, photo == nullptr ? nullptr : get_export_photo_size(photo->sizes_), true);
      break;
    }
    case td_api::messageChatDeletePhoto::ID:
      store_actor(object);
      object("action", "delete_group_photo");
      break;
    case td_api::messageChatAddMembers::ID:
      store_actor(object);
      object("action", "invite_members");
      store_user_names(object, "members",
                       static_cast<const td_api::messageChatAddMembers *>(content)->member_user_ids_);
      break;
    case td_api::messageChatDeleteMember::ID:
      store_actor(object);
      object("action", "remove_members");
      store_user_names(object, "members", {static_cast<const td_api::messageChatDeleteMember *>(content)->user_id_});
      break;
    case td_api::messageChatJoinByLink::ID:
      store_actor(object);
      object("action", "join_group_by_link");
      break;
    case td_api::messageSupergroupChatCreate::ID:
      store_actor(object);
      object("action", "create_channel");
      store_string(object, "title", static_cast<const td_api::messageSupergroupChatCreate *>(content)->title_);
      break;
    case td_api::messageChatUpgradeTo::ID:
      store_actor(object);
      object("action", "migrate_to_supergroup");
      break;
    case td_api::messageChatUpgradeFrom::ID:
      store_actor(object);
      object("action", "migrate_from_group");
      store_string(object, "title", static_cast<const td_api::messageChatUpgradeFrom *>(content)->title_);
      break;
    case td_api::messagePinMessage::ID:
      store_actor(object);
      object("action", "pin_message");
      store_same_chat_message_id(object, "message_id",
                                 static_cast<const td_api::messagePinMessage *>(content)->message_id_);
      break;
    case td_api::messageGameScore::ID: {
      const auto *m = static_cast<const td_api::messageGameScore *>(content);
      store_actor(object);
      object("action", "score_in_game");
      store_same_chat_message_id(object, "game_message_id", m->game_message_id_);
      object("score", JsonInt(m->score_));
      break;
    }
    case td_api::messagePaymentSuccessful::ID: {
      const auto *m = static_cast<const td_api::messagePaymentSuccessful *>(content);
      object("action", "send_payment");
      object("amount", JsonLong(m->total_amount_));
      store_string(object, "currency", m->currency_);
      store_message_reference(object, "invoice_message_id", m->invoice_chat_id_, m->invoice_message_id_);
      if (m->is_first_recurring_) {
        object("recurring", "init");
      } else if (m->is_recurring_) {
        object("recurring", "used");
      }
      break;
    }
    case td_api::messageCall::ID: {
      const auto *m = static_cast<const td_api::messageCall *>(content);
      store_actor(object);
      object("action", "phone_call");
      if (m->duration_ != 0) {
        object("duration_seconds", JsonInt(m->duration_));
      }
      store_string(object, "discard_reason", get_call_discard_reason_string(m->discard_reason_.get()));
      break;
    }
    case td_api::messageGroupCall::ID: {
      const auto *m = static_cast<const td_api::messageGroupCall *>(content);
      store_actor(object);
      object("action", "conference_call");
      if (m->duration_ != 0) {
        object("duration_seconds", JsonInt(m->duration_));
      }
      object("is_active", JsonBool(m->is_active_));
      object("is_missed", JsonBool(m->was_missed_));
      break;
    }
    case td_api::messageScreenshotTaken::ID:
      store_actor(object);
      object("action", "take_screenshot");
      break;
    case td_api::messageCustomServiceAction::ID:
      store_actor(object);
      store_string(object, "information_text", static_cast<const td_api::messageCustomServiceAction *>(content)->text_);
      break;
    case td_api::messageBotWriteAccessAllowed::ID: {
      const auto *reason = static_cast<const td_api::messageBotWriteAccessAllowed *>(content)->reason_.get();
      switch (reason->get_id()) {
        case td_api::botWriteAccessAllowReasonAddedToAttachmentMenu::ID:
          object("action", "attach_menu_bot_allowed");
          break;
        case td_api::botWriteAccessAllowReasonAcceptedRequest::ID:
          object("action", "web_app_bot_allowed");
          break;
        case td_api::botWriteAccessAllowReasonLaunchedWebApp::ID: {
          const auto *web_app =
              static_cast<const td_api::botWriteAccessAllowReasonLaunchedWebApp *>(reason)->web_app_.get();
          object("action", "allow_sending_messages");
          if (web_app != nullptr) {
            store_string(object, "reason_app_name", web_app->title_);
          }
          break;
        }
        case td_api::botWriteAccessAllowReasonConnectedWebsite::ID:
          object("action", "allow_sending_messages");
          store_string(object, "reason_domain",
                       static_cast<const td_api::botWriteAccessAllowReasonConnectedWebsite *>(reason)->domain_name_);
          break;
        default:
          UNREACHABLE();
      }
      break;
    }
    case td_api::messagePassportDataSent::ID: {
      const auto &types = static_cast<const td_api::messagePassportDataSent *>(content)->types_;
      object("action", "send_passport_values");
      object("values", json_array(types, [](const td_api::object_ptr<td_api::PassportElementType> &type) {
               return JsonRawString(get_passport_element_type_string(type.get()));
             }));
      break;
    }
    case td_api::messageContactRegistered::ID:
      store_actor(object);
      object("action", "joined_telegram");
      break;
    case td_api::messageUnpaidMessagesAllowedByLink::ID:
      store_actor(object);
      object("action", "no_paid_messages_token_used");
      break;
    case td_api::messageBotInvite::ID:
      store_actor(object);
      object("action", "bot_invite");
      object("inviter",
             ExportJsonName(get_user_name(static_cast<const td_api::messageBotInvite *>(content)->inviter_user_id_)));
      break;
    case td_api::messageProximityAlertTriggered::ID: {
      const auto *m = static_cast<const td_api::messageProximityAlertTriggered *>(content);
      object("action", "proximity_reached");
      store_peer(object, "from", "from_id", get_export_message_sender_dialog_id(m->traveler_id_.get()));
      store_peer(object, "to", "to_id", get_export_message_sender_dialog_id(m->watcher_id_.get()));
      object("distance", JsonInt(m->distance_));
      break;
    }
    case td_api::messageVideoChatStarted::ID:
      store_actor(object);
      object("action", "group_call");
      break;
    case td_api::messageVideoChatEnded::ID: {
      auto duration = static_cast<const td_api::messageVideoChatEnded *>(content)->duration_;
      store_actor(object);
      object("action", "group_call");
      if (duration != 0) {
        object("duration", JsonInt(duration));
      }
      break;
    }
    case td_api::messageVideoChatScheduled::ID:
      store_actor(object);
      object("action", "group_call_scheduled");
      object("schedule_date", JsonInt(static_cast<const td_api::messageVideoChatScheduled *>(content)->start_date_));
      break;
    case td_api::messageInviteVideoChatParticipants::ID:
      store_actor(object);
      object("action", "invite_to_group_call");
      store_user_names(object, "members",
                       static_cast<const td_api::messageInviteVideoChatParticipants *>(content)->user_ids_);
      break;
    case td_api::messageChatSetMessageAutoDeleteTime::ID:
      store_actor(object);
      object("action", "set_messages_ttl");
      object("period",
             JsonInt(
                 static_cast<const td_api::messageChatSetMessageAutoDeleteTime *>(content)->message_auto_delete_time_));
      break;
    case td_api::messageChatSetTheme::ID: {
      const auto *theme = static_cast<const td_api::messageChatSetTheme *>(content)->theme_.get();
      store_actor(object);
      object("action", "edit_chat_theme");
      if (theme != nullptr && theme->get_id() == td_api::chatThemeEmoji::ID) {
        store_string(object, "emoticon", static_cast<const td_api::chatThemeEmoji *>(theme)->name_);
      }
      break;
    }
    case td_api::messageChatJoinByRequest::ID:
      store_actor(object);
      object("action", "join_group_by_request");
      break;
    case td_api::messageChatJoinFromCommunity::ID:
      store_actor(object);
      object("action", "join_group_via_community");
      object("community_id",
             JsonLong(static_cast<const td_api::messageChatJoinFromCommunity *>(content)->community_id_));
      break;
    case td_api::messageChatAddedToCommunity::ID:
      store_actor(object);
      object("action", "add_to_community");
      object("community_id",
             JsonLong(static_cast<const td_api::messageChatAddedToCommunity *>(content)->community_id_));
      break;
    case td_api::messageChatRemovedFromCommunity::ID:
      store_actor(object);
      object("action", "remove_from_community");
      break;
    case td_api::messageWebAppDataSent::ID:
      object("action", "send_webview_data");
      store_string(object, "text", static_cast<const td_api::messageWebAppDataSent *>(content)->button_text_);
      break;
    case td_api::messageGiftedPremium::ID: {
      const auto *m = static_cast<const td_api::messageGiftedPremium *>(content);
      store_actor(object);
      object("action", "send_premium_gift");
      store_string(object, "cost", format_export_money_amount(m->amount_, m->currency_));
      if (m->day_count_ != 0) {
        object("days", JsonInt(m->day_count_));
      }
      break;
    }
    case td_api::messageForumTopicCreated::ID:
      store_actor(object);
      object("action", "topic_created");
      store_string(object, "title", static_cast<const td_api::messageForumTopicCreated *>(content)->name_);
      break;
    case td_api::messageForumTopicEdited::ID: {
      const auto *m = static_cast<const td_api::messageForumTopicEdited *>(content);
      store_actor(object);
      object("action", "topic_edit");
      store_string(object, "new_title", m->name_);
      object("new_icon_emoji_id", ExportJsonUnsigned(m->edit_icon_custom_emoji_id_ ? m->icon_custom_emoji_id_ : 0));
      break;
    }
    case td_api::messageForumTopicIsClosedToggled::ID:
    case td_api::messageForumTopicIsHiddenToggled::ID:
      store_actor(object);
      object("action", "topic_edit");
      object("new_icon_emoji_id", ExportJsonUnsigned(0));
      break;
    case td_api::messageSuggestProfilePhoto::ID: {
      const auto *photo = static_cast<const td_api::messageSuggestProfilePhoto *>(content)->photo_.get();
      store_actor(object);
      object("action", "suggest_profile_photo");
      store_photo(object, photo == nullptr ? nullptr : get_export_photo_size(photo->sizes_), true);
      break;
    }
    case td_api::messageUsersShared::ID: {
      const auto *m = static_cast<const td_api::messageUsersShared *>(content);
      store_actor(object);
      object("action", "requested_peer");
      object("button_id", JsonInt(m->button_id_));
      auto peers = json_array([m](auto &array) {
        for (const auto &user : m->users_) {
          array(JsonLong(get_peer_id_value(DialogId(UserId(user->user_id_)))));
        }
      });
      // the array is written as a string
      object("peers", JsonRawString(get_embedded_json(peers, json_offset_)));
      break;
    }
    case td_api::messageChatShared::ID: {
      const auto *m = static_cast<const td_api::messageChatShared *>(content);
      store_actor(object);
      object("action", "requested_peer");
      object("button_id", JsonInt(m->button_id_));
      auto peers = json_array([m](auto &array) {
        if (m->chat_ != nullptr) {
          array(JsonLong(get_peer_id_value(DialogId(m->chat_->chat_id_))));
        }
      });
      // the array is written as a string
      object("peers", JsonRawString(get_embedded_json(peers, json_offset_)));
      break;
    }
    case td_api::messagePremiumGiftCode::ID: {
      const auto *m = static_cast<const td_api::messagePremiumGiftCode *>(content);
      object("action", "gift_code_prize");
      store_string(object, "gift_code", m->code_);
      if (m->creator_id_ != nullptr) {
        object("boost_peer_id",
               JsonRawString(get_peer_id_string(get_export_message_sender_dialog_id(m->creator_id_.get()))));
      }
      object("days", JsonInt(m->day_count_));
      object("unclaimed", JsonBool(m->is_unclaimed_));
      object("via_giveaway", JsonBool(m->is_from_giveaway_));
      break;
    }
    case td_api::messageGiveawayCreated::ID:
      object("action", "giveaway_launch");
      break;
    case td_api::messageGiveawayCompleted::ID: {
      const auto *m = static_cast<const td_api::messageGiveawayCompleted *>(content);
      object("action", "giveaway_results");
      object("winners", JsonInt(m->winner_count_));
      object("unclaimed", JsonInt(m->unclaimed_prize_count_));
      object("stars", JsonBool(m->is_star_giveaway_));
      break;
    }
    case td_api::messageChatSetBackground::ID: {
      auto old_background_message_id =
          static_cast<const td_api::messageChatSetBackground *>(content)->old_background_message_id_;
      store_actor(object);
      object("action", old_background_message_id != 0 ? "set_same_chat_wallpaper" : "set_chat_wallpaper");
      store_same_chat_message_id(object, "message_id", old_background_message_id);
      break;
    }
    case td_api::messageChatBoost::ID:
      store_actor(object);
      object("action", "boost_apply");
      object("boosts", JsonInt(static_cast<const td_api::messageChatBoost *>(content)->boost_count_));
      break;
    case td_api::messagePaymentRefunded::ID: {
      const auto *m = static_cast<const td_api::messagePaymentRefunded *>(content);
      auto owner_dialog_id = get_export_message_sender_dialog_id(m->owner_id_.get());
      object("action", "refunded_payment");
      object("amount", JsonLong(m->total_amount_));
      store_string(object, "currency", m->currency_);
      object("peer_name", ExportJsonName(get_dialog_name(owner_dialog_id)));
      object("peer_id", JsonRawString(get_peer_id_string(owner_dialog_id)));
      store_string(object, "charge_id", m->telegram_payment_charge_id_);
      break;
    }
    case td_api::messageGiftedStars::ID: {
      const auto *m = static_cast<const td_api::messageGiftedStars *>(content);
      store_actor(object);
      object("action", "send_stars_gift");
      store_string(object, "cost", format_export_money_amount(m->amount_, m->currency_));
      if (m->star_count_ != 0) {
        object("amount_whole", JsonLong(m->star_count_));
        object("amount_nano", JsonInt(0));
      }
      break;
    }
    case td_api::messageGiftedGrams::ID: {
      auto gram_amount = static_cast<const td_api::messageGiftedGrams *>(content)->gram_amount_;
      store_actor(object);
      object("action", "send_ton_gift");
      if (gram_amount != 0) {
        object("amount_whole", JsonLong(gram_amount / EXPORT_NANOS_IN_GRAM));
        object("amount_nano", JsonLong(gram_amount % EXPORT_NANOS_IN_GRAM));
      }
      break;
    }
    case td_api::messageGiveawayPrizeStars::ID: {
      const auto *m = static_cast<const td_api::messageGiveawayPrizeStars *>(content);
      DialogId boosted_dialog_id(m->boosted_chat_id_);
      store_actor(object);
      object("action", "stars_prize");
      object("boost_peer_id", JsonRawString(get_peer_id_string(boosted_dialog_id)));
      object("boost_peer_name", ExportJsonName(get_dialog_name(boosted_dialog_id)));
      object("stars", JsonLong(m->star_count_));
      object("is_unclaimed", JsonBool(m->is_unclaimed_));
      object("giveaway_msg_id", JsonInt(get_export_server_message_id(m->giveaway_message_id_)));
      store_string(object, "transaction_id", m->transaction_id_);
      break;
    }
    case td_api::messageGift::ID: {
      const auto *m = static_cast<const td_api::messageGift *>(content);
      const auto *gift = m->gift_.get();
      store_star_gift(object, gift == nullptr ? 0 : gift->id_, gift == nullptr ? 0 : gift->star_count_,
                      gift != nullptr && gift->overall_limits_ != nullptr, m->is_private_, m->text_.get());
      break;
    }
    case td_api::messageRefundedUpgradedGift::ID: {
      const auto *gift = static_cast<const td_api::messageRefundedUpgradedGift *>(content)->gift_.get();
      store_star_gift(object, gift == nullptr ? 0 : gift->id_, gift == nullptr ? 0 : gift->star_count_,
                      gift != nullptr && gift->overall_limits_ != nullptr, false, nullptr);
      break;
    }
    case td_api::messageUpgradedGift::ID: {
      const auto *gift = static_cast<const td_api::messageUpgradedGift *>(content)->gift_.get();
      store_star_gift(object, gift == nullptr ? 0 : gift->id_, 0, false, false, nullptr);
      break;
    }
    case td_api::messageUpgradedGiftPurchaseOffer::ID: {
      const auto *gift = static_cast<const td_api::messageUpgradedGiftPurchaseOffer *>(content)->gift_.get();
      store_star_gift(object, gift == nullptr ? 0 : gift->id_, 0, false, false, nullptr);
      break;
    }
    case td_api::messageUpgradedGiftPurchaseOfferRejected::ID: {
      const auto *gift = static_cast<const td_api::messageUpgradedGiftPurchaseOfferRejected *>(content)->gift_.get();
      store_star_gift(object, gift == nullptr ? 0 : gift->id_, 0, false, false, nullptr);
      break;
    }
    case td_api::messageUpgradedGiftOnChainPurchaseOffer::ID: {
      const auto *gift = static_cast<const td_api::messageUpgradedGiftOnChainPurchaseOffer *>(content)->gift_.get();
      store_star_gift(object, gift == nullptr ? 0 : gift->id_, 0, false, false, nullptr);
      break;
    }
    case td_api::messagePaidMessagesRefunded::ID: {
      const auto *m = static_cast<const td_api::messagePaidMessagesRefunded *>(content);
      store_actor(object);
      object("action", "paid_messages_refund");
      object("messages_count", JsonInt(m->message_count_));
      object("stars_count", JsonLong(m->star_count_));
      break;
    }
    case td_api::messagePaidMessagePriceChanged::ID:
      store_actor(object);
      object("action", "paid_messages_price_change");
      object("price_stars",
             JsonLong(static_cast<const td_api::messagePaidMessagePriceChanged *>(content)->paid_message_star_count_));
      object("is_broadcast_messages_allowed", JsonFalse());
      break;
    case td_api::messageDirectMessagePriceChanged::ID: {
      const auto *m = static_cast<const td_api::messageDirectMessagePriceChanged *>(content);
      store_actor(object);
      object("action", "paid_messages_price_change");
      object("price_stars", JsonLong(m->paid_message_star_count_));
      object("is_broadcast_messages_allowed", JsonBool(m->is_enabled_));
      break;
    }
    case td_api::messageChecklistTasksDone::ID: {
      const auto *m = static_cast<const td_api::messageChecklistTasksDone *>(content);
      store_actor(object);
      object("action", "todo_completions");
      object("completed", json_array(m->marked_as_done_task_ids_, [](int32 task_id) { return JsonInt(task_id); }));
      object("incompleted",
             json_array(m->marked_as_not_done_task_ids_, [](int32 task_id) { return JsonInt(task_id); }));
      break;
    }
    case td_api::messageChecklistTasksAdded::ID:
      store_actor(object);
      object("action", "todo_append_tasks");
      object("items",
             get_checklist_tasks_json(static_cast<const td_api::messageChecklistTasksAdded *>(content)->tasks_));
      break;
    case td_api::messagePollOptionAdded::ID: {
      const auto &text = static_cast<const td_api::messagePollOptionAdded *>(content)->text_;
      store_actor(object);
      object("action", "poll_append_answer");
      if (text != nullptr) {
        store_string(object, "option", text->text_);
      }
      break;
    }
    case td_api::messagePollOptionDeleted::ID: {
      const auto &text = static_cast<const td_api::messagePollOptionDeleted *>(content)->text_;
      store_actor(object);
      object("action", "poll_delete_answer");
      if (text != nullptr) {
        store_string(object, "option", text->text_);
      }
      break;
    }
    case td_api::messageSuggestedPostApproved::ID: {
      const auto *m = static_cast<const td_api::messageSuggestedPostApproved *>(content);
      store_actor(object);
      object("action", "process_suggested_post");
      store_suggested_post_price(object, m->price_.get());
      object("scheduled_date", JsonInt(m->send_date_));
      break;
    }
    case td_api::messageSuggestedPostApprovalFailed::ID:
      store_actor(object);
      object("action", "process_suggested_post");
      store_suggested_post_price(
          object, static_cast<const td_api::messageSuggestedPostApprovalFailed *>(content)->price_.get());
      break;
    case td_api::messageSuggestedPostDeclined::ID:
      store_actor(object);
      object("action", "process_suggested_post");
      object("rejected", JsonTrue());
      store_string(object, "comment", static_cast<const td_api::messageSuggestedPostDeclined *>(content)->comment_);
      break;
    case td_api::messageSuggestedPostPaid::ID: {
      const auto *m = static_cast<const td_api::messageSuggestedPostPaid *>(content);
      store_actor(object);
      object("action", "suggested_post_success");
      if (m->gram_amount_ != 0 || m->star_amount_ == nullptr) {
        store_price(object, m->gram_amount_ / EXPORT_NANOS_IN_GRAM, m->gram_amount_ % EXPORT_NANOS_IN_GRAM,
                    m->gram_amount_ != 0);
      } else {
        store_price(object, m->star_amount_->star_count_, m->star_amount_->nanostar_count_, false);
      }
      break;
    }
    case td_api::messageSuggestedPostRefunded::ID: {
      const auto *reason = static_cast<const td_api::messageSuggestedPostRefunded *>(content)->reason_.get();
      store_actor(object);
      object("action", "suggested_post_refund");
      object("user_initiated",
             JsonBool(reason != nullptr && reason->get_id() == td_api::suggestedPostRefundReasonPaymentRefunded::ID));
      break;
    }
    case td_api::messageSuggestBirthdate::ID: {
      const auto *birthdate = static_cast<const td_api::messageSuggestBirthdate *>(content)->birthdate_.get();
      store_actor(object);
      object("action", "suggest_birthday");
      object("day", JsonInt(birthdate == nullptr ? 0 : birthdate->day_));
      object("month", JsonInt(birthdate == nullptr ? 0 : birthdate->month_));
      if (birthdate != nullptr && birthdate->year_ != 0) {
        object("year", JsonInt(birthdate->year_));
      }
      break;
    }
    case td_api::messageChatHasProtectedContentToggled::ID:
      store_actor(object);
      object("action", "no_forwards_toggle");
      object(
          "new_value",
          JsonBool(
              static_cast<const td_api::messageChatHasProtectedContentToggled *>(content)->new_has_protected_content_));
      break;
    case td_api::messageChatHasProtectedContentDisableRequested::ID:
      store_actor(object);
      object("action", "no_forwards_request");
      object(
          "expired",
          JsonBool(static_cast<const td_api::messageChatHasProtectedContentDisableRequested *>(content)->is_expired_));
      object("new_value", JsonFalse());
      break;
    case td_api::messageChatOwnerLeft::ID:
      store_actor(object);
      object("action", "new_creator_pending");
      object("new_creator", ExportJsonName(get_user_name(
                                static_cast<const td_api::messageChatOwnerLeft *>(content)->new_owner_user_id_)));
      break;
    case td_api::messageChatOwnerChanged::ID:
      store_actor(object);
      object("action", "change_creator");
      object("new_creator", ExportJsonName(get_user_name(
                                static_cast<const td_api::messageChatOwnerChanged *>(content)->new_owner_user_id_)));
      break;
    case td_api::messageManagedBotCreated::ID:
      store_actor(object);
      object("action", "managed_bot_created");
      object("bot", ExportJsonName(
                        get_user_name(static_cast<const td_api::messageManagedBotCreated *>(content)->bot_user_id_)));
      break;
    case td_api::messageTonWalletTransfer::ID: {
      const auto *m = static_cast<const td_api::messageTonWalletTransfer *>(content);
      store_actor(object);
      object("action", "gram_transfer");
      object("amount_whole", JsonLong(m->amount_ / EXPORT_NANOS_IN_GRAM));
      object("amount_nano", JsonLong(m->amount_ % EXPORT_NANOS_IN_GRAM));
      store_string(object, "peer_address", m->peer_address_);
      store_string(object, "transaction_id", m->transaction_id_);
      object("comment_encrypted", JsonBool(m->is_comment_encrypted_));
      store_string(object, m->is_comment_encrypted_ ? Slice("encrypted_comment_base64") : Slice("comment"),
                   m->comment_);
      break;
    }
    case td_api::messageTonConnectRequest::ID: {
      const auto *m = static_cast<const td_api::messageTonConnectRequest *>(content);
      const auto *state = m->state_.get();
      auto state_id = state == nullptr ? 0 : state->get_id();
      store_actor(object);
      object("action", "ton_connect_request");
      object("session_id", ExportJsonUnsigned(m->session_id_));
      object("expires",
             JsonInt(state_id == td_api::tonConnectRequestStatePending::ID
                         ? static_cast<const td_api::tonConnectRequestStatePending *>(state)->expiration_date_
                         : 0));
      store_string(object, "topic", m->topic_);
      store_string(object, "trace_id", m->trace_id_);
      object("accepted", JsonBool(state_id == td_api::tonConnectRequestStateAccepted::ID));
      object("declined", JsonBool(state_id == td_api::tonConnectRequestStateRejected::ID));
      break;
    }
    default:
      UNREACHABLE();
  }
}

void ExportJsonMessage::store_text(JsonObjectScope &object, const td_api::MessageContent *content) const {
  Slice text;
  const vector<td_api::object_ptr<td_api::textEntity>> *entities = nullptr;
  if (content->get_id() == td_api::messageAnimatedEmoji::ID) {
    text = static_cast<const td_api::messageAnimatedEmoji *>(content)->emoji_;
  } else {
    const auto *formatted_text = get_export_message_text(content);
    if (formatted_text != nullptr) {
      text = formatted_text->text_;
      entities = &formatted_text->entities_;
    }
  }
  object("text", ExportJsonText(text, entities, false, &custom_emoji_files_));
  object("text_entities", ExportJsonText(text, entities, true, &custom_emoji_files_));
}

void ExportJsonMessage::store_inline_buttons(JsonObjectScope &object) const {
  if (message_.reply_markup_ == nullptr || message_.reply_markup_->get_id() != td_api::replyMarkupInlineKeyboard::ID) {
    return;
  }
  const auto &rows = static_cast<const td_api::replyMarkupInlineKeyboard *>(message_.reply_markup_.get())->rows_;
  bool has_buttons = false;
  for (const auto &row : rows) {
    has_buttons |= !row.empty();
  }
  if (!has_buttons) {
    return;
  }
  object("inline_bot_buttons", json_array([&rows](auto &array) {
           for (const auto &row : rows) {
             if (!row.empty()) {
               array(json_array(row, [](const td_api::object_ptr<td_api::inlineKeyboardButton> &button) {
                 return ExportJsonInlineButton(button.get());
               }));
             }
           }
         }));
}

void ExportJsonMessage::store_reactions(JsonObjectScope &object) const {
  const auto *interaction_info = message_.interaction_info_.get();
  if (interaction_info == nullptr || interaction_info->reactions_ == nullptr ||
      interaction_info->reactions_->reactions_.empty()) {
    return;
  }
  object("reactions", json_array([this, interaction_info](auto &array) {
           for (const auto &reaction : interaction_info->reactions_->reactions_) {
             array(json_object([this, &reaction](auto &o) {
               const auto *type = reaction->type_.get();
               auto type_id = type == nullptr ? 0 : type->get_id();
               o("type", type_id == td_api::reactionTypeEmoji::ID         ? "emoji"
                         : type_id == td_api::reactionTypeCustomEmoji::ID ? "custom_emoji"
                         : type_id == td_api::reactionTypePaid::ID        ? "paid"
                                                                          : "empty");
               o("count", JsonInt(reaction->total_count_));
               if (type_id == td_api::reactionTypeEmoji::ID) {
                 o("emoji", JsonRawString(static_cast<const td_api::reactionTypeEmoji *>(type)->emoji_));
               } else if (type_id == td_api::reactionTypeCustomEmoji::ID) {
                 o("document_id", JsonRawString(get_export_custom_emoji_path(
                                      &custom_emoji_files_,
                                      static_cast<const td_api::reactionTypeCustomEmoji *>(type)->custom_emoji_id_)));
               }
               if (!reaction->recent_sender_ids_.empty()) {
                 o("recent", json_array([this, &reaction](auto &recent) {
                     for (const auto &sender_id : reaction->recent_sender_ids_) {
                       auto dialog_id = get_export_message_sender_dialog_id(sender_id.get());
                       recent(json_object([this, dialog_id](auto &r) {
                         r("from", ExportJsonName(get_dialog_name(dialog_id)));
                         r("from_id", JsonRawString(get_peer_id_string(dialog_id)));
                       }));
                     }
                   }));
               }
             }));
           }
         }));
}

void ExportJsonMessage::store(JsonValueScope *scope) const {
  auto object = scope->enter_object();
  auto server_message_id = get_export_server_message_id(message_.id_);
  object("id", JsonInt(is_migrated_ ? server_message_id + EXPORT_MIGRATED_MESSAGE_ID_SHIFT : server_message_id));

  const auto *content = message_.content_.get();
  auto kind = is_history_cleared_ ? ExportContentKind::Service : get_export_content_kind(content);
  if (kind == ExportContentKind::Unsupported) {
    object("type", "unsupported");
    return;
  }

  object("type", kind == ExportContentKind::Service ? "service" : "message");
  object("date", JsonRawString(format_date(message_.date_)));
  object("date_unixtime", JsonRawString(to_string(message_.date_)));
  if (message_.edit_date_ != 0) {
    object("edited", JsonRawString(format_date(message_.edit_date_)));
    object("edited_unixtime", JsonRawString(to_string(message_.edit_date_)));
  }

  if (is_history_cleared_) {
    store_actor(object);
    object("action", "clear_history");
  } else if (kind == ExportContentKind::Service) {
    store_service_content(object, content);
  } else {
    store_message_header(object);
    store_media_content(object, content);
  }

  store_text(object, content);
  store_inline_buttons(object);
  store_reactions(object);
}

ExportJsonWriter::ExportJsonWriter(Td *td) : td_(td) {
}

Status ExportJsonWriter::start(const ExportSettings &settings, ExportStats *stats) {
  CHECK(output_ == nullptr);
  CHECK(!settings.path.empty() && settings.path.back() == TD_DIR_SLASH);

  settings_ = settings;
  output_ = td::make_unique<ExportOutputFile>(settings_.path + "result.json", stats);
  if (settings_.only_single_dialog()) {
    dialog_offset_ = 0;
    return Status::OK();
  }

  // a chat is an item of the "list" array of the object of the chat list in the root object
  dialog_offset_ = 3;
  auto buf = StackAllocator::alloc(1 << 12);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 1);
  auto &sb = jb.string_builder();
  sb << '{';
  jb.print_offset();
  sb << "\"about\" : " << JsonRawString(get_export_about_text(ExportAbout::Telegram));
  if (sb.is_error()) {
    return Status::Error("Failed to serialize the exported data");
  }
  return output_->write(sb.as_cslice());
}

static string format_account_date(int32 date, int32 utc_time_offset) {
  return get_export_date_time_string(get_export_local_time(date, utc_time_offset));
}

template <class T>
Status ExportJsonWriter::write_root_item(Slice key, const T &value) {
  CHECK(output_ != nullptr);
  CHECK(!settings_.only_single_dialog());

  auto buf = StackAllocator::alloc(1 << 14);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 1);
  auto &sb = jb.string_builder();
  // the root object always begins with the about text
  sb << ',';
  jb.print_offset();
  sb << JsonString(key) << " : ";
  jb.enter_value() << value;
  if (sb.is_error()) {
    return Status::Error(PSLICE() << "Failed to serialize " << key);
  }
  return output_->write(sb.as_cslice());
}

template <class F>
class ExportJsonList final : public Jsonable {
 public:
  ExportJsonList(ExportAbout about, F store_list) : about_(about), store_list_(std::move(store_list)) {
  }

  void store(JsonValueScope *scope) const {
    auto object = scope->enter_object();
    object("about", JsonRawString(get_export_about_text(about_)));
    object("list", json_array([this](auto &array) { store_list_(array); }));
  }

 private:
  ExportAbout about_;
  F store_list_;
};

template <class F>
static ExportJsonList<F> get_json_list(ExportAbout about, F store_list) {
  return ExportJsonList<F>(about, std::move(store_list));
}

Status ExportJsonWriter::write_personal_info(const ExportPersonalInfo &info) {
  return write_root_item("personal_information", json_object([&info](auto &o) {
                           o("user_id", JsonLong(info.user_id));
                           o("first_name", JsonRawString(info.first_name));
                           o("last_name", JsonRawString(info.last_name));
                           o("phone_number", JsonRawString(format_export_phone_number(info.phone_number)));
                           if (!info.username.empty()) {
                             o("username", JsonRawString(PSLICE() << '@' << info.username));
                           }
                           if (!info.bio.empty()) {
                             o("bio", JsonRawString(info.bio));
                           }
                         }));
}

static const char PHOTO_NOT_INCLUDED[] = "(Photo not included. Change data exporting settings to download.)";

static const char PHOTO_UNAVAILABLE[] = "(Photo unavailable, please try again later)";

static const char PHOTO_TOO_BIG[] = "(Photo exceeds maximum size. Change data exporting settings to download.)";

static Slice get_photo_path(const ExportFile &file) {
  switch (file.skip_reason) {
    case ExportFile::SkipReason::None:
      return file.relative_path;
    case ExportFile::SkipReason::Unavailable:
      return Slice(PHOTO_UNAVAILABLE);
    case ExportFile::SkipReason::FileType:
      return Slice(PHOTO_NOT_INCLUDED);
    case ExportFile::SkipReason::FileSize:
      return Slice(PHOTO_TOO_BIG);
    default:
      UNREACHABLE();
      return Slice();
  }
}

Status ExportJsonWriter::write_root_list_start(Slice key) {
  CHECK(output_ != nullptr);
  CHECK(!settings_.only_single_dialog());
  has_list_items_ = false;

  auto buf = StackAllocator::alloc(1 << 8);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 1);
  auto &sb = jb.string_builder();
  sb << ',';
  jb.print_offset();
  sb << JsonString(key) << " : [";
  if (sb.is_error()) {
    return Status::Error(PSLICE() << "Failed to serialize " << key);
  }
  return output_->write(sb.as_cslice());
}

template <class T, class F>
Status ExportJsonWriter::write_root_list_items(const vector<T> &items, const F &get_item) {
  CHECK(output_ != nullptr);

  auto buf = StackAllocator::alloc(1 << 16);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 2);
  auto &sb = jb.string_builder();
  for (const auto &item : items) {
    if (has_list_items_) {
      sb << ',';
    }
    has_list_items_ = true;
    jb.print_offset();
    jb.enter_value() << get_item(item);
  }
  if (sb.is_error()) {
    return Status::Error("Failed to serialize a list");
  }
  auto data = sb.as_cslice();
  if (data.empty()) {
    return Status::OK();
  }
  return output_->write(data);
}

Status ExportJsonWriter::write_root_list_end() {
  CHECK(output_ != nullptr);

  auto buf = StackAllocator::alloc(1 << 6);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 1);
  auto &sb = jb.string_builder();
  jb.print_offset();
  sb << ']';
  return output_->write(sb.as_cslice());
}

Status ExportJsonWriter::write_userpics_start(int32 count) {
  return write_root_list_start("profile_pictures");
}

Status ExportJsonWriter::write_userpics_slice(const vector<ExportUserpic> &userpics) {
  auto utc_time_offset = settings_.utc_time_offset;
  return write_root_list_items(userpics, [utc_time_offset](const ExportUserpic &userpic) {
    return json_object([&userpic, utc_time_offset](auto &o) {
      if (userpic.date != 0) {
        o("date", JsonRawString(format_account_date(userpic.date, utc_time_offset)));
        o("date_unixtime", JsonRawString(to_string(userpic.date)));
      }
      o("photo", JsonRawString(get_photo_path(userpic.file)));
    });
  });
}

Status ExportJsonWriter::write_userpics_end() {
  return write_root_list_end();
}

Status ExportJsonWriter::write_stories_start(int32 count) {
  return write_root_list_start("stories");
}

Status ExportJsonWriter::write_stories_slice(const vector<ExportStory> &stories, int32 skipped_count) {
  auto utc_time_offset = settings_.utc_time_offset;
  return write_root_list_items(stories, [utc_time_offset](const ExportStory &story) {
    return json_object([&story, utc_time_offset](auto &o) {
      if (story.date != 0) {
        o("date", JsonRawString(format_account_date(story.date, utc_time_offset)));
        o("date_unixtime", JsonRawString(to_string(story.date)));
      }
      if (story.expire_date != 0) {
        o("expires", JsonRawString(format_account_date(story.expire_date, utc_time_offset)));
        o("expires_unixtime", JsonRawString(to_string(story.expire_date)));
      }
      o("pinned", JsonBool(story.is_pinned));
      o("media", JsonRawString(get_photo_path(story.file)));
    });
  });
}

Status ExportJsonWriter::write_stories_end() {
  return write_root_list_end();
}

Status ExportJsonWriter::write_profile_audios_start(int32 count) {
  return write_root_list_start("profile_music");
}

Status ExportJsonWriter::write_profile_audios_slice(const vector<ExportMessage> &audios) {
  return write_root_list_items(audios, [this](const ExportMessage &audio) {
    return ExportJsonMessage(td_, audio, false, no_custom_emoji_files_, settings_.utc_time_offset, 2);
  });
}

Status ExportJsonWriter::write_profile_audios_end() {
  return write_root_list_end();
}

Status ExportJsonWriter::write_contacts(const ExportContactList &contacts) {
  auto utc_time_offset = settings_.utc_time_offset;
  TRY_STATUS(write_root_item("contacts", get_json_list(ExportAbout::Contacts, [&](auto &array) {
                               for (const auto &contact : contacts.contacts) {
                                 array(json_object([&](auto &o) {
                                   if (!contact.first_name.empty() || !contact.last_name.empty() ||
                                       !contact.phone_number.empty()) {
                                     o("first_name", JsonRawString(contact.first_name));
                                     o("last_name", JsonRawString(contact.last_name));
                                     o("phone_number", JsonRawString(format_export_phone_number(contact.phone_number)));
                                   }
                                   o("date", JsonRawString(format_account_date(contact.date, utc_time_offset)));
                                   o("date_unixtime", JsonRawString(to_string(contact.date)));
                                 }));
                               }
                             })));

  auto store_top_peers = [this](auto &array, const vector<ExportTopPeer> &top_peers, Slice category) {
    for (const auto &top_peer : top_peers) {
      auto dialog_id = top_peer.dialog_id;
      Slice type = "user";
      string name;
      if (dialog_id.get_type() == DialogType::User) {
        auto user_id = dialog_id.get_user_id();
        auto first_name = td_->user_manager_->get_user_first_name(user_id);
        auto last_name = td_->user_manager_->get_user_last_name(user_id);
        if (first_name.empty() || last_name.empty()) {
          name = PSTRING() << first_name << last_name;
        } else {
          name = PSTRING() << first_name << ' ' << last_name;
        }
      } else {
        name = td_->dialog_manager_->get_dialog_title(dialog_id);
        bool is_public = false;
        bool is_broadcast = false;
        bool is_supergroup = false;
        if (dialog_id.get_type() == DialogType::Channel) {
          auto channel_id = dialog_id.get_channel_id();
          is_public = !td_->chat_manager_->get_channel_first_username(channel_id).empty();
          is_broadcast = td_->chat_manager_->is_broadcast_channel(channel_id);
          is_supergroup = !is_broadcast;
        }
        if (is_public) {
          type = is_broadcast ? Slice("public_channel") : Slice("public_supergroup");
        } else {
          type = is_broadcast ? Slice("private_channel")
                              : (is_supergroup ? Slice("private_supergroup") : Slice("private_group"));
        }
      }
      array(json_object([&](auto &o) {
        o("id", JsonLong(get_export_dialog_bare_id(dialog_id)));
        o("category", category);
        o("type", type);
        o("name", ExportJsonName(name));
        o("rating", JsonRaw(PSLICE() << StringBuilder::FixedDouble(top_peer.rating, 6)));
      }));
    }
  };
  return write_root_item("frequent_contacts", get_json_list(ExportAbout::FrequentContacts, [&](auto &array) {
                           store_top_peers(array, contacts.correspondents, "people");
                           store_top_peers(array, contacts.inline_bots, "inline_bots");
                           store_top_peers(array, contacts.phone_calls, "calls");
                         }));
}

Status ExportJsonWriter::write_sessions(const ExportSessionList &sessions) {
  auto utc_time_offset = settings_.utc_time_offset;
  TRY_STATUS(write_root_item("sessions", get_json_list(ExportAbout::Sessions, [&](auto &array) {
                               for (const auto &session : sessions.sessions) {
                                 array(json_object([&](auto &o) {
                                   o("last_active",
                                     JsonRawString(format_account_date(session.last_active, utc_time_offset)));
                                   o("last_active_unixtime", JsonRawString(to_string(session.last_active)));
                                   o("last_ip", JsonRawString(session.ip));
                                   o("last_country", JsonRawString(session.country));
                                   o("last_region", JsonRawString(session.region));
                                   o("application_name", ExportJsonName(session.application_name));
                                   if (!session.application_version.empty()) {
                                     o("application_version", JsonRawString(session.application_version));
                                   }
                                   o("device_model", JsonRawString(session.device_model));
                                   o("platform", JsonRawString(session.platform));
                                   o("system_version", JsonRawString(session.system_version));
                                   o("created", JsonRawString(format_account_date(session.created, utc_time_offset)));
                                   o("created_unixtime", JsonRawString(to_string(session.created)));
                                 }));
                               }
                             })));
  return write_root_item("web_sessions", get_json_list(ExportAbout::WebSessions, [&](auto &array) {
                           for (const auto &session : sessions.web_sessions) {
                             array(json_object([&](auto &o) {
                               o("last_active",
                                 JsonRawString(format_account_date(session.last_active, utc_time_offset)));
                               o("last_active_unixtime", JsonRawString(to_string(session.last_active)));
                               o("last_ip", JsonRawString(session.ip));
                               o("last_region", JsonRawString(session.region));
                               o("bot_username", ExportJsonName(session.bot_username));
                               o("domain_name", ExportJsonName(session.domain));
                               o("browser", JsonRawString(session.browser));
                               o("platform", JsonRawString(session.platform));
                               o("created", JsonRawString(format_account_date(session.created, utc_time_offset)));
                               o("created_unixtime", JsonRawString(to_string(session.created)));
                             }));
                           }
                         }));
}

// json_decode keeps strings as they are, but only valid UTF-8 can be written back
static bool has_only_utf8_strings(const JsonValue &value) {
  switch (value.type()) {
    case JsonValue::Type::String:
      return check_utf8(value.get_string());
    case JsonValue::Type::Array:
      for (const auto &item : value.get_array()) {
        if (!has_only_utf8_strings(item)) {
          return false;
        }
      }
      return true;
    case JsonValue::Type::Object: {
      bool result = true;
      value.get_object().foreach([&result](Slice name, const JsonValue &field_value) {
        result = result && check_utf8(name) && has_only_utf8_strings(field_value);
      });
      return result;
    }
    default:
      return true;
  }
}

Status ExportJsonWriter::write_other_data(const ExportFile &file) {
  if (file.skip_reason != ExportFile::SkipReason::None) {
    return Status::OK();
  }
  string path = settings_.path;
  for (auto c : file.relative_path) {
    path += c == '/' ? TD_DIR_SLASH : c;
  }
  auto r_content = read_file_str(path);
  if (r_content.is_error()) {
    return Status::Error(PSLICE() << "Failed to read file \"" << path << '"');
  }
  auto content = r_content.move_as_ok();
  if (content.empty()) {
    return Status::OK();
  }
  auto r_value = json_decode(content);
  if (r_value.is_error()) {
    return Status::Error(PSLICE() << "Failed to parse file \"" << path << '"');
  }
  auto value = r_value.move_as_ok();
  if ((value.type() != JsonValue::Type::Object && value.type() != JsonValue::Type::Array) ||
      !has_only_utf8_strings(value)) {
    return Status::Error(PSLICE() << "Failed to parse file \"" << path << '"');
  }
  return write_root_item("other_data", value);
}

Status ExportJsonWriter::write_dialogs_start(int32 dialog_count) {
  return Status::OK();
}

Status ExportJsonWriter::write_dialog_list_start(bool is_left_channel) {
  auto mode = is_left_channel ? DialogsMode::Left : DialogsMode::Chats;
  if (dialogs_mode_ == mode) {
    return Status::OK();
  }
  if (dialogs_mode_ != DialogsMode::None) {
    TRY_STATUS(write_dialog_list_end());
  }
  dialogs_mode_ = mode;
  has_dialogs_ = false;

  auto buf = StackAllocator::alloc(1 << 12);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 1);
  auto &sb = jb.string_builder();
  sb << ',';
  jb.print_offset();
  sb << (is_left_channel ? Slice("\"left_chats\" : {") : Slice("\"chats\" : {"));
  jb.inc_offset();
  jb.print_offset();
  sb << "\"about\" : "
     << JsonRawString(get_export_about_text(is_left_channel ? ExportAbout::LeftChats : ExportAbout::Chats)) << ',';
  jb.print_offset();
  sb << "\"list\" : [";
  if (sb.is_error()) {
    return Status::Error("Failed to serialize the chat list");
  }
  return output_->write(sb.as_cslice());
}

Status ExportJsonWriter::write_dialog_list_end() {
  auto buf = StackAllocator::alloc(1 << 6);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), 2);
  auto &sb = jb.string_builder();
  jb.print_offset();
  sb << ']';
  jb.dec_offset();
  jb.print_offset();
  sb << '}';
  return output_->write(sb.as_cslice());
}

Status ExportJsonWriter::write_dialog_start(const ExportDialogInfo &dialog) {
  CHECK(output_ != nullptr);
  if (!settings_.only_single_dialog()) {
    TRY_STATUS(write_dialog_list_start(dialog.is_left_channel));
  }
  has_messages_ = false;

  using Type = ExportDialogInfo::Type;
  auto buf = StackAllocator::alloc(1 << 12);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), dialog_offset_);
  auto &sb = jb.string_builder();
  if (!settings_.only_single_dialog()) {
    if (has_dialogs_) {
      sb << ',';
    }
    has_dialogs_ = true;
    jb.print_offset();
  }
  sb << '{';
  jb.inc_offset();
  if (dialog.type != Type::Self && dialog.type != Type::Replies && dialog.type != Type::VerifyCodes) {
    jb.print_offset();
    sb << "\"name\" : ";
    if (dialog.name.empty()) {
      sb << JsonNull();
    } else {
      sb << JsonRawString(dialog.name);
    }
    sb << ',';
  }
  jb.print_offset();
  sb << "\"type\" : ";
  auto type = get_dialog_type_string(dialog.type);
  if (type.empty()) {
    sb << JsonNull();
  } else {
    sb << JsonRawString(type);
  }
  sb << ',';
  jb.print_offset();
  sb << "\"id\" : " << get_export_dialog_bare_id(dialog.dialog_id) << ',';
  jb.print_offset();
  sb << "\"messages\" : [";
  if (sb.is_error()) {
    return Status::Error("Failed to serialize the chat");
  }
  return output_->write(sb.as_cslice());
}

Status ExportJsonWriter::write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated,
                                               const ExportCustomEmojiFiles &custom_emoji_files) {
  CHECK(output_ != nullptr);

  auto message_offset = dialog_offset_ + MESSAGE_JSON_OFFSET;
  auto buf = StackAllocator::alloc(1 << 16);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), message_offset);
  auto &sb = jb.string_builder();
  for (const auto &export_message : messages) {
    const auto &message = export_message.message;
    if (message == nullptr || !settings_.is_inside_date_range(message->date_)) {
      continue;
    }
    if (has_messages_) {
      sb << ',';
    }
    has_messages_ = true;
    jb.print_offset();
    jb.enter_value() << ExportJsonMessage(td_, export_message, is_migrated, custom_emoji_files,
                                          settings_.utc_time_offset, message_offset);
  }
  if (sb.is_error()) {
    return Status::Error("Failed to serialize messages");
  }
  auto data = sb.as_cslice();
  if (data.empty()) {
    return Status::OK();
  }
  return output_->write(data);
}

Status ExportJsonWriter::write_dialog_end(bool only_my_messages) {
  CHECK(output_ != nullptr);

  auto buf = StackAllocator::alloc(1 << 6);
  JsonBuilder jb(StringBuilder(buf.as_slice(), true), dialog_offset_ + 1);
  auto &sb = jb.string_builder();
  jb.print_offset();
  sb << ']';
  jb.dec_offset();
  jb.print_offset();
  sb << '}';
  if (settings_.only_single_dialog()) {
    sb << '\n';
  }
  return output_->write(sb.as_cslice());
}

Status ExportJsonWriter::write_dialogs_end() {
  CHECK(output_ != nullptr);
  if (dialogs_mode_ == DialogsMode::None) {
    return Status::OK();
  }
  dialogs_mode_ = DialogsMode::None;
  return write_dialog_list_end();
}

Status ExportJsonWriter::finish() {
  CHECK(output_ != nullptr);
  if (settings_.only_single_dialog()) {
    return Status::OK();
  }
  return output_->write("\n}\n");
}

}  // namespace td
