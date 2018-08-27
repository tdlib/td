//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/SignalSlot.h"
#include "td/actor/Timeout.h"

#include "td/db/binlog/BinlogEvent.h"

#include "td/telegram/AccessRights.h"
#include "td/telegram/CallDiscardReason.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/Contact.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/Game.h"
#include "td/telegram/Global.h"
#include "td/telegram/Location.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesDb.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/Payments.h"
#include "td/telegram/Photo.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/SecretChatId.h"
#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/SecureValue.h"
#include "td/telegram/UserId.h"
#include "td/telegram/WebPageId.h"

#include "td/utils/buffer.h"
#include "td/utils/ChangesProcessor.h"
#include "td/utils/common.h"
#include "td/utils/Heap.h"
#include "td/utils/Hints.h"
#include "td/utils/logging.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_storers.h"

#include <array>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

class Td;

class MultiSequenceDispatcher;

// Do not forget to update MessagesManager::update_message_content when one of the inheritors of this class changes
class MessageContent {
 public:
  MessageContent() = default;
  MessageContent(const MessageContent &) = default;
  MessageContent &operator=(const MessageContent &) = default;
  MessageContent(MessageContent &&) = default;
  MessageContent &operator=(MessageContent &&) = default;

  virtual int32 get_id() const = 0;
  virtual ~MessageContent() = default;
};

class MessageText : public MessageContent {
 public:
  FormattedText text;
  WebPageId web_page_id;

  MessageText() = default;
  MessageText(FormattedText text, WebPageId web_page_id) : text(std::move(text)), web_page_id(web_page_id) {
  }

  static const int32 ID = 0;
  int32 get_id() const override {
    return ID;
  }
};

class MessageAnimation : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageAnimation() = default;
  MessageAnimation(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  static const int32 ID = 1;
  int32 get_id() const override {
    return ID;
  }
};

class MessageAudio : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageAudio() = default;
  MessageAudio(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  static const int32 ID = 2;
  int32 get_id() const override {
    return ID;
  }
};

class MessageDocument : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageDocument() = default;
  MessageDocument(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  static const int32 ID = 3;
  int32 get_id() const override {
    return ID;
  }
};

class MessagePhoto : public MessageContent {
 public:
  Photo photo;

  FormattedText caption;

  MessagePhoto() = default;
  MessagePhoto(Photo &&photo, FormattedText &&caption) : photo(std::move(photo)), caption(std::move(caption)) {
  }

  static const int32 ID = 4;
  int32 get_id() const override {
    return ID;
  }
};

class MessageSticker : public MessageContent {
 public:
  FileId file_id;

  MessageSticker() = default;
  explicit MessageSticker(FileId file_id) : file_id(file_id) {
  }

  static const int32 ID = 5;
  int32 get_id() const override {
    return ID;
  }
};

class MessageVideo : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageVideo() = default;
  MessageVideo(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  static const int32 ID = 6;
  int32 get_id() const override {
    return ID;
  }
};

class MessageVoiceNote : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;
  bool is_listened;

  MessageVoiceNote() = default;
  MessageVoiceNote(FileId file_id, FormattedText &&caption, bool is_listened)
      : file_id(file_id), caption(std::move(caption)), is_listened(is_listened) {
  }

  static const int32 ID = 7;
  int32 get_id() const override {
    return ID;
  }
};

class MessageContact : public MessageContent {
 public:
  Contact contact;

  MessageContact() = default;
  explicit MessageContact(Contact &&contact) : contact(std::move(contact)) {
  }

  static const int32 ID = 8;
  int32 get_id() const override {
    return ID;
  }
};

class MessageLocation : public MessageContent {
 public:
  Location location;

  MessageLocation() = default;
  explicit MessageLocation(Location &&location) : location(std::move(location)) {
  }

  static const int32 ID = 9;
  int32 get_id() const override {
    return ID;
  }
};

class MessageVenue : public MessageContent {
 public:
  Venue venue;

  MessageVenue() = default;
  explicit MessageVenue(Venue &&venue) : venue(std::move(venue)) {
  }

  static const int32 ID = 10;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatCreate : public MessageContent {
 public:
  string title;
  vector<UserId> participant_user_ids;

  MessageChatCreate() = default;
  MessageChatCreate(string &&title, vector<UserId> &&participant_user_ids)
      : title(std::move(title)), participant_user_ids(std::move(participant_user_ids)) {
  }

  static const int32 ID = 11;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatChangeTitle : public MessageContent {
 public:
  string title;

  MessageChatChangeTitle() = default;
  explicit MessageChatChangeTitle(string &&title) : title(std::move(title)) {
  }

  static const int32 ID = 12;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatChangePhoto : public MessageContent {
 public:
  Photo photo;

  MessageChatChangePhoto() = default;
  explicit MessageChatChangePhoto(Photo &&photo) : photo(std::move(photo)) {
  }

  static const int32 ID = 13;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatDeletePhoto : public MessageContent {
 public:
  static const int32 ID = 14;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatDeleteHistory : public MessageContent {
 public:
  static const int32 ID = 15;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatAddUsers : public MessageContent {
 public:
  vector<UserId> user_ids;

  MessageChatAddUsers() = default;
  explicit MessageChatAddUsers(vector<UserId> &&user_ids) : user_ids(std::move(user_ids)) {
  }

  static const int32 ID = 16;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatJoinedByLink : public MessageContent {
 public:
  static const int32 ID = 17;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatDeleteUser : public MessageContent {
 public:
  UserId user_id;

  MessageChatDeleteUser() = default;
  explicit MessageChatDeleteUser(UserId user_id) : user_id(user_id) {
  }

  static const int32 ID = 18;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatMigrateTo : public MessageContent {
 public:
  ChannelId migrated_to_channel_id;

  MessageChatMigrateTo() = default;
  explicit MessageChatMigrateTo(ChannelId migrated_to_channel_id) : migrated_to_channel_id(migrated_to_channel_id) {
  }

  static const int32 ID = 19;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChannelCreate : public MessageContent {
 public:
  string title;

  MessageChannelCreate() = default;
  explicit MessageChannelCreate(string &&title) : title(std::move(title)) {
  }

  static const int32 ID = 20;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChannelMigrateFrom : public MessageContent {
 public:
  string title;
  ChatId migrated_from_chat_id;

  MessageChannelMigrateFrom() = default;
  MessageChannelMigrateFrom(string &&title, ChatId migrated_from_chat_id)
      : title(std::move(title)), migrated_from_chat_id(migrated_from_chat_id) {
  }

  static const int32 ID = 21;
  int32 get_id() const override {
    return ID;
  }
};

class MessagePinMessage : public MessageContent {
 public:
  MessageId message_id;

  MessagePinMessage() = default;
  explicit MessagePinMessage(MessageId message_id) : message_id(message_id) {
  }

  static const int32 ID = 22;
  int32 get_id() const override {
    return ID;
  }
};

class MessageGame : public MessageContent {
 public:
  Game game;

  MessageGame() = default;
  explicit MessageGame(Game &&game) : game(std::move(game)) {
  }

  static const int32 ID = 23;
  int32 get_id() const override {
    return ID;
  }
};

class MessageGameScore : public MessageContent {
 public:
  MessageId game_message_id;
  int64 game_id;
  int32 score;

  MessageGameScore() = default;
  MessageGameScore(MessageId game_message_id, int64 game_id, int32 score)
      : game_message_id(game_message_id), game_id(game_id), score(score) {
  }

  static const int32 ID = 24;
  int32 get_id() const override {
    return ID;
  }
};

class MessageScreenshotTaken : public MessageContent {
 public:
  static const int32 ID = 25;
  int32 get_id() const override {
    return ID;
  }
};

class MessageChatSetTtl : public MessageContent {
 public:
  int32 ttl;

  MessageChatSetTtl() = default;
  explicit MessageChatSetTtl(int32 ttl) : ttl(ttl) {
  }

  static const int32 ID = 26;
  int32 get_id() const override {
    return ID;
  }
};

class MessageUnsupported
    : public MessageContent {  // TODO save a layer in which the message was received to
                               // automatically reget it if the layer changes
 public:
  static const int32 ID = 27;
  int32 get_id() const override {
    return ID;
  }
};

class MessageCall : public MessageContent {
 public:
  int64 call_id;
  int32 duration;
  CallDiscardReason discard_reason;

  MessageCall() = default;
  MessageCall(int64 call_id, int32 duration, CallDiscardReason discard_reason)
      : call_id(call_id), duration(duration), discard_reason(discard_reason) {
  }

  static const int32 ID = 28;
  int32 get_id() const override {
    return ID;
  }
};

class MessageInvoice : public MessageContent {
 public:
  string title;
  string description;
  Photo photo;
  string start_parameter;

  // InputMessageInvoice
  Invoice invoice;
  string payload;
  string provider_token;
  string provider_data;

  // MessageInvoice
  int64 total_amount = 0;
  MessageId receipt_message_id;

  MessageInvoice() = default;
  MessageInvoice(string &&title, string &&description, Photo &&photo, string &&start_parameter, int64 total_amount,
                 string &&currency, bool is_test, bool need_shipping_address, MessageId receipt_message_id)
      : title(std::move(title))
      , description(std::move(description))
      , photo(std::move(photo))
      , start_parameter(std::move(start_parameter))
      , invoice(std::move(currency), is_test, need_shipping_address)
      , payload()
      , provider_token()
      , provider_data()
      , total_amount(total_amount)
      , receipt_message_id(receipt_message_id) {
  }

  static const int32 ID = 29;
  int32 get_id() const override {
    return ID;
  }
};

class MessagePaymentSuccessful : public MessageContent {
 public:
  MessageId invoice_message_id;
  string currency;
  int64 total_amount = 0;

  // bots only part
  string invoice_payload;
  string shipping_option_id;
  unique_ptr<OrderInfo> order_info;
  string telegram_payment_charge_id;
  string provider_payment_charge_id;

  MessagePaymentSuccessful() = default;
  MessagePaymentSuccessful(MessageId invoice_message_id, string &&currency, int64 total_amount)
      : invoice_message_id(invoice_message_id), currency(std::move(currency)), total_amount(total_amount) {
  }

  static const int32 ID = 30;
  int32 get_id() const override {
    return ID;
  }
};

class MessageVideoNote : public MessageContent {
 public:
  FileId file_id;

  bool is_viewed = false;

  MessageVideoNote() = default;
  MessageVideoNote(FileId file_id, bool is_viewed) : file_id(file_id), is_viewed(is_viewed) {
  }

  static const int32 ID = 31;
  int32 get_id() const override {
    return ID;
  }
};

class MessageContactRegistered : public MessageContent {
 public:
  static const int32 ID = 32;
  int32 get_id() const override {
    return ID;
  }
};

class MessageExpiredPhoto : public MessageContent {
 public:
  MessageExpiredPhoto() = default;

  static const int32 ID = 33;
  int32 get_id() const override {
    return ID;
  }
};

class MessageExpiredVideo : public MessageContent {
 public:
  MessageExpiredVideo() = default;

  static const int32 ID = 34;
  int32 get_id() const override {
    return ID;
  }
};

class MessageLiveLocation : public MessageContent {
 public:
  Location location;
  int32 period;

  MessageLiveLocation() = default;
  MessageLiveLocation(Location &&location, int32 period) : location(std::move(location)), period(period) {
  }

  static const int32 ID = 35;
  int32 get_id() const override {
    return ID;
  }
};

class MessageCustomServiceAction : public MessageContent {
 public:
  string message;

  MessageCustomServiceAction() = default;
  explicit MessageCustomServiceAction(string &&message) : message(std::move(message)) {
  }

  static const int32 ID = 36;
  int32 get_id() const override {
    return ID;
  }
};

class MessageWebsiteConnected : public MessageContent {
 public:
  string domain_name;

  MessageWebsiteConnected() = default;
  explicit MessageWebsiteConnected(string &&domain_name) : domain_name(std::move(domain_name)) {
  }

  static const int32 ID = 37;
  int32 get_id() const override {
    return ID;
  }
};

class MessagePassportDataSent : public MessageContent {
 public:
  vector<SecureValueType> types;

  MessagePassportDataSent() = default;
  explicit MessagePassportDataSent(vector<SecureValueType> &&types) : types(std::move(types)) {
  }

  static const int32 ID = 38;
  int32 get_id() const override {
    return ID;
  }
};

class MessagePassportDataReceived : public MessageContent {
 public:
  vector<EncryptedSecureValue> values;
  EncryptedSecureCredentials credentials;

  MessagePassportDataReceived() = default;
  MessagePassportDataReceived(vector<EncryptedSecureValue> &&values, EncryptedSecureCredentials &&credentials)
      : values(std::move(values)), credentials(std::move(credentials)) {
  }

  static const int32 ID = 39;
  int32 get_id() const override {
    return ID;
  }
};

class InputMessageText {
 public:
  FormattedText text;
  bool disable_web_page_preview = false;
  bool clear_draft = false;
  InputMessageText() = default;
  InputMessageText(FormattedText text, bool disable_web_page_preview, bool clear_draft)
      : text(std::move(text)), disable_web_page_preview(disable_web_page_preview), clear_draft(clear_draft) {
  }
};

bool operator==(const InputMessageText &lhs, const InputMessageText &rhs);
bool operator!=(const InputMessageText &lhs, const InputMessageText &rhs);

class DraftMessage {
 public:
  int32 date;
  MessageId reply_to_message_id;
  InputMessageText input_message_text;
};

class DialogNotificationSettings {
 public:
  int32 mute_until = 0;
  string sound = "default";
  bool show_preview = true;
  bool silent_send_message = false;
  bool use_default_mute_until = true;
  bool use_default_sound = true;
  bool use_default_show_preview = true;
  bool is_use_default_fixed = true;
  bool is_synchronized = false;

  DialogNotificationSettings() = default;

  DialogNotificationSettings(bool use_default_mute_until, int32 mute_until, bool use_default_sound, string sound,
                             bool use_default_show_preview, bool show_preview, bool silent_send_message)
      : mute_until(mute_until)
      , sound(std::move(sound))
      , show_preview(show_preview)
      , silent_send_message(silent_send_message)
      , use_default_mute_until(use_default_mute_until)
      , use_default_sound(use_default_sound)
      , use_default_show_preview(use_default_show_preview)
      , is_synchronized(true) {
  }
};

enum class NotificationSettingsScope : int32 { Private, Group };

class ScopeNotificationSettings {
 public:
  int32 mute_until = 0;
  string sound = "default";
  bool show_preview = true;
  bool is_synchronized = false;

  ScopeNotificationSettings() = default;

  ScopeNotificationSettings(int32 mute_until, string sound, bool show_preview)
      : mute_until(mute_until), sound(std::move(sound)), show_preview(show_preview), is_synchronized(true) {
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, DialogNotificationSettings notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.silent_send_message
                        << ", " << notification_settings.use_default_mute_until << ", "
                        << notification_settings.use_default_sound << ", "
                        << notification_settings.use_default_show_preview << ", "
                        << notification_settings.is_synchronized << "]";
}

inline StringBuilder &operator<<(StringBuilder &string_builder, NotificationSettingsScope scope) {
  switch (scope) {
    case NotificationSettingsScope::Private:
      return string_builder << "notification settings for private chats";
    case NotificationSettingsScope::Group:
      return string_builder << "notification settings for group chats";
    default:
      UNREACHABLE();
      return string_builder;
  }
}

inline StringBuilder &operator<<(StringBuilder &string_builder, ScopeNotificationSettings notification_settings) {
  return string_builder << "[" << notification_settings.mute_until << ", " << notification_settings.sound << ", "
                        << notification_settings.show_preview << ", " << notification_settings.is_synchronized << "]";
}

inline constexpr size_t search_messages_filter_size() {
  return static_cast<int32>(SearchMessagesFilter::Size) - 1;
}

inline int32 search_messages_filter_index(SearchMessagesFilter filter) {
  CHECK(filter != SearchMessagesFilter::Empty);
  return static_cast<int32>(filter) - 1;
}

inline int32 search_messages_filter_index_mask(SearchMessagesFilter filter) {
  if (filter == SearchMessagesFilter::Empty) {
    return 0;
  }
  return 1 << search_messages_filter_index(filter);
}

inline int32 search_calls_filter_index(SearchMessagesFilter filter) {
  CHECK(filter == SearchMessagesFilter::Call || filter == SearchMessagesFilter::MissedCall);
  return static_cast<int32>(filter) - static_cast<int32>(SearchMessagesFilter::Call);
}

class DialogDate {
  int64 order;
  DialogId dialog_id;

 public:
  DialogDate(int64 order, DialogId dialog_id) : order(order), dialog_id(dialog_id) {
  }

  bool operator<(const DialogDate &other) const {
    return order > other.order || (order == other.order && dialog_id.get() > other.dialog_id.get());
  }

  bool operator<=(const DialogDate &other) const {
    return order >= other.order && (order != other.order || dialog_id.get() >= other.dialog_id.get());
  }

  bool operator==(const DialogDate &other) const {
    return order == other.order && dialog_id == other.dialog_id;
  }

  bool operator!=(const DialogDate &other) const {
    return order != other.order || dialog_id != other.dialog_id;
  }

  int64 get_order() const {
    return order;
  }
  DialogId get_dialog_id() const {
    return dialog_id;
  }
  int32 get_date() const {
    return static_cast<int32>((order >> 32) & 0x7FFFFFFF);
  }
  MessageId get_message_id() const {
    return MessageId(ServerMessageId(static_cast<int32>(order & 0x7FFFFFFF)));
  }

  friend struct DialogDateHash;

  friend StringBuilder &operator<<(StringBuilder &string_builder, DialogDate dialog_date);
};

const DialogDate MIN_DIALOG_DATE(std::numeric_limits<int64>::max(), DialogId());
const DialogDate MAX_DIALOG_DATE(0, DialogId());
const int64 DEFAULT_ORDER = -1;

struct DialogDateHash {
  std::size_t operator()(const DialogDate &dialog_date) const {
    return std::hash<int64>()(dialog_date.order) * 2023654985u + DialogIdHash()(dialog_date.dialog_id);
  }
};

inline StringBuilder &operator<<(StringBuilder &string_builder, DialogDate dialog_date) {
  return string_builder << "[" << dialog_date.order << ", " << dialog_date.dialog_id.get() << "]";
}

class dummyUpdate : public telegram_api::Update {
 public:
  static constexpr int32 ID = 1234567891;
  int32 get_id() const override {
    return ID;
  }

  void store(TlStorerUnsafe &s) const override {
    UNREACHABLE();
  }

  void store(TlStorerCalcLength &s) const override {
    UNREACHABLE();
  }

  void store(TlStorerToString &s, const char *field_name) const override;
};

class Dependencies {
 public:
  std::unordered_set<UserId, UserIdHash> user_ids;
  std::unordered_set<ChatId, ChatIdHash> chat_ids;
  std::unordered_set<ChannelId, ChannelIdHash> channel_ids;
  std::unordered_set<SecretChatId, SecretChatIdHash> secret_chat_ids;
  std::unordered_set<DialogId, DialogIdHash> dialog_ids;
  std::unordered_set<WebPageId, WebPageIdHash> web_page_ids;
};

struct CallsDbState {
  std::array<MessageId, 2> first_calls_database_message_id_by_index;
  std::array<int32, 2> message_count_by_index;
};

class MessagesManager : public Actor {
 public:
  //  static constexpr int32 MESSAGE_FLAG_IS_UNREAD = 1 << 0;
  static constexpr int32 MESSAGE_FLAG_IS_OUT = 1 << 1;
  static constexpr int32 MESSAGE_FLAG_IS_FORWARDED = 1 << 2;
  static constexpr int32 MESSAGE_FLAG_IS_REPLY = 1 << 3;
  static constexpr int32 MESSAGE_FLAG_HAS_MENTION = 1 << 4;
  static constexpr int32 MESSAGE_FLAG_HAS_UNREAD_CONTENT = 1 << 5;
  static constexpr int32 MESSAGE_FLAG_HAS_REPLY_MARKUP = 1 << 6;
  static constexpr int32 MESSAGE_FLAG_HAS_ENTITIES = 1 << 7;
  static constexpr int32 MESSAGE_FLAG_HAS_FROM_ID = 1 << 8;
  static constexpr int32 MESSAGE_FLAG_HAS_MEDIA = 1 << 9;
  static constexpr int32 MESSAGE_FLAG_HAS_VIEWS = 1 << 10;
  static constexpr int32 MESSAGE_FLAG_IS_SENT_VIA_BOT = 1 << 11;
  static constexpr int32 MESSAGE_FLAG_IS_SILENT = 1 << 13;
  static constexpr int32 MESSAGE_FLAG_IS_POST = 1 << 14;
  static constexpr int32 MESSAGE_FLAG_HAS_EDIT_DATE = 1 << 15;
  static constexpr int32 MESSAGE_FLAG_HAS_AUTHOR_SIGNATURE = 1 << 16;
  static constexpr int32 MESSAGE_FLAG_HAS_MEDIA_ALBUM_ID = 1 << 17;

  static constexpr int32 MESSAGE_FORWARD_HEADER_FLAG_HAS_AUTHOR_ID = 1 << 0;
  static constexpr int32 MESSAGE_FORWARD_HEADER_FLAG_HAS_CHANNEL_ID = 1 << 1;
  static constexpr int32 MESSAGE_FORWARD_HEADER_FLAG_HAS_MESSAGE_ID = 1 << 2;
  static constexpr int32 MESSAGE_FORWARD_HEADER_FLAG_HAS_AUTHOR_SIGNATURE = 1 << 3;
  static constexpr int32 MESSAGE_FORWARD_HEADER_FLAG_HAS_SAVED_FROM = 1 << 4;

  static constexpr int32 SEND_MESSAGE_FLAG_IS_REPLY = 1 << 0;
  static constexpr int32 SEND_MESSAGE_FLAG_DISABLE_WEB_PAGE_PREVIEW = 1 << 1;
  static constexpr int32 SEND_MESSAGE_FLAG_HAS_REPLY_MARKUP = 1 << 2;
  static constexpr int32 SEND_MESSAGE_FLAG_HAS_ENTITIES = 1 << 3;
  //  static constexpr int32 SEND_MESSAGE_FLAG_IS_POST = 1 << 4;
  static constexpr int32 SEND_MESSAGE_FLAG_DISABLE_NOTIFICATION = 1 << 5;
  static constexpr int32 SEND_MESSAGE_FLAG_FROM_BACKGROUND = 1 << 6;
  static constexpr int32 SEND_MESSAGE_FLAG_CLEAR_DRAFT = 1 << 7;
  static constexpr int32 SEND_MESSAGE_FLAG_WITH_MY_SCORE = 1 << 8;
  static constexpr int32 SEND_MESSAGE_FLAG_GROUP_MEDIA = 1 << 9;
  static constexpr int32 SEND_MESSAGE_FLAG_HAS_MESSAGE = 1 << 11;

  MessagesManager(Td *td, ActorShared<> parent);
  MessagesManager(const MessagesManager &) = delete;
  MessagesManager &operator=(const MessagesManager &) = delete;
  MessagesManager(MessagesManager &&) = delete;
  MessagesManager &operator=(MessagesManager &&) = delete;
  ~MessagesManager() override;

  static vector<MessageId> get_message_ids(const vector<int64> &input_message_ids);

  static vector<int32> get_server_message_ids(const vector<MessageId> &message_ids);

  static tl_object_ptr<telegram_api::InputMessage> get_input_message(MessageId message_id);

  static MessageId get_message_id(const tl_object_ptr<telegram_api::Message> &message_ptr);

  DialogId get_message_dialog_id(const tl_object_ptr<telegram_api::Message> &message_ptr) const;

  FullMessageId get_full_message_id(const tl_object_ptr<telegram_api::Message> &message_ptr) const;

  static int32 get_message_date(const tl_object_ptr<telegram_api::Message> &message_ptr);

  tl_object_ptr<telegram_api::InputPeer> get_input_peer(DialogId dialog_id, AccessRights access_rights) const;

  vector<tl_object_ptr<telegram_api::InputPeer>> get_input_peers(const vector<DialogId> &dialog_ids,
                                                                 AccessRights access_rights) const;

  tl_object_ptr<telegram_api::inputDialogPeer> get_input_dialog_peer(DialogId dialog_id,
                                                                     AccessRights access_rights) const;

  vector<tl_object_ptr<telegram_api::inputDialogPeer>> get_input_dialog_peers(const vector<DialogId> &dialog_ids,
                                                                              AccessRights access_rights) const;

  tl_object_ptr<telegram_api::inputEncryptedChat> get_input_encrypted_chat(DialogId dialog_id,
                                                                           AccessRights access_rights) const;

  bool have_input_peer(DialogId dialog_id, AccessRights access_rights) const;

  void on_get_messages(vector<tl_object_ptr<telegram_api::Message>> &&messages, bool is_channel_message,
                       const char *source);

  void on_get_history(DialogId dialog_id, MessageId from_message_id, int32 offset, int32 limit, bool from_the_end,
                      vector<tl_object_ptr<telegram_api::Message>> &&messages);

  void on_get_public_dialogs_search_result(const string &query, vector<tl_object_ptr<telegram_api::Peer>> &&my_peers,
                                           vector<tl_object_ptr<telegram_api::Peer>> &&peers);
  void on_failed_public_dialogs_search(const string &query, Status &&error);

  void on_get_dialog_messages_search_result(DialogId dialog_id, const string &query, UserId sender_user_id,
                                            MessageId from_message_id, int32 offset, int32 limit,
                                            SearchMessagesFilter filter, int64 random_id, int32 total_count,
                                            vector<tl_object_ptr<telegram_api::Message>> &&messages);
  void on_failed_dialog_messages_search(DialogId dialog_id, int64 random_id);

  void on_get_messages_search_result(const string &query, int32 offset_date, DialogId offset_dialog_id,
                                     MessageId offset_message_id, int32 limit, int64 random_id, int32 total_count,
                                     vector<tl_object_ptr<telegram_api::Message>> &&messages);
  void on_failed_messages_search(int64 random_id);

  void on_get_recent_locations(DialogId dialog_id, int32 limit, int64 random_id, int32 total_count,
                               vector<tl_object_ptr<telegram_api::Message>> &&messages);
  void on_get_recent_locations_failed(int64 random_id);

  // if message is from_update, flags have_previous and have_next are ignored and should be both true
  FullMessageId on_get_message(tl_object_ptr<telegram_api::Message> message_ptr, bool from_update,
                               bool is_channel_message, bool have_previous, bool have_next, const char *source);

  void open_secret_message(SecretChatId secret_chat_id, int64 random_id, Promise<>);

  void on_send_secret_message_success(int64 random_id, MessageId message_id, int32 date,
                                      tl_object_ptr<telegram_api::EncryptedFile> file_ptr, Promise<> promise);
  void on_send_secret_message_error(int64 random_id, Status error, Promise<> promise);

  void delete_secret_messages(SecretChatId secret_chat_id, std::vector<int64> random_ids, Promise<> promise);

  void delete_secret_chat_history(SecretChatId secret_chat_id, MessageId last_message_id, Promise<> promise);

  void read_secret_chat_outbox(SecretChatId secret_chat_id, int32 up_to_date, int32 read_date);

  void on_get_secret_message(SecretChatId secret_chat_id, UserId user_id, MessageId message_id, int32 date,
                             tl_object_ptr<telegram_api::encryptedFile> file,
                             tl_object_ptr<secret_api::decryptedMessage> message, Promise<> promise);

  void on_secret_chat_screenshot_taken(SecretChatId secret_chat_id, UserId user_id, MessageId message_id, int32 date,
                                       int64 random_id, Promise<> promise);

  void on_secret_chat_ttl_changed(SecretChatId secret_chat_id, UserId user_id, MessageId message_id, int32 date,
                                  int32 ttl, int64 random_id, Promise<> promise);

  void on_update_sent_text_message(int64 random_id, tl_object_ptr<telegram_api::MessageMedia> message_media,
                                   vector<tl_object_ptr<telegram_api::MessageEntity>> &&entities);

  void on_update_message_web_page(FullMessageId full_message_id, bool have_web_page);

  void on_get_dialogs(vector<tl_object_ptr<telegram_api::dialog>> &&dialogs, int32 total_count,
                      vector<tl_object_ptr<telegram_api::Message>> &&messages, Promise<Unit> &&promise);

  void on_get_common_dialogs(UserId user_id, vector<tl_object_ptr<telegram_api::Chat>> &&chats, int32 total_count);

  bool on_update_message_id(int64 random_id, MessageId new_message_id, const string &source);

  void on_update_dialog_draft_message(DialogId dialog_id, tl_object_ptr<telegram_api::DraftMessage> &&draft_message);

  void on_update_dialog_is_pinned(DialogId dialog_id, bool is_pinned);

  void on_update_pinned_dialogs();

  void on_update_dialog_is_marked_as_unread(DialogId dialog_id, bool is_marked_as_unread);

  void on_update_service_notification(tl_object_ptr<telegram_api::updateServiceNotification> &&update);

  void on_update_contact_registered(tl_object_ptr<telegram_api::updateContactRegistered> &&update);

  void on_update_new_channel_message(tl_object_ptr<telegram_api::updateNewChannelMessage> &&update);

  void on_update_edit_channel_message(tl_object_ptr<telegram_api::updateEditChannelMessage> &&update);

  void on_update_read_channel_inbox(tl_object_ptr<telegram_api::updateReadChannelInbox> &&update);

  void on_update_read_channel_outbox(tl_object_ptr<telegram_api::updateReadChannelOutbox> &&update);

  void on_update_read_channel_messages_contents(
      tl_object_ptr<telegram_api::updateChannelReadMessagesContents> &&update);

  void on_update_channel_too_long(tl_object_ptr<telegram_api::updateChannelTooLong> &&update, bool force_apply);

  void on_update_message_views(FullMessageId full_message_id, int32 views);

  void on_read_channel_inbox(ChannelId channel_id, MessageId max_message_id, int32 server_unread_count);

  void on_read_channel_outbox(ChannelId channel_id, MessageId max_message_id);

  void on_update_channel_max_unavailable_message_id(ChannelId channel_id, MessageId max_unavailable_message_id);

  void on_user_dialog_action(DialogId dialog_id, UserId user_id, tl_object_ptr<td_api::ChatAction> &&action,
                             int32 message_content_id = -1);

  void delete_messages(DialogId dialog_id, const vector<MessageId> &message_ids, bool revoke, Promise<Unit> &&promise);

  void delete_dialog_history(DialogId dialog_id, bool remove_from_dialog_list, Promise<Unit> &&promise);

  void delete_dialog_messages_from_user(DialogId dialog_id, UserId user_id, Promise<Unit> &&promise);

  void delete_dialog(DialogId dialog_id);

  void read_all_dialog_mentions(DialogId dialog_id, Promise<Unit> &&promise);

  Status add_recently_found_dialog(DialogId dialog_id) TD_WARN_UNUSED_RESULT;

  Status remove_recently_found_dialog(DialogId dialog_id) TD_WARN_UNUSED_RESULT;

  void clear_recently_found_dialogs();

  DialogId resolve_dialog_username(const string &username);

  DialogId search_public_dialog(const string &username_to_search, bool force, Promise<Unit> &&promise);

  Result<FormattedText> process_input_caption(DialogId dialog_id, tl_object_ptr<td_api::formattedText> &&text,
                                              bool is_bot) const;

  Result<InputMessageText> process_input_message_text(
      DialogId dialog_id, tl_object_ptr<td_api::InputMessageContent> &&input_message_content, bool is_bot,
      bool for_draft = false) const TD_WARN_UNUSED_RESULT;

  static Result<std::pair<Location, int32>> process_input_message_location(
      tl_object_ptr<td_api::InputMessageContent> &&input_message_content) TD_WARN_UNUSED_RESULT;

  static Result<Venue> process_input_message_venue(tl_object_ptr<td_api::InputMessageContent> &&input_message_content)
      TD_WARN_UNUSED_RESULT;

  static Result<Contact> process_input_message_contact(
      tl_object_ptr<td_api::InputMessageContent> &&input_message_content) TD_WARN_UNUSED_RESULT;

  Result<Game> process_input_message_game(tl_object_ptr<td_api::InputMessageContent> &&input_message_content) const
      TD_WARN_UNUSED_RESULT;

  bool need_skip_bot_commands(DialogId dialog_id, bool is_bot) const;

  FormattedText get_message_text(string message_text,
                                 vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities, int32 send_date,
                                 const char *source) const;

  Result<MessageId> send_message(DialogId dialog_id, MessageId reply_to_message_id, bool disable_notification,
                                 bool from_background, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                 tl_object_ptr<td_api::InputMessageContent> &&input_message_content)
      TD_WARN_UNUSED_RESULT;

  Result<vector<MessageId>> send_message_group(
      DialogId dialog_id, MessageId reply_to_message_id, bool disable_notification, bool from_background,
      vector<tl_object_ptr<td_api::InputMessageContent>> &&input_message_contents) TD_WARN_UNUSED_RESULT;

  Result<MessageId> send_bot_start_message(UserId bot_user_id, DialogId dialog_id,
                                           const string &parameter) TD_WARN_UNUSED_RESULT;

  Result<MessageId> send_inline_query_result_message(DialogId dialog_id, MessageId reply_to_message_id,
                                                     bool disable_notification, bool from_background, int64 query_id,
                                                     const string &result_id) TD_WARN_UNUSED_RESULT;

  Result<vector<MessageId>> forward_messages(DialogId to_dialog_id, DialogId from_dialog_id,
                                             vector<MessageId> message_ids, bool disable_notification,
                                             bool from_background, bool in_game_share,
                                             bool as_album) TD_WARN_UNUSED_RESULT;

  Result<MessageId> send_dialog_set_ttl_message(DialogId dialog_id, int32 ttl);

  Status send_screenshot_taken_notification_message(DialogId dialog_id);

  Result<MessageId> add_local_message(
      DialogId dialog_id, UserId sender_user_id, MessageId reply_to_message_id, bool disable_notification,
      tl_object_ptr<td_api::InputMessageContent> &&input_message_content) TD_WARN_UNUSED_RESULT;

  void edit_message_text(FullMessageId full_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                         tl_object_ptr<td_api::InputMessageContent> &&input_message_content, Promise<Unit> &&promise);

  void edit_message_live_location(FullMessageId full_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                  tl_object_ptr<td_api::location> &&input_location, Promise<Unit> &&promise);

  void edit_message_media(FullMessageId full_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                          tl_object_ptr<td_api::InputMessageContent> &&input_message_content, Promise<Unit> &&promise);

  void edit_message_caption(FullMessageId full_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                            tl_object_ptr<td_api::formattedText> &&input_caption, Promise<Unit> &&promise);

  void edit_message_reply_markup(FullMessageId full_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                 Promise<Unit> &&promise);

  void edit_inline_message_text(const string &inline_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
                                Promise<Unit> &&promise);

  void edit_inline_message_live_location(const string &inline_message_id,
                                         tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                         tl_object_ptr<td_api::location> &&input_location, Promise<Unit> &&promise);

  void edit_inline_message_media(const string &inline_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                 tl_object_ptr<td_api::InputMessageContent> &&input_message_content,
                                 Promise<Unit> &&promise);

  void edit_inline_message_caption(const string &inline_message_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup,
                                   tl_object_ptr<td_api::formattedText> &&input_caption, Promise<Unit> &&promise);

  void edit_inline_message_reply_markup(const string &inline_message_id,
                                        tl_object_ptr<td_api::ReplyMarkup> &&reply_markup, Promise<Unit> &&promise);

  void set_game_score(FullMessageId full_message_id, bool edit_message, UserId user_id, int32 score, bool force,
                      Promise<Unit> &&promise);

  void set_inline_game_score(const string &inline_message_id, bool edit_message, UserId user_id, int32 score,
                             bool force, Promise<Unit> &&promise);

  int64 get_game_high_scores(FullMessageId full_message_id, UserId user_id, Promise<Unit> &&promise);

  int64 get_inline_game_high_scores(const string &inline_message_id, UserId user_id, Promise<Unit> &&promise);

  void on_get_game_high_scores(int64 random_id, tl_object_ptr<telegram_api::messages_highScores> &&high_scores);

  tl_object_ptr<td_api::gameHighScores> get_game_high_scores_object(int64 random_id);

  void send_dialog_action(DialogId dialog_id, const tl_object_ptr<td_api::ChatAction> &action, Promise<Unit> &&promise);

  void set_dialog_photo(DialogId dialog_id, const tl_object_ptr<td_api::InputFile> &photo, Promise<Unit> &&promise);

  void set_dialog_title(DialogId dialog_id, const string &title, Promise<Unit> &&promise);

  void add_dialog_participant(DialogId dialog_id, UserId user_id, int32 forward_limit, Promise<Unit> &&promise);

  void add_dialog_participants(DialogId dialog_id, const vector<UserId> &user_ids, Promise<Unit> &&promise);

  void set_dialog_participant_status(DialogId dialog_id, UserId user_id,
                                     const tl_object_ptr<td_api::ChatMemberStatus> &chat_member_status,
                                     Promise<Unit> &&promise);

  DialogParticipant get_dialog_participant(DialogId dialog_id, UserId user_id, int64 &random_id, bool force,
                                           Promise<Unit> &&promise);

  std::pair<int32, vector<DialogParticipant>> search_dialog_participants(DialogId dialog_id, const string &query,
                                                                         int32 limit, DialogParticipantsFilter filter,
                                                                         int64 &random_id, bool force,
                                                                         Promise<Unit> &&promise);

  vector<UserId> get_dialog_administrators(DialogId dialog_id, int left_tries, Promise<Unit> &&promise);

  void export_dialog_invite_link(DialogId dialog_id, Promise<Unit> &&promise);

  string get_dialog_invite_link(DialogId dialog_id);

  int64 get_dialog_event_log(DialogId dialog_id, const string &query, int64 from_event_id, int32 limit,
                             const tl_object_ptr<td_api::chatEventLogFilters> &filters, const vector<UserId> &user_ids,
                             Promise<Unit> &&promise);

  void on_get_event_log(int64 random_id, tl_object_ptr<telegram_api::channels_adminLogResults> &&events);

  tl_object_ptr<td_api::chatEvents> get_chat_events_object(int64 random_id);

  bool have_dialog(DialogId dialog_id) const;
  bool have_dialog_force(DialogId dialog_id);

  bool load_dialog(DialogId dialog_id, int left_tries, Promise<Unit> &&promise);

  void load_dialogs(vector<DialogId> dialog_ids, Promise<Unit> &&promise);

  vector<DialogId> get_dialogs(DialogDate offset, int32 limit, bool force, Promise<Unit> &&promise);

  vector<DialogId> search_public_dialogs(const string &query, Promise<Unit> &&promise);

  std::pair<size_t, vector<DialogId>> search_dialogs(const string &query, int32 limit, Promise<Unit> &&promise);

  vector<DialogId> search_dialogs_on_server(const string &query, int32 limit, Promise<Unit> &&promise);

  vector<DialogId> get_common_dialogs(UserId user_id, DialogId offset_dialog_id, int32 limit, bool force,
                                      Promise<Unit> &&promise);

  bool have_message(FullMessageId full_message_id);

  void get_message(FullMessageId full_message_id, Promise<Unit> &&promise);

  MessageId get_replied_message(DialogId dialog_id, MessageId message_id, bool force, Promise<Unit> &&promise);

  void get_dialog_pinned_message(DialogId dialog_id, Promise<MessageId> &&promise);

  bool get_messages(DialogId dialog_id, const vector<MessageId> &message_ids, Promise<Unit> &&promise);

  void get_messages_from_server(vector<FullMessageId> &&message_ids, Promise<Unit> &&promise,
                                tl_object_ptr<telegram_api::InputMessage> input_message = nullptr);

  bool is_message_edited_recently(FullMessageId full_message_id, int32 seconds);

  std::pair<string, string> get_public_message_link(FullMessageId full_message_id, bool for_group,
                                                    Promise<Unit> &&promise);

  void on_get_public_message_link(FullMessageId full_message_id, bool for_group, string url, string html);

  Status delete_dialog_reply_markup(DialogId dialog_id, MessageId message_id) TD_WARN_UNUSED_RESULT;

  Status set_dialog_draft_message(DialogId dialog_id,
                                  tl_object_ptr<td_api::draftMessage> &&draft_message) TD_WARN_UNUSED_RESULT;

  void clear_all_draft_messages(bool exclude_secret_chats, Promise<Unit> &&promise);

  Status toggle_dialog_is_pinned(DialogId dialog_id, bool is_pinned) TD_WARN_UNUSED_RESULT;

  Status toggle_dialog_is_marked_as_unread(DialogId dialog_id, bool is_marked_as_unread) TD_WARN_UNUSED_RESULT;

  Status toggle_dialog_silent_send_message(DialogId dialog_id, bool silent_send_message) TD_WARN_UNUSED_RESULT;

  Status set_pinned_dialogs(vector<DialogId> dialog_ids) TD_WARN_UNUSED_RESULT;

  Status set_dialog_client_data(DialogId dialog_id, string &&client_data) TD_WARN_UNUSED_RESULT;

  void create_dialog(DialogId dialog_id, bool force, Promise<Unit> &&promise);

  DialogId create_new_group_chat(const vector<UserId> &user_ids, const string &title, int64 &random_id,
                                 Promise<Unit> &&promise);

  DialogId create_new_channel_chat(const string &title, bool is_megagroup, const string &description, int64 &random_id,
                                   Promise<Unit> &&promise);

  void create_new_secret_chat(UserId user_id, Promise<SecretChatId> &&promise);

  DialogId migrate_dialog_to_megagroup(DialogId dialog_id, Promise<Unit> &&promise);

  Status open_dialog(DialogId dialog_id) TD_WARN_UNUSED_RESULT;

  Status close_dialog(DialogId dialog_id) TD_WARN_UNUSED_RESULT;

  Status view_messages(DialogId dialog_id, const vector<MessageId> &message_ids, bool force_read) TD_WARN_UNUSED_RESULT;

  Status open_message_content(FullMessageId full_message_id) TD_WARN_UNUSED_RESULT;

  static tl_object_ptr<td_api::NotificationSettingsScope> get_notification_settings_scope_object(
      NotificationSettingsScope scope);

  static tl_object_ptr<td_api::chatNotificationSettings> get_chat_notification_settings_object(
      const DialogNotificationSettings *notification_settings);

  static tl_object_ptr<td_api::scopeNotificationSettings> get_scope_notification_settings_object(
      const ScopeNotificationSettings *notification_settings);

  const DialogNotificationSettings *get_dialog_notification_settings(DialogId dialog_id, Promise<Unit> &&promise);

  const ScopeNotificationSettings *get_scope_notification_settings(NotificationSettingsScope scope,
                                                                   Promise<Unit> &&promise);

  Status set_dialog_notification_settings(DialogId dialog_id,
                                          tl_object_ptr<td_api::chatNotificationSettings> &&notification_settings)
      TD_WARN_UNUSED_RESULT;

  Status set_scope_notification_settings(NotificationSettingsScope scope,
                                         tl_object_ptr<td_api::scopeNotificationSettings> &&notification_settings)
      TD_WARN_UNUSED_RESULT;

  void reset_all_notification_settings();

  tl_object_ptr<td_api::chat> get_chat_object(DialogId dialog_id);

  static tl_object_ptr<td_api::chats> get_chats_object(const vector<DialogId> &dialogs);

  tl_object_ptr<td_api::messages> get_dialog_history(DialogId dialog_id, MessageId from_message_id, int32 offset,
                                                     int32 limit, int left_tries, bool only_local,
                                                     Promise<Unit> &&promise);

  std::pair<int32, vector<MessageId>> search_dialog_messages(DialogId dialog_id, const string &query,
                                                             UserId sender_user_id, MessageId from_message_id,
                                                             int32 offset, int32 limit,
                                                             const tl_object_ptr<td_api::SearchMessagesFilter> &filter,
                                                             int64 &random_id, bool use_db, Promise<Unit> &&promise);

  std::pair<int64, vector<FullMessageId>> offline_search_messages(
      DialogId dialog_id, const string &query, int64 from_search_id, int32 limit,
      const tl_object_ptr<td_api::SearchMessagesFilter> &filter, int64 &random_id, Promise<> &&promise);

  std::pair<int32, vector<FullMessageId>> search_messages(const string &query, int32 offset_date,
                                                          DialogId offset_dialog_id, MessageId offset_message_id,
                                                          int32 limit, int64 &random_id, Promise<Unit> &&promise);

  std::pair<int32, vector<FullMessageId>> search_call_messages(MessageId from_message_id, int32 limit, bool only_missed,
                                                               int64 &random_id, bool use_db, Promise<Unit> &&promise);

  std::pair<int32, vector<MessageId>> search_dialog_recent_location_messages(DialogId dialog_id, int32 limit,
                                                                             int64 &random_id, Promise<Unit> &&promise);

  vector<FullMessageId> get_active_live_location_messages(Promise<Unit> &&promise);

  int64 get_dialog_message_by_date(DialogId dialog_id, int32 date, Promise<Unit> &&promise);

  void on_get_dialog_message_by_date_success(DialogId dialog_id, int32 date, int64 random_id,
                                             vector<tl_object_ptr<telegram_api::Message>> &&messages);

  void on_get_dialog_message_by_date_fail(int64 random_id);

  int32 get_dialog_message_count(DialogId dialog_id, const tl_object_ptr<td_api::SearchMessagesFilter> &filter,
                                 bool return_local, int64 &random_id, Promise<Unit> &&promise);

  tl_object_ptr<td_api::message> get_dialog_message_by_date_object(int64 random_id);

  tl_object_ptr<td_api::message> get_message_object(FullMessageId full_message_id);

  tl_object_ptr<td_api::messages> get_messages_object(int32 total_count, DialogId dialog_id,
                                                      const vector<MessageId> &message_ids);

  tl_object_ptr<td_api::messages> get_messages_object(int32 total_count, const vector<FullMessageId> &full_message_ids);

  void add_pending_update(tl_object_ptr<telegram_api::Update> &&update, int32 new_pts, int32 pts_count,
                          bool force_apply, const char *source);

  void add_pending_channel_update(DialogId dialog_id, tl_object_ptr<telegram_api::Update> &&update, int32 new_pts,
                                  int32 pts_count, const char *source, bool is_postponed_update = false);

  bool is_update_about_username_change_received(DialogId dialog_id) const;

  void on_dialog_photo_updated(DialogId dialog_id);
  void on_dialog_title_updated(DialogId dialog_id);
  void on_dialog_username_updated(DialogId dialog_id, const string &old_username, const string &new_username);

  void on_resolved_username(const string &username, DialogId dialog_id);
  void drop_username(const string &username);

  static tl_object_ptr<telegram_api::MessagesFilter> get_input_messages_filter(SearchMessagesFilter filter);

  static SearchMessagesFilter get_search_messages_filter(const tl_object_ptr<td_api::SearchMessagesFilter> &filter);

  tl_object_ptr<telegram_api::InputNotifyPeer> get_input_notify_peer(DialogId dialogId) const;

  static tl_object_ptr<telegram_api::InputNotifyPeer> get_input_notify_peer(NotificationSettingsScope scope);

  static NotificationSettingsScope get_notification_settings_scope(
      const tl_object_ptr<td_api::NotificationSettingsScope> &scope);

  void on_update_dialog_notify_settings(DialogId dialog_id,
                                        tl_object_ptr<telegram_api::peerNotifySettings> &&peer_notify_settings);

  void on_update_scope_notify_settings(NotificationSettingsScope scope,
                                       tl_object_ptr<telegram_api::peerNotifySettings> &&peer_notify_settings);

  bool get_dialog_report_spam_state(DialogId dialog_id, Promise<Unit> &&promise);

  void change_dialog_report_spam_state(DialogId dialog_id, bool is_spam_dialog, Promise<Unit> &&promise);

  void report_dialog(DialogId dialog_id, const tl_object_ptr<td_api::ChatReportReason> &reason,
                     const vector<MessageId> &message_ids, Promise<Unit> &&promise);

  void on_get_peer_settings(DialogId dialog_id, tl_object_ptr<telegram_api::peerSettings> &&peer_settings);

  void before_get_difference();

  void after_get_difference();

  bool on_get_dialog_error(DialogId dialog_id, const Status &status, const string &source);

  void on_send_message_get_quick_ack(int64 random_id);

  void check_send_message_result(int64 random_id, DialogId dialog_id, const telegram_api::Updates *updates_ptr,
                                 const char *source);

  FullMessageId on_send_message_success(int64 random_id, MessageId new_message_id, int32 date, FileId new_file_id,
                                        const char *source);

  void on_send_message_file_part_missing(int64 random_id, int bad_part);

  void on_send_message_fail(int64 random_id, Status error);

  void on_upload_message_media_success(DialogId dialog_id, MessageId message_id,
                                       tl_object_ptr<telegram_api::MessageMedia> &&media);

  void on_upload_message_media_file_part_missing(DialogId dialog_id, MessageId message_id, int bad_part);

  void on_upload_message_media_fail(DialogId dialog_id, MessageId message_id, Status error);

  void on_create_new_dialog_success(int64 random_id, tl_object_ptr<telegram_api::Updates> &&updates,
                                    DialogType expected_type, Promise<Unit> &&promise);

  void on_create_new_dialog_fail(int64 random_id, Status error, Promise<Unit> &&promise);

  void on_get_channel_difference(DialogId dialog_id, int32 request_pts, int32 request_limit,
                                 tl_object_ptr<telegram_api::updates_ChannelDifference> &&difference_ptr);

  void force_create_dialog(DialogId dialog_id, const char *source, bool force_update_dialog_pos = false);

  void send_get_dialog_notification_settings_query(DialogId dialog_id, Promise<Unit> &&promise);

  void on_get_dialog_notification_settings_query_finished(DialogId dialog_id, Status &&status);

  void on_get_dialog_query_finished(DialogId dialog_id, Status &&status);

  void on_get_sponsored_dialog_id(tl_object_ptr<telegram_api::Peer> peer,
                                  vector<tl_object_ptr<telegram_api::User>> users,
                                  vector<tl_object_ptr<telegram_api::Chat>> chats);

  void on_binlog_events(vector<BinlogEvent> &&events);

  void get_payment_form(FullMessageId full_message_id, Promise<tl_object_ptr<td_api::paymentForm>> &&promise);

  void validate_order_info(FullMessageId full_message_id, tl_object_ptr<td_api::orderInfo> order_info, bool allow_save,
                           Promise<tl_object_ptr<td_api::validatedOrderInfo>> &&promise);

  void send_payment_form(FullMessageId full_message_id, const string &order_info_id, const string &shipping_option_id,
                         const tl_object_ptr<td_api::InputCredentials> &credentials,
                         Promise<tl_object_ptr<td_api::paymentResult>> &&promise);

  void get_payment_receipt(FullMessageId full_message_id, Promise<tl_object_ptr<td_api::paymentReceipt>> &&promise);

  ActorOwn<MultiSequenceDispatcher> sequence_dispatcher_;

 private:
  class PendingPtsUpdate {
   public:
    tl_object_ptr<telegram_api::Update> update;
    int32 pts;
    int32 pts_count;

    PendingPtsUpdate(tl_object_ptr<telegram_api::Update> &&update, int32 pts, int32 pts_count)
        : update(std::move(update)), pts(pts), pts_count(pts_count) {
    }
  };

  struct MessageInfo {
    DialogId dialog_id;
    MessageId message_id;
    UserId sender_user_id;
    int32 date;
    int32 ttl = 0;
    int64 random_id = 0;
    tl_object_ptr<telegram_api::messageFwdHeader> forward_header;
    MessageId reply_to_message_id;
    UserId via_bot_user_id;
    int32 views = 0;
    int32 flags = 0;
    int32 edit_date = 0;
    string author_signature;
    int64 media_album_id = 0;

    unique_ptr<MessageContent> content;
    tl_object_ptr<telegram_api::ReplyMarkup> reply_markup;
  };

  struct MessageForwardInfo {
    UserId sender_user_id;
    int32 date = 0;
    DialogId dialog_id;
    MessageId message_id;
    string author_signature;
    DialogId from_dialog_id;
    MessageId from_message_id;

    MessageForwardInfo() = default;

    MessageForwardInfo(UserId sender_user_id, int32 date, DialogId dialog_id, MessageId message_id,
                       string author_signature, DialogId from_dialog_id, MessageId from_message_id)
        : sender_user_id(sender_user_id)
        , date(date)
        , dialog_id(dialog_id)
        , message_id(message_id)
        , author_signature(std::move(author_signature))
        , from_dialog_id(from_dialog_id)
        , from_message_id(from_message_id) {
    }

    bool operator==(const MessageForwardInfo &rhs) const {
      return sender_user_id == rhs.sender_user_id && date == rhs.date && dialog_id == rhs.dialog_id &&
             message_id == rhs.message_id && author_signature == rhs.author_signature &&
             from_dialog_id == rhs.from_dialog_id && from_message_id == rhs.from_message_id;
    }

    bool operator!=(const MessageForwardInfo &rhs) const {
      return !(*this == rhs);
    }

    friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageForwardInfo &forward_info) {
      return string_builder << "MessageForwardInfo[sender " << forward_info.sender_user_id << "("
                            << forward_info.author_signature << "), source " << forward_info.dialog_id << ", source "
                            << forward_info.message_id << ", from " << forward_info.from_dialog_id << ", from "
                            << forward_info.from_message_id << " at " << forward_info.date << "]";
    }
  };

  // Do not forget to update MessagesManager::update_message when this class is changed
  struct Message {
    int32 random_y;

    MessageId message_id;
    UserId sender_user_id;
    int32 date = 0;
    int32 edit_date = 0;

    int64 random_id = 0;

    unique_ptr<MessageForwardInfo> forward_info;

    MessageId reply_to_message_id;
    int64 reply_to_random_id = 0;  // for send_message

    UserId via_bot_user_id;

    string author_signature;

    bool is_channel_post = false;
    bool is_outgoing = false;
    bool is_failed_to_send = false;  // TODO replace with error_code
    bool disable_notification = false;
    bool contains_mention = false;
    bool contains_unread_mention = false;
    bool had_reply_markup = false;   // had non-inline reply markup?
    bool is_content_secret = false;  // should be shown only while tapped

    bool from_background = false;           // for send_message
    bool disable_web_page_preview = false;  // for send_message
    bool clear_draft = false;               // for send_message
    bool in_game_share = false;             // for send_message

    bool have_previous = false;
    bool have_next = false;
    bool from_database = false;

    int32 views = 0;

    int32 ttl = 0;
    double ttl_expires_at = 0;

    int64 media_album_id = 0;

    unique_ptr<MessageContent> content;

    unique_ptr<ReplyMarkup> reply_markup;

    unique_ptr<MessageContent> edited_content;
    unique_ptr<ReplyMarkup> edited_reply_markup;
    uint64 edit_generation = 0;
    Promise<Unit> edit_promise;

    unique_ptr<Message> left;
    unique_ptr<Message> right;

    int32 last_access_date = 0;

    uint64 send_message_logevent_id = 0;

    NetQueryRef send_query_ref;

    DialogId debug_forward_from;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Dialog {
    DialogId dialog_id;
    MessageId last_new_message_id;  // identifier of the last known server message received from update, there should be
                                    // no server messages after it
    MessageId last_message_id;      // identifier of the message after which currently there is no any message, i.e. a
                                    // message without a gap after it, memory only
    MessageId first_database_message_id;  // identifier of the first message in the database, needed
                                          // until there is no gaps in the database
    MessageId last_database_message_id;   // identifier of the last local or server message, if last_database_message_id
                                          // is known and last_message_id is known then last_database_message_id <=
                                          // last_message_id

    std::array<MessageId, search_messages_filter_size()> first_database_message_id_by_index;
    // use struct Count?
    std::array<int32, search_messages_filter_size()> message_count_by_index;

    int32 server_unread_count = 0;
    int32 local_unread_count = 0;
    int32 unread_mention_count = 0;
    MessageId last_read_inbox_message_id;
    int32 last_read_inbox_message_date = 0;  // secret chats only
    MessageId last_read_outbox_message_id;
    MessageId reply_markup_message_id;
    DialogNotificationSettings notification_settings;
    unique_ptr<DraftMessage> draft_message;
    uint64 save_draft_message_logevent_id = 0;
    uint64 save_draft_message_logevent_id_generation = 0;
    uint64 save_notification_settings_logevent_id = 0;
    uint64 save_notification_settings_logevent_id_generation = 0;
    uint64 read_history_logevent_id = 0;
    uint64 read_history_logevent_id_generation = 0;

    MessageId
        last_read_all_mentions_message_id;  // all mentions with a message id not greater than it are implicitly read
    MessageId
        max_unavailable_message_id;  // maximal unavailable message id for dialogs with cleared/unavailable history

    int32 last_clear_history_date = 0;
    MessageId last_clear_history_message_id;
    int64 order = DEFAULT_ORDER;
    int64 pinned_order = DEFAULT_ORDER;
    int32 delete_last_message_date = 0;
    MessageId deleted_last_message_id;

    MessageId max_added_message_id;

    bool has_contact_registered_message = false;

    bool is_last_message_deleted_locally = false;

    bool know_can_report_spam = false;
    bool can_report_spam = false;

    bool is_opened = false;

    bool need_restore_reply_markup = true;

    bool have_full_history = false;
    bool is_empty = false;

    bool is_last_read_inbox_message_id_inited = false;
    bool is_last_read_outbox_message_id_inited = false;
    bool need_repair_server_unread_count = false;
    bool is_marked_as_unread = false;

    bool increment_view_counter = false;

    bool is_update_new_chat_sent = false;

    int32 pts = 0;                                                     // for channels only
    std::multimap<int32, PendingPtsUpdate> postponed_channel_updates;  // for channels only
    int32 retry_get_difference_timeout = 1;                            // for channels only
    std::unordered_map<int64, MessageId> random_id_to_message_id;      // for secret chats only

    MessageId last_assigned_message_id;  // identifier of the last local or yet unsent message, assigned after
                                         // application start, used to guarantee that all assigned message identifiers
                                         // are different

    std::unordered_map<MessageId, MessageId, MessageIdHash> yet_unsent_message_id_to_persistent_message_id;

    std::unordered_set<MessageId, MessageIdHash> deleted_message_ids;

    std::vector<MessageId> pending_update_new_messages;

    string client_data;

    // Load from newest to oldest message
    MessageId suffix_load_first_message_id_;  // identifier of some message such all suffix messages in range
                                              // [suffix_load_first_message_id_, last_message_id] are loaded
    MessageId suffix_load_query_message_id_;
    std::vector<std::pair<Promise<>, std::function<bool(const Message *)>>> suffix_load_queries_;
    bool suffix_load_done_ = false;
    bool suffix_load_has_query_ = false;

    std::unordered_set<MessageId, MessageIdHash> pending_viewed_message_ids;

    unique_ptr<Message> messages = nullptr;

    struct MessageOp {
      enum : int8 { Add, SetPts, Delete, DeleteAll } type;
      bool from_update;
      bool have_previous;
      bool have_next;
      int32 content_type;
      MessageId message_id;
      const char *source;
      double date;

      MessageOp(decltype(type) type, MessageId message_id, int32 content_type, bool from_update, bool have_previous,
                bool have_next, const char *source)
          : type(type)
          , from_update(from_update)
          , have_previous(have_previous)
          , have_next(have_next)
          , content_type(content_type)
          , message_id(message_id)
          , source(source)
          , date(G()->server_time()) {
      }
    };

    const char *debug_set_dialog_last_database_message_id = "Unknown";  // to be removed soon
    vector<MessageOp> debug_message_op;

    // message identifiers loaded from database, to be removed soon
    MessageId debug_last_new_message_id;
    MessageId debug_first_database_message_id;
    MessageId debug_last_database_message_id;

    Dialog() = default;
    Dialog(const Dialog &) = delete;
    Dialog &operator=(const Dialog &) = delete;
    Dialog(Dialog &&other) = delete;
    Dialog &operator=(Dialog &&other) = delete;
    ~Dialog();

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  class MessagesIteratorBase {
    vector<const Message *> stack_;

   protected:
    MessagesIteratorBase() = default;

    // points iterator to message with greatest id which is less or equal than message_id
    MessagesIteratorBase(const Message *root, MessageId message_id) {
      size_t last_right_pos = 0;
      while (root != nullptr) {
        //        LOG(DEBUG) << "root->message_id = " << root->message_id;
        stack_.push_back(root);
        if (root->message_id.get() <= message_id.get()) {
          //          LOG(DEBUG) << "Go right";
          last_right_pos = stack_.size();
          root = root->right.get();
        } else {
          //          LOG(DEBUG) << "Go left";
          root = root->left.get();
        }
      }
      stack_.resize(last_right_pos);
    }

    const Message *operator*() const {
      return stack_.empty() ? nullptr : stack_.back();
    }

    ~MessagesIteratorBase() = default;

   public:
    MessagesIteratorBase(const MessagesIteratorBase &) = delete;
    MessagesIteratorBase &operator=(const MessagesIteratorBase &) = delete;
    MessagesIteratorBase(MessagesIteratorBase &&other) = default;
    MessagesIteratorBase &operator=(MessagesIteratorBase &&other) = default;

    void operator++() {
      if (stack_.empty()) {
        return;
      }

      const Message *cur = stack_.back();
      if (!cur->have_next) {
        stack_.clear();
        return;
      }
      if (cur->right == nullptr) {
        while (true) {
          stack_.pop_back();
          if (stack_.empty()) {
            return;
          }
          const Message *new_cur = stack_.back();
          if (new_cur->left.get() == cur) {
            return;
          }
          cur = new_cur;
        }
      }

      cur = cur->right.get();
      while (cur != nullptr) {
        stack_.push_back(cur);
        cur = cur->left.get();
      }
    }

    void operator--() {
      if (stack_.empty()) {
        return;
      }

      const Message *cur = stack_.back();
      if (!cur->have_previous) {
        stack_.clear();
        return;
      }
      if (cur->left == nullptr) {
        while (true) {
          stack_.pop_back();
          if (stack_.empty()) {
            return;
          }
          const Message *new_cur = stack_.back();
          if (new_cur->right.get() == cur) {
            return;
          }
          cur = new_cur;
        }
      }

      cur = cur->left.get();
      while (cur != nullptr) {
        stack_.push_back(cur);
        cur = cur->right.get();
      }
    }
  };

  class MessagesIterator : public MessagesIteratorBase {
   public:
    MessagesIterator() = default;

    MessagesIterator(Dialog *d, MessageId message_id) : MessagesIteratorBase(d->messages.get(), message_id) {
    }

    Message *operator*() const {
      return const_cast<Message *>(MessagesIteratorBase::operator*());
    }
  };

  class MessagesConstIterator : public MessagesIteratorBase {
   public:
    MessagesConstIterator() = default;

    MessagesConstIterator(const Dialog *d, MessageId message_id) : MessagesIteratorBase(d->messages.get(), message_id) {
    }

    const Message *operator*() const {
      return MessagesIteratorBase::operator*();
    }
  };

  struct PendingSecretMessage {
    MessageInfo message_info;
    MultiPromiseActor load_data_multipromise;
    Promise<> success_promise;
  };

  struct InputMessageContent {
    unique_ptr<MessageContent> content;
    bool disable_web_page_preview = false;
    bool clear_draft = false;
    int32 ttl = 0;
    UserId via_bot_user_id;

    InputMessageContent(unique_ptr<MessageContent> &&content, bool disable_web_page_preview, bool clear_draft,
                        int32 ttl, UserId via_bot_user_id)
        : content(std::move(content))
        , disable_web_page_preview(disable_web_page_preview)
        , clear_draft(clear_draft)
        , ttl(ttl)
        , via_bot_user_id(via_bot_user_id) {
    }
  };

  class DeleteAllChannelMessagesFromUserOnServerLogEvent;
  class DeleteDialogHistoryFromServerLogEvent;
  class DeleteMessageLogEvent;
  class DeleteMessagesFromServerLogEvent;
  class ForwardMessagesLogEvent;
  class GetChannelDifferenceLogEvent;
  class ReadAllDialogMentionsOnServerLogEvent;
  class ReadHistoryOnServerLogEvent;
  class ReadHistoryInSecretChatLogEvent;
  class ReadMessageContentsOnServerLogEvent;
  class ReorderPinnedDialogsOnServerLogEvent;
  class SaveDialogDraftMessageOnServerLogEvent;
  class UpdateDialogNotificationSettingsOnServerLogEvent;
  class UpdateScopeNotificationSettingsOnServerLogEvent;
  class ResetAllNotificationSettingsOnServerLogEvent;
  class ChangeDialogReportSpamStateOnServerLogEvent;
  class SendBotStartMessageLogEvent;
  class SendInlineQueryResultMessageLogEvent;
  class SendMessageLogEvent;
  class SendScreenshotTakenNotificationMessageLogEvent;
  class ToggleDialogIsPinnedOnServerLogEvent;
  class ToggleDialogIsMarkedAsUnreadOnServerLogEvent;
  class GetDialogFromServerLogEvent;

  static constexpr size_t MAX_GROUPED_MESSAGES = 10;               // server side limit
  static constexpr int32 MAX_GET_DIALOGS = 100;                    // server side limit
  static constexpr int32 MAX_GET_HISTORY = 100;                    // server side limit
  static constexpr int32 MAX_SEARCH_MESSAGES = 100;                // server side limit
  static constexpr int32 MIN_SEARCH_PUBLIC_DIALOG_PREFIX_LEN = 5;  // server side limit
  static constexpr int32 MIN_CHANNEL_DIFFERENCE = 10;
  static constexpr int32 MAX_CHANNEL_DIFFERENCE = 100;
  static constexpr int32 MAX_BOT_CHANNEL_DIFFERENCE = 100000;  // server side limit
  static constexpr int32 MAX_RECENT_FOUND_DIALOGS = 20;        // some reasonable value
  static constexpr size_t MAX_NAME_LENGTH = 255;               // server side limit for title and description
  static constexpr int64 SPONSORED_DIALOG_ORDER = static_cast<int64>(2147483647) << 32;
  static constexpr int32 MIN_PINNED_DIALOG_DATE = 2147000000;  // some big date
  static constexpr int32 MAX_PRIVATE_MESSAGE_TTL = 60;         // server side limit

  static constexpr int32 UPDATE_CHANNEL_TO_LONG_FLAG_HAS_PTS = 1 << 0;

  static constexpr int32 CHANNEL_DIFFERENCE_FLAG_IS_FINAL = 1 << 0;
  static constexpr int32 CHANNEL_DIFFERENCE_FLAG_HAS_TIMEOUT = 1 << 1;

  static constexpr int32 DIALOG_FLAG_HAS_PTS = 1 << 0;
  static constexpr int32 DIALOG_FLAG_HAS_DRAFT = 1 << 1;
  static constexpr int32 DIALOG_FLAG_IS_PINNED = 1 << 2;

  static constexpr int32 MAX_MESSAGE_VIEW_DELAY = 1;  // seconds
  static constexpr int32 MIN_SAVE_DRAFT_DELAY = 1;    // seconds
  static constexpr int32 MIN_READ_HISTORY_DELAY = 3;  // seconds
  static constexpr int32 MAX_SAVE_DIALOG_DELAY = 0;   // seconds
  static constexpr int32 DIALOG_UNLOAD_DELAY = 60;    // seconds

  static constexpr int32 USERNAME_CACHE_EXPIRE_TIME = 3 * 86400;
  static constexpr int32 USERNAME_CACHE_EXPIRE_TIME_SHORT = 900;

  static constexpr int32 MIN_LIVE_LOCATION_PERIOD = 60;     // seconds, server side limit
  static constexpr int32 MAX_LIVE_LOCATION_PERIOD = 86400;  // seconds, server side limit

  static constexpr int32 MAX_PRELOADED_DIALOGS = 1000;

  static constexpr double DIALOG_ACTION_TIMEOUT = 5.5;

  static constexpr const char *DELETE_MESSAGE_USER_REQUEST_SOURCE = "user request";

  static constexpr bool DROP_UPDATES = false;

  static bool is_dialog_inited(const Dialog *d);

  int32 get_dialog_mute_until(const Dialog *d) const;

  bool is_dialog_muted(const Dialog *d) const;

  void open_dialog(Dialog *d);

  void close_dialog(Dialog *d);

  void add_secret_message(unique_ptr<PendingSecretMessage> pending_secret_message, Promise<Unit> lock_promise = Auto());

  void finish_add_secret_message(unique_ptr<PendingSecretMessage> pending_secret_message);

  void fix_message_info_dialog_id(MessageInfo &message_info) const;

  MessageInfo parse_telegram_api_message(tl_object_ptr<telegram_api::Message> message_ptr, const char *source) const;

  std::pair<DialogId, unique_ptr<Message>> create_message(MessageInfo &&message_info, bool is_channel_message);

  FullMessageId on_get_message(MessageInfo &&message_info, bool from_update, bool is_channel_message,
                               bool have_previous, bool have_next, const char *source);

  Result<InputMessageContent> process_input_message_content(
      DialogId dialog_id, tl_object_ptr<td_api::InputMessageContent> &&input_message_content) const;

  Message *get_message_to_send(Dialog *d, MessageId reply_to_message_id, bool disable_notification,
                               bool from_background, unique_ptr<MessageContent> &&content, bool *need_update_dialog_pos,
                               unique_ptr<MessageForwardInfo> forward_info = nullptr);

  int64 begin_send_message(DialogId dialog_id, const Message *m);

  Status can_send_message(DialogId dialog_id) const TD_WARN_UNUSED_RESULT;

  Status can_send_message_content(DialogId dialog_id, const MessageContent *content,
                                  bool is_forward) const TD_WARN_UNUSED_RESULT;

  bool can_edit_message(DialogId dialog_id, const Message *m, bool is_editing, bool only_reply_markup = false) const;

  bool can_report_dialog(DialogId dialog_id) const;

  void cancel_edit_message_media(DialogId dialog_id, Message *m);

  void on_message_media_edited(DialogId dialog_id, MessageId message_id, FileId file_id, FileId thumbnail_file_id,
                               uint64 generation, Result<Unit> &&result);

  MessageId get_persistent_message_id(const Dialog *d, MessageId message_id) const;

  static MessageId get_replied_message_id(const Message *m);

  MessageId get_reply_to_message_id(Dialog *d, MessageId message_id);

  bool can_set_game_score(DialogId dialog_id, const Message *m) const;

  bool check_update_dialog_id(const tl_object_ptr<telegram_api::Update> &update, DialogId dialog_id);

  void process_update(tl_object_ptr<telegram_api::Update> &&update);

  void process_channel_update(tl_object_ptr<telegram_api::Update> &&update);

  void on_message_edited(FullMessageId full_message_id);

  void delete_messages_from_updates(const vector<MessageId> &message_ids);

  void delete_dialog_messages_from_updates(DialogId dialog_id, const vector<MessageId> &message_ids);

  void do_forward_messages(DialogId to_dialog_id, DialogId from_dialog_id, const vector<Message *> &messages,
                           const vector<MessageId> &message_ids, int64 logevent_id);

  Result<MessageId> forward_message(DialogId to_dialog_id, DialogId from_dialog_id, MessageId message_id,
                                    bool disable_notification, bool from_background,
                                    bool in_game_share) TD_WARN_UNUSED_RESULT;

  SecretInputMedia get_secret_input_media(const MessageContent *content,
                                          tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                          BufferSlice thumbnail, int32 layer);

  tl_object_ptr<telegram_api::invoice> get_input_invoice(const Invoice &invoice) const;

  tl_object_ptr<telegram_api::inputWebDocument> get_input_web_document(const Photo &photo) const;

  tl_object_ptr<telegram_api::inputMediaInvoice> get_input_media_invoice(const MessageInvoice *message_invoice) const;

  tl_object_ptr<telegram_api::InputMedia> get_input_media(const MessageContent *content,
                                                          tl_object_ptr<telegram_api::InputFile> input_file,
                                                          tl_object_ptr<telegram_api::InputFile> input_thumbnail,
                                                          int32 ttl);

  void delete_message_content_thumbnail(MessageContent *content);

  void do_send_media(DialogId dialog_id, Message *m, FileId file_id, FileId thumbnail_file_id,
                     tl_object_ptr<telegram_api::InputFile> input_file,
                     tl_object_ptr<telegram_api::InputFile> input_thumbnail);

  void do_send_secret_media(DialogId dialog_id, Message *m, FileId file_id, FileId thumbnail_file_id,
                            tl_object_ptr<telegram_api::InputEncryptedFile> input_encrypted_file,
                            BufferSlice thumbnail);

  void do_send_message(DialogId dialog_id, Message *m, vector<int> bad_parts = {});

  void on_message_media_uploaded(DialogId dialog_id, Message *m, tl_object_ptr<telegram_api::InputMedia> &&input_media,
                                 FileId file_id, FileId thumbnail_file_id);

  void on_secret_message_media_uploaded(DialogId dialog_id, Message *m, SecretInputMedia &&secret_input_media,
                                        FileId file_id, FileId thumbnail_file_id);

  void on_upload_message_media_finished(int64 media_album_id, DialogId dialog_id, MessageId message_id, Status result);

  void do_send_message_group(int64 media_album_id);

  void on_media_message_ready_to_send(DialogId dialog_id, MessageId message_id, Promise<Message *> &&promise);

  void on_yet_unsent_media_queue_updated(DialogId dialog_id);

  void save_send_bot_start_message_logevent(UserId bot_user_id, DialogId dialog_id, const string &parameter,
                                            Message *m);

  void do_send_bot_start_message(UserId bot_user_id, DialogId dialog_id, const string &parameter, Message *m);

  void save_send_inline_query_result_message_logevent(DialogId dialog_id, Message *m, int64 query_id,
                                                      const string &result_id);

  void do_send_inline_query_result_message(DialogId dialog_id, Message *m, int64 query_id, const string &result_id);

  uint64 save_send_screenshot_taken_notification_message_logevent(DialogId dialog_id, const Message *m);

  void do_send_screenshot_taken_notification_message(DialogId dialog_id, const Message *m, uint64 logevent_id);

  Message *continue_send_message(DialogId dialog_id, unique_ptr<Message> &&m, uint64 logevent_id);

  tl_object_ptr<telegram_api::InputChatPhoto> get_input_chat_photo(FileId file_id) const;

  bool is_message_unload_enabled() const;

  static bool is_allowed_media_group_content(int32 content_type);

  static bool can_forward_message(DialogId from_dialog_id, const Message *m);

  static bool is_secret_message_content(int32 ttl, int32 content_type);

  static bool is_service_message_content(int32 content_type);

  static bool can_have_message_content_caption(int32 content_type);

  static bool can_delete_channel_message(DialogParticipantStatus status, const Message *m, bool is_bot);

  bool can_revoke_message(DialogId dialog_id, const Message *m) const;

  bool can_unload_message(const Dialog *d, const Message *m) const;

  void unload_message(Dialog *d, MessageId message_id);

  unique_ptr<Message> delete_message(Dialog *d, MessageId message_id, bool is_permanently_deleted,
                                     bool *need_update_dialog_pos, const char *source);

  unique_ptr<Message> do_delete_message(Dialog *d, MessageId message_id, bool is_permanently_deleted,
                                        bool only_from_memory, bool *need_update_dialog_pos, const char *source);

  void unload_dialog(DialogId dialog_id);

  void delete_all_dialog_messages(Dialog *d, bool remove_from_dialog_list, bool is_permanent);

  void do_delete_all_dialog_messages(Dialog *d, unique_ptr<Message> &m, vector<int64> &deleted_message_ids);

  void delete_messages_from_server(DialogId dialog_id, vector<MessageId> message_ids, bool revoke, uint64 logevent_id,
                                   Promise<Unit> &&promise);

  void delete_dialog_history_from_server(DialogId dialog_id, MessageId max_message_id, bool remove_from_dialog_list,
                                         bool allow_error, uint64 logevent_id, Promise<Unit> &&promise);

  void delete_all_channel_messages_from_user_on_server(ChannelId channel_id, UserId user_id, uint64 logevent_id,
                                                       Promise<Unit> &&promise);

  void read_all_dialog_mentions_on_server(DialogId dialog_id, uint64 logevent_id, Promise<Unit> &&promise);

  static MessageId find_message_by_date(const unique_ptr<Message> &m, int32 date);

  static void find_messages_from_user(const unique_ptr<Message> &m, UserId user_id, vector<MessageId> &message_ids);

  static void find_unread_mentions(const unique_ptr<Message> &m, vector<MessageId> &message_ids);

  static void find_old_messages(const unique_ptr<Message> &m, MessageId max_message_id, vector<MessageId> &message_ids);

  void find_unloadable_messages(const Dialog *d, int32 unload_before_date, const unique_ptr<Message> &m,
                                vector<MessageId> &message_ids, int32 &left_to_unload) const;

  bool update_message_views(DialogId dialog_id, Message *m, int32 views);

  bool update_message_contains_unread_mention(Dialog *d, Message *m, bool contains_unread_mention, const char *source);

  static bool update_opened_message_content(Message *m);

  void read_message_content_from_updates(MessageId message_id);

  void read_channel_message_content_from_updates(Dialog *d, MessageId message_id);

  bool read_message_content(Dialog *d, Message *m, bool is_local_read, const char *source);

  void read_message_contents_on_server(DialogId dialog_id, vector<MessageId> message_ids, uint64 logevent_id);

  static int32 calc_new_unread_count_from_last_unread(Dialog *d, MessageId max_message_id, MessageType type);

  static int32 calc_new_unread_count_from_the_end(Dialog *d, MessageId max_message_id, MessageType type,
                                                  int32 hint_unread_count);

  static int32 calc_new_unread_count(Dialog *d, MessageId max_message_id, MessageType type, int32 hint_unread_count);

  void repair_server_unread_count(DialogId dialog_id, int32 unread_count);

  void repair_channel_server_unread_count(Dialog *d);

  void read_history_inbox(DialogId dialog_id, MessageId max_message_id, int32 unread_count, const char *source);

  void read_history_outbox(DialogId dialog_id, MessageId max_message_id, int32 read_date = -1);

  void read_history_on_server(Dialog *d, MessageId max_message_id);

  void read_history_on_server_impl(DialogId dialog_id, MessageId max_message_id);

  void on_read_history_finished(DialogId dialog_id, uint64 generation);

  void read_secret_chat_outbox_inner(DialogId dialog_id, int32 up_to_date, int32 read_date);

  void set_dialog_max_unavailable_message_id(DialogId dialog_id, MessageId max_unavailable_message_id, bool from_update,
                                             const char *source);

  void preload_newer_messages(const Dialog *d, MessageId max_message_id);

  void preload_older_messages(const Dialog *d, MessageId min_message_id);

  void on_get_history_from_database(DialogId dialog_id, MessageId from_message_id, int32 offset, int32 limit,
                                    bool from_the_end, bool only_local, vector<BufferSlice> &&messages,
                                    Promise<Unit> &&promise);

  void get_history_from_the_end(DialogId dialog_id, bool from_database, bool only_local, Promise<Unit> &&promise);

  void get_history(DialogId dialog_id, MessageId from_message_id, int32 offset, int32 limit, bool from_database,
                   bool only_local, Promise<Unit> &&promise);

  void load_messages(DialogId dialog_id, MessageId from_message_id, int32 offset, int32 limit, int left_tries,
                     bool only_local, Promise<Unit> &&promise);

  static int32 get_random_y(MessageId message_id);

  bool is_allowed_useless_update(const tl_object_ptr<telegram_api::Update> &update) const;

  bool is_message_auto_read(DialogId dialog_id, bool is_outgoing) const;

  void fail_send_message(FullMessageId full_message_id, int error_code, const string &error_message);

  void fail_edit_message_media(FullMessageId full_message_id, Status &&error);

  void on_dialog_updated(DialogId dialog_id, const char *source);

  BufferSlice get_dialog_database_value(const Dialog *d);

  void save_dialog_to_database(DialogId dialog_id);

  void on_save_dialog_to_database(DialogId dialog_id, bool success);

  void load_dialog_list(Promise<Unit> &&promise);

  void load_dialog_list_from_database(int32 limit, Promise<Unit> &&promise);

  static void preload_dialog_list(void *messages_manager_void);

  void update_message_count_by_index(Dialog *d, int diff, const Message *m);

  void update_message_count_by_index(Dialog *d, int diff, int32 index_mask);

  int32 get_message_index_mask(DialogId dialog_id, const Message *m) const;

  int32 get_message_content_index_mask(const MessageContent *content, bool is_secret, bool is_outgoing) const;

  Message *add_message_to_dialog(DialogId dialog_id, unique_ptr<Message> message, bool from_update, bool *need_update,
                                 bool *need_update_dialog_pos, const char *source);

  Message *add_message_to_dialog(Dialog *d, unique_ptr<Message> message, bool from_update, bool *need_update,
                                 bool *need_update_dialog_pos, const char *source);

  void on_message_changed(const Dialog *d, const Message *m, const char *source);

  void add_message_to_database(const Dialog *d, const Message *m, const char *source);

  void delete_all_dialog_messages_from_database(DialogId dialog_id, MessageId message_id, const char *source);

  void delete_message_from_database(Dialog *d, MessageId message_id, const Message *m,
                                    bool is_permanently_deleted) const;

  void delete_message_files(const Message *m) const;

  void do_delete_message_logevent(const DeleteMessageLogEvent &logevent) const;

  void attach_message_to_previous(Dialog *d, MessageId message_id, const char *source);

  void attach_message_to_next(Dialog *d, MessageId message_id, const char *source);

  void update_message(Dialog *d, unique_ptr<Message> &old_message, unique_ptr<Message> new_message,
                      bool need_send_update_message_content, bool *need_update_dialog_pos);

  static bool need_message_text_changed_warning(const Message *old_message, const MessageText *old_content,
                                                const MessageText *new_content);

  static int64 choose_location_access_hash(const Location &first, const Location &second);

  bool update_message_content(DialogId dialog_id, Message *old_message, unique_ptr<MessageContent> new_content,
                              bool need_send_update_message_content, bool need_merge_files);

  void send_update_new_message(Dialog *d, const Message *m, bool force = false);

  void flush_pending_update_new_messages(DialogId dialog_id);

  void send_update_message_send_succeeded(Dialog *d, MessageId old_message_id, const Message *m) const;

  void send_update_message_content(DialogId dialog_id, MessageId message_id, const MessageContent *content,
                                   int32 message_date, bool is_content_secret, const char *source) const;

  void send_update_message_edited(DialogId dialog_id, const Message *m);

  void send_update_delete_messages(DialogId dialog_id, vector<int64> &&message_ids, bool is_permanent,
                                   bool from_cache) const;

  void send_update_new_chat(Dialog *d);

  void send_update_chat_draft_message(const Dialog *d);

  void send_update_chat_last_message(Dialog *d, const char *source);

  void send_update_chat_last_message_impl(const Dialog *d, const char *source) const;

  void send_update_unread_message_count(DialogId dialog_id, bool force, const char *source);

  void send_update_unread_chat_count(DialogId dialog_id, bool force, const char *source);

  void send_update_chat_read_inbox(const Dialog *d, bool force, const char *source);

  void send_update_chat_read_outbox(const Dialog *d);

  void send_update_chat_unread_mention_count(const Dialog *d);

  void send_update_chat_is_sponsored(const Dialog *d) const;

  tl_object_ptr<td_api::message> get_message_object(DialogId dialog_id, const Message *message) const;

  static tl_object_ptr<td_api::messages> get_messages_object(int32 total_count,
                                                             vector<tl_object_ptr<td_api::message>> &&messages);

  vector<DialogId> sort_dialogs_by_order(const vector<DialogId> &dialog_ids, int32 limit) const;

  vector<DialogId> get_peers_dialog_ids(vector<tl_object_ptr<telegram_api::Peer>> &&peers);

  void recalc_unread_count();

  void set_dialog_last_read_inbox_message_id(Dialog *d, MessageId message_id, int32 server_unread_count,
                                             int32 local_unread_count, bool force_update, const char *source);

  void set_dialog_last_read_outbox_message_id(Dialog *d, MessageId message_id);

  void set_dialog_last_message_id(Dialog *d, MessageId last_message_id, const char *source);

  void set_dialog_first_database_message_id(Dialog *d, MessageId first_database_message_id, const char *source);

  void set_dialog_last_database_message_id(Dialog *d, MessageId last_database_message_id, const char *source);

  void set_dialog_last_new_message_id(Dialog *d, MessageId last_new_message_id, const char *source);

  void set_dialog_last_clear_history_date(Dialog *d, int32 date, MessageId last_clear_history_message_id,
                                          const char *source);

  void set_dialog_is_empty(Dialog *d, const char *source);

  static int32 get_pinned_dialogs_limit();

  static vector<DialogId> remove_secret_chat_dialog_ids(vector<DialogId> dialog_ids);

  void set_dialog_is_pinned(DialogId dialog_id, bool is_pinned);

  void set_dialog_is_pinned(Dialog *d, bool is_pinned);

  void set_dialog_is_marked_as_unread(Dialog *d, bool is_marked_as_unread);

  void toggle_dialog_is_pinned_on_server(DialogId dialog_id, bool is_pinned, uint64 logevent_id);

  void toggle_dialog_is_marked_as_unread_on_server(DialogId dialog_id, bool is_marked_as_unread, uint64 logevent_id);

  void reorder_pinned_dialogs_on_server(const vector<DialogId> &dialog_ids, uint64 logevent_id);

  void set_dialog_reply_markup(Dialog *d, MessageId message_id);

  void try_restore_dialog_reply_markup(Dialog *d, const Message *m);

  static string get_notification_settings_scope_database_key(NotificationSettingsScope scope);

  bool update_dialog_notification_settings(DialogId dialog_id, DialogNotificationSettings *current_settings,
                                           const DialogNotificationSettings &new_settings);

  bool update_scope_notification_settings(NotificationSettingsScope scope, ScopeNotificationSettings *current_settings,
                                          const ScopeNotificationSettings &new_settings);

  void update_dialog_unmute_timeout(Dialog *d, bool old_use_default, int32 old_mute_until, bool new_use_default,
                                    int32 new_mute_until);

  void update_scope_unmute_timeout(NotificationSettingsScope scope, int32 old_mute_until, int32 new_mute_until);

  void on_dialog_unmute(DialogId dialog_id);

  void on_scope_unmute(NotificationSettingsScope scope);

  bool update_dialog_silent_send_message(Dialog *d, bool silent_send_message);

  void on_send_dialog_action_timeout(DialogId dialog_id);

  void on_active_dialog_action_timeout(DialogId dialog_id);

  static bool need_cancel_user_dialog_action(int32 action_id, int32 message_content_id);

  void cancel_user_dialog_action(DialogId dialog_id, const Message *m);

  Dialog *get_dialog_by_message_id(MessageId message_id);

  MessageId get_message_id_by_random_id(Dialog *d, int64 random_id, const char *source);

  Dialog *add_dialog(DialogId dialog_id);

  Dialog *add_new_dialog(unique_ptr<Dialog> &&d, bool is_loaded_from_database);

  void fix_new_dialog(Dialog *d, unique_ptr<Message> &&last_database_message, MessageId last_database_message_id,
                      int64 order, int32 last_clear_history_date, MessageId last_clear_history_message_id);

  void add_dialog_last_database_message(Dialog *d, unique_ptr<Message> &&last_database_message);

  tl_object_ptr<td_api::inputMessageText> get_input_message_text_object(
      const InputMessageText &input_message_text) const;

  tl_object_ptr<td_api::draftMessage> get_draft_message_object(const unique_ptr<DraftMessage> &draft_message) const;

  tl_object_ptr<td_api::ChatType> get_chat_type_object(DialogId dialog_id) const;

  tl_object_ptr<td_api::chat> get_chat_object(const Dialog *d);

  bool have_dialog_info(DialogId dialog_id) const;
  bool have_dialog_info_force(DialogId dialog_id) const;

  Dialog *get_dialog(DialogId dialog_id);
  const Dialog *get_dialog(DialogId dialog_id) const;

  Dialog *get_dialog_force(DialogId dialog_id);

  Dialog *on_load_dialog_from_database(const Result<BufferSlice> &r_value);

  void on_get_dialogs_from_database(vector<BufferSlice> &&dialogs, Promise<Unit> &&promise);

  void send_get_dialog_query(DialogId dialog_id, Promise<Unit> &&promise, uint64 logevent_id = 0);

  void send_search_public_dialogs_query(const string &query, Promise<Unit> &&promise);

  vector<DialogId> get_pinned_dialogs() const;

  void update_dialogs_hints(const Dialog *d);
  void update_dialogs_hints_rating(const Dialog *d);

  std::pair<int32, vector<DialogParticipant>> search_private_chat_participants(UserId my_user_id, UserId peer_user_id,
                                                                               const string &query, int32 limit,
                                                                               DialogParticipantsFilter filter) const;

  static unique_ptr<Message> *find_message(unique_ptr<Message> *v, MessageId message_id);
  static const unique_ptr<Message> *find_message(const unique_ptr<Message> *v, MessageId message_id);

  static Message *get_message(Dialog *d, MessageId message_id);
  static const Message *get_message(const Dialog *d, MessageId message_id);

  Message *get_message(FullMessageId full_message_id);
  const Message *get_message(FullMessageId full_message_id) const;

  Message *get_message_force(Dialog *d, MessageId message_id);

  Message *get_message_force(FullMessageId full_message_id);

  void get_message_force_from_server(Dialog *d, MessageId message_id, Promise<Unit> &&promise,
                                     tl_object_ptr<telegram_api::InputMessage> input_message = nullptr);

  Message *on_get_message_from_database(DialogId dialog_id, Dialog *d, const BufferSlice &value);

  void get_dialog_message_by_date_from_server(const Dialog *d, int32 date, int64 random_id, bool after_database_search,
                                              Promise<Unit> &&promise);

  void on_get_dialog_message_by_date_from_database(DialogId dialog_id, int32 date, int64 random_id,
                                                   Result<BufferSlice> result, Promise<Unit> promise);

  static DialogNotificationSettings get_dialog_notification_settings(
      tl_object_ptr<telegram_api::peerNotifySettings> &&settings);

  static ScopeNotificationSettings get_scope_notification_settings(
      tl_object_ptr<telegram_api::peerNotifySettings> &&settings);

  std::pair<bool, int32> get_dialog_mute_until(DialogId dialog_id, const Dialog *d) const;

  static NotificationSettingsScope get_dialog_notification_setting_scope(DialogId dialog_id);

  int32 get_scope_mute_until(DialogId dialog_id) const;

  DialogNotificationSettings *get_dialog_notification_settings(DialogId dialog_id, bool force);

  ScopeNotificationSettings *get_scope_notification_settings(NotificationSettingsScope scope);

  static unique_ptr<DraftMessage> get_draft_message(ContactsManager *contacts_manager,
                                                    tl_object_ptr<telegram_api::DraftMessage> &&draft_message_ptr);

  static FormattedText get_secret_media_caption(string &&message_text, string &&message_caption);

  Photo get_web_document_photo(tl_object_ptr<telegram_api::WebDocument> web_document, DialogId owner_dialog_id) const;

  unique_ptr<MessageContent> get_secret_message_document(
      tl_object_ptr<telegram_api::encryptedFile> file,
      tl_object_ptr<secret_api::decryptedMessageMediaDocument> &&document,
      vector<tl_object_ptr<telegram_api::DocumentAttribute>> &&attributes, DialogId owner_dialog_id,
      FormattedText &&caption, bool is_opened) const;

  unique_ptr<MessageContent> get_message_document(tl_object_ptr<telegram_api::document> &&document,
                                                  DialogId owner_dialog_id, FormattedText &&caption, bool is_opened,
                                                  MultiPromiseActor *load_data_multipromise_ptr) const;

  unique_ptr<MessageContent> get_message_document(std::pair<DocumentsManager::DocumentType, FileId> &&parsed_document,
                                                  FormattedText &&caption, bool is_opened) const;

  unique_ptr<MessagePhoto> get_message_photo(tl_object_ptr<telegram_api::photo> &&photo, DialogId owner_dialog_id,
                                             FormattedText &&caption) const;

  unique_ptr<MessageContent> get_secret_message_content(
      string message_text, tl_object_ptr<telegram_api::encryptedFile> file,
      tl_object_ptr<secret_api::DecryptedMessageMedia> &&media,
      vector<tl_object_ptr<secret_api::MessageEntity>> &&secret_entities, DialogId owner_dialog_id,
      MultiPromiseActor &load_data_multipromise) const;

  unique_ptr<MessageContent> get_message_content(FormattedText message_text,
                                                 tl_object_ptr<telegram_api::MessageMedia> &&media,
                                                 DialogId owner_dialog_id, bool is_content_read, UserId via_bot_user_id,
                                                 int32 *ttl) const;

  unique_ptr<MessageContent> dup_message_content(DialogId dialog_id, const MessageContent *content, bool for_forward);

  unique_ptr<MessageContent> get_message_action_content(tl_object_ptr<telegram_api::MessageAction> &&action,
                                                        DialogId owner_dialog_id, MessageId reply_to_message_id) const;

  tl_object_ptr<td_api::MessageContent> get_message_content_object(const MessageContent *content, int32 message_date,
                                                                   bool is_content_secret) const;

  static FormattedText get_message_content_text(const MessageContent *content);

  static FormattedText get_message_content_caption(const MessageContent *content);

  int32 get_message_content_duration(const MessageContent *content) const;

  static FileId get_message_content_file_id(const MessageContent *content);

  static void update_message_content_file_id_remote(MessageContent *content, FileId file_id);

  FileId get_message_content_thumbnail_file_id(const MessageContent *content) const;

  vector<FileId> get_message_file_ids(const Message *message) const;

  void cancel_upload_message_content_files(const MessageContent *content);

  void cancel_send_message_query(DialogId dialog_id, unique_ptr<Message> &m);

  static int32 get_message_flags(const Message *m);

  unique_ptr<MessageForwardInfo> get_message_forward_info(
      tl_object_ptr<telegram_api::messageFwdHeader> &&forward_header);

  tl_object_ptr<td_api::MessageForwardInfo> get_message_forward_info_object(
      const unique_ptr<MessageForwardInfo> &forward_info) const;

  void ttl_read_history(Dialog *d, bool is_outgoing, MessageId from_message_id, MessageId till_message_id,
                        double view_date);
  void ttl_read_history_impl(DialogId dialog_id, bool is_outgoing, MessageId from_message_id, MessageId till_message_id,
                             double view_date);
  void ttl_on_view(const Dialog *d, Message *message, double view_date, double now);
  bool ttl_on_open(Dialog *d, Message *message, double now, bool is_local_read);
  void ttl_register_message(DialogId dialog_id, const Message *message, double now);
  void ttl_unregister_message(DialogId dialog_id, const Message *message, double now, const char *source);
  void ttl_loop(double now);
  void ttl_update_timeout(double now);

  void on_message_ttl_expired(Dialog *d, Message *message);
  void on_message_ttl_expired_impl(Dialog *d, Message *message);

  void start_up() override;
  void loop() override;
  void tear_down() override;

  void ttl_db_loop_start(double server_now);
  void ttl_db_loop(double server_now);
  void ttl_db_on_result(Result<std::pair<std::vector<std::pair<DialogId, BufferSlice>>, int32>> r_result, bool dummy);

  static MessageId get_first_database_message_id_by_index(const Dialog *d, SearchMessagesFilter filter);

  void on_search_dialog_messages_db_result(int64 random_id, DialogId dialog_id, MessageId from_message_id,
                                           MessageId first_db_message_id, SearchMessagesFilter filter_type,
                                           int32 offset, int32 limit, Result<MessagesDbMessagesResult> result,
                                           Promise<> promise);

  void on_messages_db_fts_result(Result<MessagesDbFtsResult> result, int64 random_id, Promise<> &&promise);

  void on_messages_db_calls_result(Result<MessagesDbCallsResult> result, int64 random_id, MessageId first_db_message_id,
                                   SearchMessagesFilter filter, Promise<> &&promise);

  void on_load_active_live_location_full_message_ids_from_database(string value);

  void on_load_active_live_location_messages_finished();

  void try_add_active_live_location(DialogId dialog_id, const Message *m);

  void add_active_live_location(FullMessageId full_message_id);

  bool delete_active_live_location(DialogId dialog_id, const Message *m);

  void save_active_live_locations();

  Result<unique_ptr<ReplyMarkup>> get_dialog_reply_markup(
      DialogId dialog_id, tl_object_ptr<td_api::ReplyMarkup> &&reply_markup_ptr) const TD_WARN_UNUSED_RESULT;

  const DialogPhoto *get_dialog_photo(DialogId dialog_id) const;

  string get_dialog_title(DialogId dialog_id) const;

  string get_dialog_username(DialogId dialog_id) const;

  static int64 get_dialog_order(MessageId message_id, int32 message_date);

  bool update_dialog_draft_message(Dialog *d, unique_ptr<DraftMessage> &&draft_message, bool from_update,
                                   bool need_update_dialog_pos);

  void save_dialog_draft_message_on_server(DialogId dialog_id);

  void on_saved_dialog_draft_message(DialogId dialog_id, uint64 generation);

  void update_dialog_notification_settings_on_server(DialogId dialog_id, bool from_binlog);

  void on_updated_dialog_notification_settings(DialogId dialog_id, uint64 generation);

  void update_scope_notification_settings_on_server(NotificationSettingsScope scope, uint64 logevent_id);

  void reset_all_notification_settings_on_server(uint64 logevent_id);

  void change_dialog_report_spam_state_on_server(DialogId dialog_id, bool is_spam_dialog, uint64 logevent_id,
                                                 Promise<Unit> &&promise);

  int64 get_next_pinned_dialog_order();

  void update_dialog_pos(Dialog *d, bool remove_from_dialog_list, const char *source,
                         bool need_send_update_chat_order = true);

  bool set_dialog_order(Dialog *d, int64 new_order, bool need_send_update_chat_order);

  void update_last_dialog_date();

  void load_notification_settings();

  void set_get_difference_timeout(double timeout);

  void skip_old_pending_update(tl_object_ptr<telegram_api::Update> &&update, int32 new_pts, int32 old_pts,
                               int32 pts_count, const char *source);

  void process_pending_updates();

  void drop_pending_updates();

  static string get_channel_pts_key(DialogId dialog_id);

  int32 load_channel_pts(DialogId dialog_id) const;

  void set_channel_pts(Dialog *d, int32 new_pts, const char *source) const;

  bool running_get_channel_difference(DialogId dialog_id) const;

  void get_channel_difference(DialogId dialog_id, int32 pts, bool force, const char *source);

  void do_get_channel_difference(DialogId dialog_id, int32 pts, bool force,
                                 tl_object_ptr<telegram_api::InputChannel> &&input_channel, const char *source);

  void process_get_channel_difference_updates(DialogId dialog_id,
                                              vector<tl_object_ptr<telegram_api::Message>> &&new_messages,
                                              vector<tl_object_ptr<telegram_api::Update>> &&other_updates);

  void on_get_channel_dialog(DialogId dialog_id, MessageId last_message_id, MessageId read_inbox_max_message_id,
                             int32 server_unread_count, int32 unread_mention_count,
                             MessageId read_outbox_max_message_id,
                             vector<tl_object_ptr<telegram_api::Message>> &&messages);

  void after_get_channel_difference(DialogId dialog_id, bool success);

  static void on_channel_get_difference_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_pending_message_views_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_pending_draft_message_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_pending_read_history_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_pending_updated_dialog_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_pending_unload_dialog_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_dialog_unmute_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_pending_send_dialog_action_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  static void on_active_dialog_action_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int);

  void load_secret_thumbnail(FileId thumbnail_file_id);

  static tl_object_ptr<telegram_api::channelAdminLogEventsFilter> get_channel_admin_log_events_filter(
      const tl_object_ptr<td_api::chatEventLogFilters> &filters);

  tl_object_ptr<td_api::ChatEventAction> get_chat_event_action_object(
      tl_object_ptr<telegram_api::ChannelAdminLogEventAction> &&action_ptr);

  void on_upload_media(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file,
                       tl_object_ptr<telegram_api::InputEncryptedFile> input_encrypted_file);
  void on_upload_media_error(FileId file_id, Status status);

  void on_load_secret_thumbnail(FileId thumbnail_file_id, BufferSlice thumbnail);
  void on_upload_thumbnail(FileId thumbnail_file_id, tl_object_ptr<telegram_api::InputFile> thumbnail_input_file);

  void on_upload_dialog_photo(FileId file_id, tl_object_ptr<telegram_api::InputFile> input_file);
  void on_upload_dialog_photo_error(FileId file_id, Status status);

  void set_sponsored_dialog_id(DialogId dialog_id);

  static uint64 get_sequence_dispatcher_id(DialogId dialog_id, int32 message_content_type);

  Dialog *get_service_notifications_dialog();

  static MessageId get_next_message_id(Dialog *d, int32 type);

  static MessageId get_next_local_message_id(Dialog *d);

  static MessageId get_next_yet_unsent_message_id(Dialog *d);

  bool add_recently_found_dialog_internal(DialogId dialog_id);

  bool remove_recently_found_dialog_internal(DialogId dialog_id);

  void save_recently_found_dialogs();
  bool load_recently_found_dialogs(Promise<Unit> &promise);

  void update_used_hashtags(DialogId dialog_id, const Message *m);

  string get_search_text(const Message *m);

  unique_ptr<Dialog> parse_dialog(DialogId dialog_id, const BufferSlice &value);

  void load_calls_db_state();
  void save_calls_db_state();

  static constexpr bool is_debug_message_op_enabled() {
    return !LOG_IS_STRIPPED(ERROR) && false;
  }

  static void dump_debug_message_op(const Dialog *d, int priority = 0);

  static void add_formatted_text_dependencies(Dependencies &dependencies, const FormattedText &text);

  static void add_message_dependencies(Dependencies &dependencies, DialogId dialog_id, const Message *m);

  static void add_dialog_dependencies(Dependencies &dependencies, DialogId dialog_id);

  void resolve_dependencies_force(const Dependencies &dependencies);

  void save_send_message_logevent(DialogId dialog_id, Message *m);

  void suffix_load_loop(Dialog *d);
  void suffix_load_update_first_message_id(Dialog *d);
  void suffix_load_query_ready(DialogId dialog_id);
  void suffix_load_add_query(Dialog *d, std::pair<Promise<>, std::function<bool(const Message *)>> query);
  void suffix_load_till_date(Dialog *d, int32 date, Promise<> promise);
  void suffix_load_till_message_id(Dialog *d, MessageId message_id, Promise<> promise);

  Result<ServerMessageId> get_invoice_message_id(FullMessageId full_message_id);

  bool is_broadcast_channel(DialogId dialog_id) const;

  int32 recently_found_dialogs_loaded_ = 0;  // 0 - not loaded, 1 - load request was sent, 2 - loaded
  MultiPromiseActor resolve_recent_found_dialogs_multipromise_;

  vector<DialogId> recently_found_dialog_ids_;

  class UploadMediaCallback;
  class UploadThumbnailCallback;
  class UploadDialogPhotoCallback;

  std::shared_ptr<UploadMediaCallback> upload_media_callback_;
  std::shared_ptr<UploadThumbnailCallback> upload_thumbnail_callback_;
  std::shared_ptr<UploadDialogPhotoCallback> upload_dialog_photo_callback_;

  int32 accumulated_pts_count_ = 0;
  int32 accumulated_pts_ = -1;
  Timeout pts_gap_timeout_;

  std::unordered_map<FileId, std::pair<FullMessageId, FileId>, FileIdHash>
      being_uploaded_files_;  // file_id -> message, thumbnail_file_id
  struct UploadedThumbnailInfo {
    FullMessageId full_message_id;
    FileId file_id;                                     // original file file_id
    tl_object_ptr<telegram_api::InputFile> input_file;  // original file InputFile
  };
  std::unordered_map<FileId, UploadedThumbnailInfo, FileIdHash> being_uploaded_thumbnails_;  // thumbnail_file_id -> ...
  struct UploadedSecretThumbnailInfo {
    FullMessageId full_message_id;
    FileId file_id;                                              // original file file_id
    tl_object_ptr<telegram_api::InputEncryptedFile> input_file;  // original file InputEncryptedFile
  };
  std::unordered_map<FileId, UploadedSecretThumbnailInfo, FileIdHash>
      being_loaded_secret_thumbnails_;  // thumbnail_file_id -> ...

  // TTL
  class TtlNode : private HeapNode {
   public:
    TtlNode(DialogId dialog_id, MessageId message_id) : full_message_id(dialog_id, message_id) {
    }

    FullMessageId full_message_id;

    HeapNode *as_heap_node() const {
      return const_cast<HeapNode *>(static_cast<const HeapNode *>(this));
    }
    static TtlNode *from_heap_node(HeapNode *node) {
      return static_cast<TtlNode *>(node);
    }

    bool operator==(const TtlNode &other) const {
      return full_message_id == other.full_message_id;
    }
  };
  struct TtlNodeHash {
    std::size_t operator()(const TtlNode &ttl_node) const {
      return FullMessageIdHash()(ttl_node.full_message_id);
    }
  };
  std::unordered_set<TtlNode, TtlNodeHash> ttl_nodes_;
  KHeap<double> ttl_heap_;
  Slot ttl_slot_;

  enum YieldType : int32 { None, Ttl, TtlDb };  // None must be first
  int32 ttl_db_expire_from_;
  int32 ttl_db_expire_till_;
  bool ttl_db_has_query_;
  Slot ttl_db_slot_;

  std::unordered_set<int64> message_random_ids_;
  std::unordered_map<int64, FullMessageId> being_sent_messages_;  // message_random_id -> message

  std::unordered_map<FullMessageId, MessageId, FullMessageIdHash>
      update_message_ids_;                                         // full_message_id -> temporary_id
  std::unordered_map<int64, DialogId> debug_being_sent_messages_;  // message_random_id -> dialog_id

  const char *debug_add_message_to_dialog_fail_reason_ = "";

  struct UploadedDialogPhotoInfo {
    Promise<Unit> promise;
    DialogId dialog_id;
  };
  std::unordered_map<FileId, UploadedDialogPhotoInfo, FileIdHash> uploaded_dialog_photos_;  // file_id -> ...

  struct PendingMessageGroupSend {
    DialogId dialog_id;
    size_t finished_count = 0;
    vector<MessageId> message_ids;
    vector<bool> is_finished;
    vector<Status> results;
  };
  std::unordered_map<int64, PendingMessageGroupSend> pending_message_group_sends_;  // media_album_id -> ...

  std::unordered_map<MessageId, DialogId, MessageIdHash> message_id_to_dialog_id_;
  std::unordered_map<MessageId, DialogId, MessageIdHash> last_clear_history_message_id_to_dialog_id_;

  std::unordered_map<int64, DialogId> created_dialogs_;                                // random_id -> dialog_id
  std::unordered_map<DialogId, Promise<Unit>, DialogIdHash> pending_created_dialogs_;  // dialog_id -> promise

  bool running_get_difference_ = false;  // true after before_get_difference and false after after_get_difference

  std::unordered_map<DialogId, unique_ptr<Dialog>, DialogIdHash> dialogs_;
  std::multimap<int32, PendingPtsUpdate> pending_updates_;
  std::multimap<int32, PendingPtsUpdate> postponed_pts_updates_;

  std::unordered_set<DialogId, DialogIdHash>
      loaded_dialogs_;  // dialogs loaded from database, but not added to dialogs_

  std::unordered_set<DialogId, DialogIdHash> postponed_chat_read_inbox_updates_;
  std::unordered_map<DialogId, vector<std::pair<MessageId, Promise<Unit>>>, DialogIdHash>
      postponed_get_message_requests_;

  std::unordered_map<string, vector<Promise<Unit>>> search_public_dialogs_queries_;
  std::unordered_map<string, vector<DialogId>> found_public_dialogs_;     // TODO time bound cache
  std::unordered_map<string, vector<DialogId>> found_on_server_dialogs_;  // TODO time bound cache

  std::unordered_map<UserId, vector<DialogId>, UserIdHash> found_common_dialogs_;  // TODO time bound cache

  std::unordered_map<int64, FullMessageId> get_dialog_message_by_date_results_;

  std::unordered_map<int64, std::pair<int32, vector<MessageId>>>
      found_dialog_messages_;  // random_id -> [total_count, [message_id]...]
  std::unordered_map<int64, std::pair<int32, vector<FullMessageId>>>
      found_messages_;  // random_id -> [total_count, [full_message_id]...]
  std::unordered_map<int64, std::pair<int32, vector<FullMessageId>>>
      found_call_messages_;  // random_id -> [total_count, [full_message_id]...]
  std::unordered_map<int64, std::pair<int32, vector<MessageId>>>
      found_dialog_recent_location_messages_;  // random_id -> [total_count, [message_id]...]

  std::unordered_map<int64, std::pair<int64, vector<FullMessageId>>>
      found_fts_messages_;  // random_id -> [from_search_id, [full_message_id]...]

  std::unordered_map<FullMessageId, std::pair<string, string>, FullMessageIdHash> public_message_links_[2];

  std::unordered_map<int64, tl_object_ptr<td_api::chatEvents>> chat_events_;  // random_id -> chat events

  std::unordered_map<int64, tl_object_ptr<td_api::gameHighScores>> game_high_scores_;  // random_id -> high scores

  std::unordered_map<DialogId, vector<Promise<Unit>>, DialogIdHash> get_dialog_notification_settings_queries_;

  std::unordered_map<DialogId, vector<Promise<Unit>>, DialogIdHash> get_dialog_queries_;
  std::unordered_map<DialogId, uint64, DialogIdHash> get_dialog_query_logevent_id_;

  std::unordered_map<FullMessageId, int32, FullMessageIdHash> replied_by_yet_unsent_messages_;

  std::unordered_set<FullMessageId, FullMessageIdHash> waiting_for_web_page_messages_;

  struct ActiveDialogAction {
    UserId user_id;
    int32 action_id;
    int32 progress;
    double start_time;

    ActiveDialogAction(UserId user_id, int32 action_id, double start_time)
        : user_id(user_id), action_id(action_id), start_time(start_time) {
    }
  };

  std::unordered_map<DialogId, std::vector<ActiveDialogAction>, DialogIdHash> active_dialog_actions_;

  ScopeNotificationSettings users_notification_settings_;
  ScopeNotificationSettings chats_notification_settings_;

  bool have_postponed_unread_message_count_update_ = false;
  bool have_postponed_unread_chat_count_update_ = false;
  bool is_message_unread_count_inited_ = false;
  bool is_dialog_unread_count_inited_ = false;
  bool need_unread_count_recalc_ = true;
  int32 unread_message_total_count_ = 0;
  int32 unread_message_muted_count_ = 0;
  int32 unread_dialog_total_count_ = 0;
  int32 unread_dialog_muted_count_ = 0;
  int32 unread_dialog_marked_count_ = 0;
  int32 unread_dialog_muted_marked_count_ = 0;

  int64 current_pinned_dialog_order_ = DEFAULT_ORDER;

  uint64 current_message_edit_generation_ = 0;

  std::set<DialogDate> ordered_dialogs_;
  std::set<DialogDate> ordered_server_dialogs_;

  // date of last dialog in the dialog list
  // last_dialog_date_ == min(last_server_dialog_date_, last_secret_chat_dialog_date_)
  DialogDate last_dialog_date_ = MIN_DIALOG_DATE;  // in memory

  // date of last known user/group/channel dialog in the right order
  DialogDate last_server_dialog_date_ = MIN_DIALOG_DATE;
  DialogDate last_loaded_database_dialog_date_ = MIN_DIALOG_DATE;
  DialogDate last_database_server_dialog_date_ = MIN_DIALOG_DATE;

  MultiPromiseActor load_dialog_list_multipromise_;  // should be defined before pending_on_get_dialogs_
  Timeout preload_dialog_list_timeout_;

  std::unordered_map<DialogId, string, DialogIdHash> active_get_channel_differencies_;
  std::unordered_map<DialogId, uint64, DialogIdHash> get_channel_difference_to_logevent_id_;

  MultiTimeout channel_get_difference_timeout_{"ChannelGetDifferenceTimeout"};
  MultiTimeout channel_get_difference_retry_timeout_{"ChannelGetDifferenceRetryTimeout"};
  MultiTimeout pending_message_views_timeout_{"PendingMessageViewsTimeout"};
  MultiTimeout pending_draft_message_timeout_{"PendingDraftMessageTimeout"};
  MultiTimeout pending_read_history_timeout_{"PendingReadHistoryTimeout"};
  MultiTimeout pending_updated_dialog_timeout_{"PendingUpdatedDialogTimeout"};
  MultiTimeout pending_unload_dialog_timeout_{"PendingUnloadDialogTimeout"};
  MultiTimeout dialog_unmute_timeout_{"DialogUnmuteTimeout"};
  MultiTimeout pending_send_dialog_action_timeout_{"PendingSendDialogActionTimeout"};
  MultiTimeout active_dialog_action_timeout_{"ActiveDialogActionTimeout"};

  Hints dialogs_hints_;  // search dialogs by title and username

  std::unordered_set<FullMessageId, FullMessageIdHash> active_live_location_full_message_ids_;
  bool are_active_live_location_messages_loaded_ = false;
  vector<Promise<Unit>> load_active_live_location_messages_queries_;

  struct ResolvedUsername {
    DialogId dialog_id;
    double expires_at;
  };

  std::unordered_map<string, ResolvedUsername> resolved_usernames_;
  std::unordered_map<string, DialogId> unaccessible_resolved_usernames_;

  struct PendingOnGetDialogs {
    vector<tl_object_ptr<telegram_api::dialog>> dialogs;
    int32 total_count;
    vector<tl_object_ptr<telegram_api::Message>> messages;
    Promise<Unit> promise;
  };

  vector<PendingOnGetDialogs> pending_on_get_dialogs_;
  std::unordered_map<DialogId, PendingOnGetDialogs, DialogIdHash> pending_channel_on_get_dialogs_;

  ChangesProcessor<unique_ptr<PendingSecretMessage>> pending_secret_messages_;

  std::unordered_map<DialogId, vector<DialogId>, DialogIdHash>
      pending_add_dialog_last_database_message_dependent_dialogs_;
  std::unordered_map<DialogId, std::pair<int32, unique_ptr<Message>>, DialogIdHash>
      pending_add_dialog_last_database_message_;  // dialog -> dependency counter + message

  CallsDbState calls_db_state_;

  std::unordered_map<uint64, std::map<int64, Promise<Message *>>> yet_unsent_media_queues_;

  std::unordered_map<DialogId, NetQueryRef, DialogIdHash> set_typing_query_;

  DialogId sponsored_dialog_id_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
