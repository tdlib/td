//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageContent.h"

#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AnimationsManager.hpp"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AudiosManager.hpp"
#include "td/telegram/AuthManager.h"
#include "td/telegram/CallDiscardReason.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatId.h"
#include "td/telegram/ConfigShared.h"
#include "td/telegram/Contact.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogAction.h"
#include "td/telegram/DialogParticipant.h"
#include "td/telegram/Dimensions.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DocumentsManager.hpp"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileId.hpp"
#include "td/telegram/files/FileLocation.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/files/FileType.h"
#include "td/telegram/Game.h"
#include "td/telegram/Game.hpp"
#include "td/telegram/Global.h"
#include "td/telegram/GroupCallManager.h"
#include "td/telegram/HashtagHints.h"
#include "td/telegram/InputGroupCallId.h"
#include "td/telegram/InputMessageText.h"
#include "td/telegram/Location.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageEntity.hpp"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/MessageSender.h"
#include "td/telegram/misc.h"
#include "td/telegram/net/DcId.h"
#include "td/telegram/Payments.h"
#include "td/telegram/Payments.hpp"
#include "td/telegram/Photo.h"
#include "td/telegram/Photo.hpp"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/PhotoSize.h"
#include "td/telegram/PhotoSizeSource.h"
#include "td/telegram/PollId.h"
#include "td/telegram/PollId.hpp"
#include "td/telegram/PollManager.h"
#include "td/telegram/secret_api.hpp"
#include "td/telegram/SecureValue.h"
#include "td/telegram/SecureValue.hpp"
#include "td/telegram/StickerFormat.h"
#include "td/telegram/StickersManager.h"
#include "td/telegram/StickersManager.hpp"
#include "td/telegram/Td.h"
#include "td/telegram/TopDialogManager.h"
#include "td/telegram/UserId.h"
#include "td/telegram/Venue.h"
#include "td/telegram/Version.h"
#include "td/telegram/VideoNotesManager.h"
#include "td/telegram/VideoNotesManager.hpp"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VideosManager.hpp"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/VoiceNotesManager.hpp"
#include "td/telegram/WebPageId.h"
#include "td/telegram/WebPagesManager.h"

#include "td/actor/actor.h"
#include "td/actor/MultiPromise.h"

#include "td/utils/algorithm.h"
#include "td/utils/emoji.h"
#include "td/utils/format.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/MimeType.h"
#include "td/utils/misc.h"
#include "td/utils/PathView.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/tl_helpers.h"
#include "td/utils/utf8.h"

#include <limits>
#include <utility>

namespace td {

class MessageText final : public MessageContent {
 public:
  FormattedText text;
  WebPageId web_page_id;

  MessageText() = default;
  MessageText(FormattedText text, WebPageId web_page_id) : text(std::move(text)), web_page_id(web_page_id) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Text;
  }
};

class MessageAnimation final : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageAnimation() = default;
  MessageAnimation(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Animation;
  }
};

class MessageAudio final : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageAudio() = default;
  MessageAudio(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Audio;
  }
};

class MessageDocument final : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageDocument() = default;
  MessageDocument(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Document;
  }
};

class MessagePhoto final : public MessageContent {
 public:
  Photo photo;

  FormattedText caption;

  MessagePhoto() = default;
  MessagePhoto(Photo &&photo, FormattedText &&caption) : photo(std::move(photo)), caption(std::move(caption)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Photo;
  }
};

class MessageSticker final : public MessageContent {
 public:
  FileId file_id;
  bool is_premium = false;

  MessageSticker() = default;
  MessageSticker(FileId file_id, bool is_premium) : file_id(file_id), is_premium(is_premium) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Sticker;
  }
};

class MessageVideo final : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;

  MessageVideo() = default;
  MessageVideo(FileId file_id, FormattedText &&caption) : file_id(file_id), caption(std::move(caption)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Video;
  }
};

class MessageVoiceNote final : public MessageContent {
 public:
  FileId file_id;

  FormattedText caption;
  bool is_listened;

  MessageVoiceNote() = default;
  MessageVoiceNote(FileId file_id, FormattedText &&caption, bool is_listened)
      : file_id(file_id), caption(std::move(caption)), is_listened(is_listened) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::VoiceNote;
  }
};

class MessageContact final : public MessageContent {
 public:
  Contact contact;

  MessageContact() = default;
  explicit MessageContact(Contact &&contact) : contact(std::move(contact)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Contact;
  }
};

class MessageLocation final : public MessageContent {
 public:
  Location location;

  MessageLocation() = default;
  explicit MessageLocation(Location &&location) : location(std::move(location)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Location;
  }
};

class MessageVenue final : public MessageContent {
 public:
  Venue venue;

  MessageVenue() = default;
  explicit MessageVenue(Venue &&venue) : venue(std::move(venue)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Venue;
  }
};

class MessageChatCreate final : public MessageContent {
 public:
  string title;
  vector<UserId> participant_user_ids;

  MessageChatCreate() = default;
  MessageChatCreate(string &&title, vector<UserId> &&participant_user_ids)
      : title(std::move(title)), participant_user_ids(std::move(participant_user_ids)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatCreate;
  }
};

class MessageChatChangeTitle final : public MessageContent {
 public:
  string title;

  MessageChatChangeTitle() = default;
  explicit MessageChatChangeTitle(string &&title) : title(std::move(title)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatChangeTitle;
  }
};

class MessageChatChangePhoto final : public MessageContent {
 public:
  Photo photo;

  MessageChatChangePhoto() = default;
  explicit MessageChatChangePhoto(Photo &&photo) : photo(std::move(photo)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatChangePhoto;
  }
};

class MessageChatDeletePhoto final : public MessageContent {
 public:
  MessageContentType get_type() const final {
    return MessageContentType::ChatDeletePhoto;
  }
};

class MessageChatDeleteHistory final : public MessageContent {
 public:
  MessageContentType get_type() const final {
    return MessageContentType::ChatDeleteHistory;
  }
};

class MessageChatAddUsers final : public MessageContent {
 public:
  vector<UserId> user_ids;

  MessageChatAddUsers() = default;
  explicit MessageChatAddUsers(vector<UserId> &&user_ids) : user_ids(std::move(user_ids)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatAddUsers;
  }
};

class MessageChatJoinedByLink final : public MessageContent {
 public:
  bool is_approved = false;

  MessageChatJoinedByLink() = default;
  explicit MessageChatJoinedByLink(bool is_approved) : is_approved(is_approved) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatJoinedByLink;
  }
};

class MessageChatDeleteUser final : public MessageContent {
 public:
  UserId user_id;

  MessageChatDeleteUser() = default;
  explicit MessageChatDeleteUser(UserId user_id) : user_id(user_id) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatDeleteUser;
  }
};

class MessageChatMigrateTo final : public MessageContent {
 public:
  ChannelId migrated_to_channel_id;

  MessageChatMigrateTo() = default;
  explicit MessageChatMigrateTo(ChannelId migrated_to_channel_id) : migrated_to_channel_id(migrated_to_channel_id) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatMigrateTo;
  }
};

class MessageChannelCreate final : public MessageContent {
 public:
  string title;

  MessageChannelCreate() = default;
  explicit MessageChannelCreate(string &&title) : title(std::move(title)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChannelCreate;
  }
};

class MessageChannelMigrateFrom final : public MessageContent {
 public:
  string title;
  ChatId migrated_from_chat_id;

  MessageChannelMigrateFrom() = default;
  MessageChannelMigrateFrom(string &&title, ChatId migrated_from_chat_id)
      : title(std::move(title)), migrated_from_chat_id(migrated_from_chat_id) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChannelMigrateFrom;
  }
};

class MessagePinMessage final : public MessageContent {
 public:
  MessageId message_id;

  MessagePinMessage() = default;
  explicit MessagePinMessage(MessageId message_id) : message_id(message_id) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::PinMessage;
  }
};

class MessageGame final : public MessageContent {
 public:
  Game game;

  MessageGame() = default;
  explicit MessageGame(Game &&game) : game(std::move(game)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Game;
  }
};

class MessageGameScore final : public MessageContent {
 public:
  MessageId game_message_id;
  int64 game_id;
  int32 score;

  MessageGameScore() = default;
  MessageGameScore(MessageId game_message_id, int64 game_id, int32 score)
      : game_message_id(game_message_id), game_id(game_id), score(score) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::GameScore;
  }
};

class MessageScreenshotTaken final : public MessageContent {
 public:
  MessageContentType get_type() const final {
    return MessageContentType::ScreenshotTaken;
  }
};

class MessageChatSetTtl final : public MessageContent {
 public:
  int32 ttl;

  MessageChatSetTtl() = default;
  explicit MessageChatSetTtl(int32 ttl) : ttl(ttl) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatSetTtl;
  }
};

class MessageUnsupported final : public MessageContent {
 public:
  static constexpr int32 CURRENT_VERSION = 11;
  int32 version = CURRENT_VERSION;

  MessageUnsupported() = default;
  explicit MessageUnsupported(int32 version) : version(version) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Unsupported;
  }
};

class MessageCall final : public MessageContent {
 public:
  int64 call_id;
  int32 duration;
  CallDiscardReason discard_reason;
  bool is_video;

  MessageCall() = default;
  MessageCall(int64 call_id, int32 duration, CallDiscardReason discard_reason, bool is_video)
      : call_id(call_id), duration(duration), discard_reason(discard_reason), is_video(is_video) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Call;
  }
};

class MessageInvoice final : public MessageContent {
 public:
  InputInvoice input_invoice;

  MessageInvoice() = default;
  explicit MessageInvoice(InputInvoice &&input_invoice) : input_invoice(std::move(input_invoice)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Invoice;
  }
};

class MessagePaymentSuccessful final : public MessageContent {
 public:
  DialogId invoice_dialog_id;
  MessageId invoice_message_id;
  string currency;
  int64 total_amount = 0;
  string invoice_payload;  // or invoice_slug for users
  bool is_recurring = false;
  bool is_first_recurring = false;

  // bots only part
  string shipping_option_id;
  unique_ptr<OrderInfo> order_info;
  string telegram_payment_charge_id;
  string provider_payment_charge_id;

  MessagePaymentSuccessful() = default;
  MessagePaymentSuccessful(DialogId invoice_dialog_id, MessageId invoice_message_id, string &&currency,
                           int64 total_amount, string &&invoice_payload, bool is_recurring, bool is_first_recurring)
      : invoice_dialog_id(invoice_dialog_id)
      , invoice_message_id(invoice_message_id)
      , currency(std::move(currency))
      , total_amount(total_amount)
      , invoice_payload(std::move(invoice_payload))
      , is_recurring(is_recurring || is_first_recurring)
      , is_first_recurring(is_first_recurring) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::PaymentSuccessful;
  }
};

class MessageVideoNote final : public MessageContent {
 public:
  FileId file_id;

  bool is_viewed = false;

  MessageVideoNote() = default;
  MessageVideoNote(FileId file_id, bool is_viewed) : file_id(file_id), is_viewed(is_viewed) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::VideoNote;
  }
};

class MessageContactRegistered final : public MessageContent {
 public:
  MessageContentType get_type() const final {
    return MessageContentType::ContactRegistered;
  }
};

class MessageExpiredPhoto final : public MessageContent {
 public:
  MessageExpiredPhoto() = default;

  MessageContentType get_type() const final {
    return MessageContentType::ExpiredPhoto;
  }
};

class MessageExpiredVideo final : public MessageContent {
 public:
  MessageExpiredVideo() = default;

  MessageContentType get_type() const final {
    return MessageContentType::ExpiredVideo;
  }
};

class MessageLiveLocation final : public MessageContent {
 public:
  Location location;
  int32 period = 0;
  int32 heading = 0;
  int32 proximity_alert_radius = 0;

  MessageLiveLocation() = default;
  MessageLiveLocation(Location &&location, int32 period, int32 heading, int32 proximity_alert_radius)
      : location(std::move(location))
      , period(period)
      , heading(heading)
      , proximity_alert_radius(proximity_alert_radius) {
    if (period < 0) {
      this->period = 0;
    }
    if (heading < 0 || heading > 360) {
      LOG(ERROR) << "Receive wrong heading " << heading;
      this->heading = 0;
    }
    if (proximity_alert_radius < 0) {
      this->proximity_alert_radius = 0;
    }
  }

  MessageContentType get_type() const final {
    return MessageContentType::LiveLocation;
  }
};

class MessageCustomServiceAction final : public MessageContent {
 public:
  string message;

  MessageCustomServiceAction() = default;
  explicit MessageCustomServiceAction(string &&message) : message(std::move(message)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::CustomServiceAction;
  }
};

class MessageWebsiteConnected final : public MessageContent {
 public:
  string domain_name;

  MessageWebsiteConnected() = default;
  explicit MessageWebsiteConnected(string &&domain_name) : domain_name(std::move(domain_name)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::WebsiteConnected;
  }
};

class MessagePassportDataSent final : public MessageContent {
 public:
  vector<SecureValueType> types;

  MessagePassportDataSent() = default;
  explicit MessagePassportDataSent(vector<SecureValueType> &&types) : types(std::move(types)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::PassportDataSent;
  }
};

class MessagePassportDataReceived final : public MessageContent {
 public:
  vector<EncryptedSecureValue> values;
  EncryptedSecureCredentials credentials;

  MessagePassportDataReceived() = default;
  MessagePassportDataReceived(vector<EncryptedSecureValue> &&values, EncryptedSecureCredentials &&credentials)
      : values(std::move(values)), credentials(std::move(credentials)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::PassportDataReceived;
  }
};

class MessagePoll final : public MessageContent {
 public:
  PollId poll_id;

  MessagePoll() = default;
  explicit MessagePoll(PollId poll_id) : poll_id(poll_id) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Poll;
  }
};

class MessageDice final : public MessageContent {
 public:
  string emoji;
  int32 dice_value = 0;

  static constexpr const char *DEFAULT_EMOJI = "🎲";

  MessageDice() = default;
  MessageDice(const string &emoji, int32 dice_value)
      : emoji(emoji.empty() ? string(DEFAULT_EMOJI) : remove_emoji_modifiers(emoji)), dice_value(dice_value) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::Dice;
  }

  bool is_valid() const {
    if (dice_value < 0) {
      return false;
    }
    if (emoji == DEFAULT_EMOJI || emoji == "🎯") {
      return dice_value <= 6;
    }
    return dice_value <= 1000;
  }
};

constexpr const char *MessageDice::DEFAULT_EMOJI;

class MessageProximityAlertTriggered final : public MessageContent {
 public:
  DialogId traveler_dialog_id;
  DialogId watcher_dialog_id;
  int32 distance = 0;

  MessageProximityAlertTriggered() = default;
  MessageProximityAlertTriggered(DialogId traveler_dialog_id, DialogId watcher_dialog_id, int32 distance)
      : traveler_dialog_id(traveler_dialog_id), watcher_dialog_id(watcher_dialog_id), distance(distance) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ProximityAlertTriggered;
  }
};

class MessageGroupCall final : public MessageContent {
 public:
  InputGroupCallId input_group_call_id;
  int32 duration = -1;
  int32 schedule_date = -1;

  MessageGroupCall() = default;
  MessageGroupCall(InputGroupCallId input_group_call_id, int32 duration, int32 schedule_date)
      : input_group_call_id(input_group_call_id), duration(duration), schedule_date(schedule_date) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::GroupCall;
  }
};

class MessageInviteToGroupCall final : public MessageContent {
 public:
  InputGroupCallId input_group_call_id;
  vector<UserId> user_ids;

  MessageInviteToGroupCall() = default;
  MessageInviteToGroupCall(InputGroupCallId input_group_call_id, vector<UserId> &&user_ids)
      : input_group_call_id(input_group_call_id), user_ids(std::move(user_ids)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::InviteToGroupCall;
  }
};

class MessageChatSetTheme final : public MessageContent {
 public:
  string emoji;

  MessageChatSetTheme() = default;
  explicit MessageChatSetTheme(string &&emoji) : emoji(std::move(emoji)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::ChatSetTheme;
  }
};

class MessageWebViewDataSent final : public MessageContent {
 public:
  string button_text;

  MessageWebViewDataSent() = default;
  explicit MessageWebViewDataSent(string &&button_text) : button_text(std::move(button_text)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::WebViewDataSent;
  }
};

class MessageWebViewDataReceived final : public MessageContent {
 public:
  string button_text;
  string data;

  MessageWebViewDataReceived() = default;
  MessageWebViewDataReceived(string &&button_text, string &&data)
      : button_text(std::move(button_text)), data(std::move(data)) {
  }

  MessageContentType get_type() const final {
    return MessageContentType::WebViewDataReceived;
  }
};

template <class StorerT>
static void store(const MessageContent *content, StorerT &storer) {
  CHECK(content != nullptr);

  Td *td = storer.context()->td().get_actor_unsafe();
  CHECK(td != nullptr);

  auto content_type = content->get_type();
  store(content_type, storer);

  switch (content_type) {
    case MessageContentType::Animation: {
      const auto *m = static_cast<const MessageAnimation *>(content);
      td->animations_manager_->store_animation(m->file_id, storer);
      store(m->caption, storer);
      break;
    }
    case MessageContentType::Audio: {
      const auto *m = static_cast<const MessageAudio *>(content);
      td->audios_manager_->store_audio(m->file_id, storer);
      store(m->caption, storer);
      store(true, storer);
      break;
    }
    case MessageContentType::Contact: {
      const auto *m = static_cast<const MessageContact *>(content);
      store(m->contact, storer);
      break;
    }
    case MessageContentType::Document: {
      const auto *m = static_cast<const MessageDocument *>(content);
      td->documents_manager_->store_document(m->file_id, storer);
      store(m->caption, storer);
      break;
    }
    case MessageContentType::Game: {
      const auto *m = static_cast<const MessageGame *>(content);
      store(m->game, storer);
      break;
    }
    case MessageContentType::Invoice: {
      const auto *m = static_cast<const MessageInvoice *>(content);
      store(m->input_invoice, storer);
      break;
    }
    case MessageContentType::LiveLocation: {
      const auto *m = static_cast<const MessageLiveLocation *>(content);
      store(m->location, storer);
      store(m->period, storer);
      store(m->heading, storer);
      store(m->proximity_alert_radius, storer);
      break;
    }
    case MessageContentType::Location: {
      const auto *m = static_cast<const MessageLocation *>(content);
      store(m->location, storer);
      break;
    }
    case MessageContentType::Photo: {
      const auto *m = static_cast<const MessagePhoto *>(content);
      store(m->photo, storer);
      store(m->caption, storer);
      break;
    }
    case MessageContentType::Sticker: {
      const auto *m = static_cast<const MessageSticker *>(content);
      td->stickers_manager_->store_sticker(m->file_id, false, storer, "MessageSticker");
      BEGIN_STORE_FLAGS();
      STORE_FLAG(m->is_premium);
      END_STORE_FLAGS();
      break;
    }
    case MessageContentType::Text: {
      const auto *m = static_cast<const MessageText *>(content);
      store(m->text, storer);
      store(m->web_page_id, storer);
      break;
    }
    case MessageContentType::Unsupported: {
      const auto *m = static_cast<const MessageUnsupported *>(content);
      store(m->version, storer);
      break;
    }
    case MessageContentType::Venue: {
      const auto *m = static_cast<const MessageVenue *>(content);
      store(m->venue, storer);
      break;
    }
    case MessageContentType::Video: {
      const auto *m = static_cast<const MessageVideo *>(content);
      td->videos_manager_->store_video(m->file_id, storer);
      store(m->caption, storer);
      break;
    }
    case MessageContentType::VideoNote: {
      const auto *m = static_cast<const MessageVideoNote *>(content);
      td->video_notes_manager_->store_video_note(m->file_id, storer);
      store(m->is_viewed, storer);
      break;
    }
    case MessageContentType::VoiceNote: {
      const auto *m = static_cast<const MessageVoiceNote *>(content);
      td->voice_notes_manager_->store_voice_note(m->file_id, storer);
      store(m->caption, storer);
      store(m->is_listened, storer);
      break;
    }
    case MessageContentType::ChatCreate: {
      const auto *m = static_cast<const MessageChatCreate *>(content);
      store(m->title, storer);
      store(m->participant_user_ids, storer);
      break;
    }
    case MessageContentType::ChatChangeTitle: {
      const auto *m = static_cast<const MessageChatChangeTitle *>(content);
      store(m->title, storer);
      break;
    }
    case MessageContentType::ChatChangePhoto: {
      const auto *m = static_cast<const MessageChatChangePhoto *>(content);
      store(m->photo, storer);
      break;
    }
    case MessageContentType::ChatDeletePhoto:
    case MessageContentType::ChatDeleteHistory:
      break;
    case MessageContentType::ChatAddUsers: {
      const auto *m = static_cast<const MessageChatAddUsers *>(content);
      store(m->user_ids, storer);
      break;
    }
    case MessageContentType::ChatJoinedByLink: {
      auto m = static_cast<const MessageChatJoinedByLink *>(content);
      BEGIN_STORE_FLAGS();
      STORE_FLAG(m->is_approved);
      END_STORE_FLAGS();
      break;
    }
    case MessageContentType::ChatDeleteUser: {
      const auto *m = static_cast<const MessageChatDeleteUser *>(content);
      store(m->user_id, storer);
      break;
    }
    case MessageContentType::ChatMigrateTo: {
      const auto *m = static_cast<const MessageChatMigrateTo *>(content);
      store(m->migrated_to_channel_id, storer);
      break;
    }
    case MessageContentType::ChannelCreate: {
      const auto *m = static_cast<const MessageChannelCreate *>(content);
      store(m->title, storer);
      break;
    }
    case MessageContentType::ChannelMigrateFrom: {
      const auto *m = static_cast<const MessageChannelMigrateFrom *>(content);
      store(m->title, storer);
      store(m->migrated_from_chat_id, storer);
      break;
    }
    case MessageContentType::PinMessage: {
      const auto *m = static_cast<const MessagePinMessage *>(content);
      store(m->message_id, storer);
      break;
    }
    case MessageContentType::GameScore: {
      const auto *m = static_cast<const MessageGameScore *>(content);
      store(m->game_message_id, storer);
      store(m->game_id, storer);
      store(m->score, storer);
      break;
    }
    case MessageContentType::ScreenshotTaken:
      break;
    case MessageContentType::ChatSetTtl: {
      const auto *m = static_cast<const MessageChatSetTtl *>(content);
      store(m->ttl, storer);
      break;
    }
    case MessageContentType::Call: {
      const auto *m = static_cast<const MessageCall *>(content);
      BEGIN_STORE_FLAGS();
      STORE_FLAG(m->is_video);
      END_STORE_FLAGS();
      store(m->call_id, storer);
      store(m->duration, storer);
      store(m->discard_reason, storer);
      break;
    }
    case MessageContentType::PaymentSuccessful: {
      const auto *m = static_cast<const MessagePaymentSuccessful *>(content);
      bool has_payload = !m->invoice_payload.empty();
      bool has_shipping_option_id = !m->shipping_option_id.empty();
      bool has_order_info = m->order_info != nullptr;
      bool has_telegram_payment_charge_id = !m->telegram_payment_charge_id.empty();
      bool has_provider_payment_charge_id = !m->provider_payment_charge_id.empty();
      bool has_invoice_message_id = m->invoice_message_id.is_valid();
      bool is_correctly_stored = true;
      bool has_invoice_dialog_id = m->invoice_dialog_id.is_valid();
      BEGIN_STORE_FLAGS();
      STORE_FLAG(has_payload);
      STORE_FLAG(has_shipping_option_id);
      STORE_FLAG(has_order_info);
      STORE_FLAG(has_telegram_payment_charge_id);
      STORE_FLAG(has_provider_payment_charge_id);
      STORE_FLAG(has_invoice_message_id);
      STORE_FLAG(is_correctly_stored);
      STORE_FLAG(has_invoice_dialog_id);
      STORE_FLAG(m->is_recurring);
      STORE_FLAG(m->is_first_recurring);
      END_STORE_FLAGS();
      store(m->currency, storer);
      store(m->total_amount, storer);
      if (has_payload) {
        store(m->invoice_payload, storer);
      }
      if (has_shipping_option_id) {
        store(m->shipping_option_id, storer);
      }
      if (has_order_info) {
        store(m->order_info, storer);
      }
      if (has_telegram_payment_charge_id) {
        store(m->telegram_payment_charge_id, storer);
      }
      if (has_provider_payment_charge_id) {
        store(m->provider_payment_charge_id, storer);
      }
      if (has_invoice_message_id) {
        store(m->invoice_message_id, storer);
      }
      if (has_invoice_dialog_id) {
        store(m->invoice_dialog_id, storer);
      }
      break;
    }
    case MessageContentType::ContactRegistered:
      break;
    case MessageContentType::ExpiredPhoto:
      break;
    case MessageContentType::ExpiredVideo:
      break;
    case MessageContentType::CustomServiceAction: {
      const auto *m = static_cast<const MessageCustomServiceAction *>(content);
      store(m->message, storer);
      break;
    }
    case MessageContentType::WebsiteConnected: {
      const auto *m = static_cast<const MessageWebsiteConnected *>(content);
      store(m->domain_name, storer);
      break;
    }
    case MessageContentType::PassportDataSent: {
      const auto *m = static_cast<const MessagePassportDataSent *>(content);
      store(m->types, storer);
      break;
    }
    case MessageContentType::PassportDataReceived: {
      const auto *m = static_cast<const MessagePassportDataReceived *>(content);
      store(m->values, storer);
      store(m->credentials, storer);
      break;
    }
    case MessageContentType::Poll: {
      const auto *m = static_cast<const MessagePoll *>(content);
      store(m->poll_id, storer);
      break;
    }
    case MessageContentType::Dice: {
      const auto *m = static_cast<const MessageDice *>(content);
      store(m->emoji, storer);
      store(m->dice_value, storer);
      break;
    }
    case MessageContentType::ProximityAlertTriggered: {
      const auto *m = static_cast<const MessageProximityAlertTriggered *>(content);
      store(m->traveler_dialog_id, storer);
      store(m->watcher_dialog_id, storer);
      store(m->distance, storer);
      break;
    }
    case MessageContentType::GroupCall: {
      const auto *m = static_cast<const MessageGroupCall *>(content);
      bool has_duration = m->duration >= 0;
      bool has_schedule_date = m->schedule_date > 0;
      BEGIN_STORE_FLAGS();
      STORE_FLAG(has_duration);
      STORE_FLAG(has_schedule_date);
      END_STORE_FLAGS();
      store(m->input_group_call_id, storer);
      if (has_duration) {
        store(m->duration, storer);
      }
      if (has_schedule_date) {
        store(m->schedule_date, storer);
      }
      break;
    }
    case MessageContentType::InviteToGroupCall: {
      const auto *m = static_cast<const MessageInviteToGroupCall *>(content);
      store(m->input_group_call_id, storer);
      store(m->user_ids, storer);
      break;
    }
    case MessageContentType::ChatSetTheme: {
      const auto *m = static_cast<const MessageChatSetTheme *>(content);
      store(m->emoji, storer);
      break;
    }
    case MessageContentType::WebViewDataSent: {
      const auto *m = static_cast<const MessageWebViewDataSent *>(content);
      store(m->button_text, storer);
      break;
    }
    case MessageContentType::WebViewDataReceived: {
      const auto *m = static_cast<const MessageWebViewDataReceived *>(content);
      store(m->button_text, storer);
      store(m->data, storer);
      break;
    }
    default:
      UNREACHABLE();
  }
}

template <class ParserT>
static void parse_caption(FormattedText &caption, ParserT &parser) {
  parse(caption.text, parser);
  if (parser.version() >= static_cast<int32>(Version::AddCaptionEntities)) {
    parse(caption.entities, parser);
  } else {
    if (!check_utf8(caption.text)) {
      caption.text.clear();
    }
    caption.entities = find_entities(caption.text, false, true);
  }
}

template <class ParserT>
static void parse(unique_ptr<MessageContent> &content, ParserT &parser) {
  Td *td = parser.context()->td().get_actor_unsafe();
  CHECK(td != nullptr);

  MessageContentType content_type;
  parse(content_type, parser);

  bool is_bad = false;
  switch (content_type) {
    case MessageContentType::Animation: {
      auto m = make_unique<MessageAnimation>();
      m->file_id = td->animations_manager_->parse_animation(parser);
      parse_caption(m->caption, parser);
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::Audio: {
      auto m = make_unique<MessageAudio>();
      m->file_id = td->audios_manager_->parse_audio(parser);
      parse_caption(m->caption, parser);
      bool legacy_is_listened;
      parse(legacy_is_listened, parser);
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::Contact: {
      auto m = make_unique<MessageContact>();
      parse(m->contact, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Document: {
      auto m = make_unique<MessageDocument>();
      m->file_id = td->documents_manager_->parse_document(parser);
      parse_caption(m->caption, parser);
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::Game: {
      auto m = make_unique<MessageGame>();
      parse(m->game, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Invoice: {
      auto m = make_unique<MessageInvoice>();
      parse(m->input_invoice, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::LiveLocation: {
      auto m = make_unique<MessageLiveLocation>();
      parse(m->location, parser);
      parse(m->period, parser);
      if (parser.version() >= static_cast<int32>(Version::AddLiveLocationHeading)) {
        parse(m->heading, parser);
      } else {
        m->heading = 0;
      }
      if (parser.version() >= static_cast<int32>(Version::AddLiveLocationProximityAlertDistance)) {
        parse(m->proximity_alert_radius, parser);
      } else {
        m->proximity_alert_radius = 0;
      }
      content = std::move(m);
      break;
    }
    case MessageContentType::Location: {
      auto m = make_unique<MessageLocation>();
      parse(m->location, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Photo: {
      auto m = make_unique<MessagePhoto>();
      parse(m->photo, parser);
      for (auto &photo_size : m->photo.photos) {
        if (!photo_size.file_id.is_valid()) {
          is_bad = true;
        }
      }
      if (m->photo.is_empty()) {
        is_bad = true;
      }
      parse_caption(m->caption, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Sticker: {
      auto m = make_unique<MessageSticker>();
      m->file_id = td->stickers_manager_->parse_sticker(false, parser);
      if (parser.version() >= static_cast<int32>(Version::AddMessageStickerFlags)) {
        BEGIN_PARSE_FLAGS();
        PARSE_FLAG(m->is_premium);
        END_PARSE_FLAGS();
      }
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::Text: {
      auto m = make_unique<MessageText>();
      parse(m->text, parser);
      parse(m->web_page_id, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Unsupported: {
      auto m = make_unique<MessageUnsupported>();
      if (parser.version() >= static_cast<int32>(Version::AddMessageUnsupportedVersion)) {
        parse(m->version, parser);
      } else {
        m->version = 0;
      }
      content = std::move(m);
      break;
    }
    case MessageContentType::Venue: {
      auto m = make_unique<MessageVenue>();
      parse(m->venue, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Video: {
      auto m = make_unique<MessageVideo>();
      m->file_id = td->videos_manager_->parse_video(parser);
      parse_caption(m->caption, parser);
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::VideoNote: {
      auto m = make_unique<MessageVideoNote>();
      m->file_id = td->video_notes_manager_->parse_video_note(parser);
      parse(m->is_viewed, parser);
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::VoiceNote: {
      auto m = make_unique<MessageVoiceNote>();
      m->file_id = td->voice_notes_manager_->parse_voice_note(parser);
      parse_caption(m->caption, parser);
      parse(m->is_listened, parser);
      is_bad = !m->file_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatCreate: {
      auto m = make_unique<MessageChatCreate>();
      parse(m->title, parser);
      parse(m->participant_user_ids, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatChangeTitle: {
      auto m = make_unique<MessageChatChangeTitle>();
      parse(m->title, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatChangePhoto: {
      auto m = make_unique<MessageChatChangePhoto>();
      parse(m->photo, parser);
      if (m->photo.is_empty()) {
        is_bad = true;
      }
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatDeletePhoto:
      content = make_unique<MessageChatDeletePhoto>();
      break;
    case MessageContentType::ChatDeleteHistory:
      content = make_unique<MessageChatDeleteHistory>();
      break;
    case MessageContentType::ChatAddUsers: {
      auto m = make_unique<MessageChatAddUsers>();
      parse(m->user_ids, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatJoinedByLink: {
      auto m = make_unique<MessageChatJoinedByLink>();
      if (parser.version() >= static_cast<int32>(Version::AddInviteLinksRequiringApproval)) {
        BEGIN_PARSE_FLAGS();
        PARSE_FLAG(m->is_approved);
        END_PARSE_FLAGS();
      } else {
        m->is_approved = false;
      }
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatDeleteUser: {
      auto m = make_unique<MessageChatDeleteUser>();
      parse(m->user_id, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatMigrateTo: {
      auto m = make_unique<MessageChatMigrateTo>();
      parse(m->migrated_to_channel_id, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChannelCreate: {
      auto m = make_unique<MessageChannelCreate>();
      parse(m->title, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChannelMigrateFrom: {
      auto m = make_unique<MessageChannelMigrateFrom>();
      parse(m->title, parser);
      parse(m->migrated_from_chat_id, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::PinMessage: {
      auto m = make_unique<MessagePinMessage>();
      parse(m->message_id, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::GameScore: {
      auto m = make_unique<MessageGameScore>();
      parse(m->game_message_id, parser);
      parse(m->game_id, parser);
      parse(m->score, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ScreenshotTaken:
      content = make_unique<MessageScreenshotTaken>();
      break;
    case MessageContentType::ChatSetTtl: {
      auto m = make_unique<MessageChatSetTtl>();
      parse(m->ttl, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Call: {
      auto m = make_unique<MessageCall>();
      if (parser.version() >= static_cast<int32>(Version::AddVideoCallsSupport)) {
        BEGIN_PARSE_FLAGS();
        PARSE_FLAG(m->is_video);
        END_PARSE_FLAGS();
      } else {
        m->is_video = false;
      }
      parse(m->call_id, parser);
      parse(m->duration, parser);
      parse(m->discard_reason, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::PaymentSuccessful: {
      auto m = make_unique<MessagePaymentSuccessful>();
      bool has_payload;
      bool has_shipping_option_id;
      bool has_order_info;
      bool has_telegram_payment_charge_id;
      bool has_provider_payment_charge_id;
      bool has_invoice_message_id;
      bool is_correctly_stored;
      bool has_invoice_dialog_id;
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(has_payload);
      PARSE_FLAG(has_shipping_option_id);
      PARSE_FLAG(has_order_info);
      PARSE_FLAG(has_telegram_payment_charge_id);
      PARSE_FLAG(has_provider_payment_charge_id);
      PARSE_FLAG(has_invoice_message_id);
      PARSE_FLAG(is_correctly_stored);
      PARSE_FLAG(has_invoice_dialog_id);
      PARSE_FLAG(m->is_recurring);
      PARSE_FLAG(m->is_first_recurring);
      END_PARSE_FLAGS();
      parse(m->currency, parser);
      parse(m->total_amount, parser);
      if (is_correctly_stored) {
        if (has_payload) {
          parse(m->invoice_payload, parser);
        }
        if (has_shipping_option_id) {
          parse(m->shipping_option_id, parser);
        }
      } else {
        if (has_payload) {
          parse(m->total_amount, parser);
        }
        if (has_shipping_option_id) {
          parse(m->invoice_payload, parser);
        }
      }
      if (has_order_info) {
        parse(m->order_info, parser);
      }
      if (has_telegram_payment_charge_id) {
        parse(m->telegram_payment_charge_id, parser);
      }
      if (has_provider_payment_charge_id) {
        parse(m->provider_payment_charge_id, parser);
      }
      if (has_invoice_message_id) {
        parse(m->invoice_message_id, parser);
      }
      if (has_invoice_dialog_id) {
        parse(m->invoice_dialog_id, parser);
      }
      if (is_correctly_stored) {
        content = std::move(m);
      } else {
        content = make_unique<MessageUnsupported>(0);
      }
      break;
    }
    case MessageContentType::ContactRegistered:
      content = make_unique<MessageContactRegistered>();
      break;
    case MessageContentType::ExpiredPhoto:
      content = make_unique<MessageExpiredPhoto>();
      break;
    case MessageContentType::ExpiredVideo:
      content = make_unique<MessageExpiredVideo>();
      break;
    case MessageContentType::CustomServiceAction: {
      auto m = make_unique<MessageCustomServiceAction>();
      parse(m->message, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::WebsiteConnected: {
      auto m = make_unique<MessageWebsiteConnected>();
      parse(m->domain_name, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::PassportDataSent: {
      auto m = make_unique<MessagePassportDataSent>();
      parse(m->types, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::PassportDataReceived: {
      auto m = make_unique<MessagePassportDataReceived>();
      parse(m->values, parser);
      parse(m->credentials, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::Poll: {
      auto m = make_unique<MessagePoll>();
      parse(m->poll_id, parser);
      is_bad = !m->poll_id.is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::Dice: {
      auto m = make_unique<MessageDice>();
      if (parser.version() >= static_cast<int32>(Version::AddDiceEmoji)) {
        parse(m->emoji, parser);
        remove_emoji_modifiers_in_place(m->emoji);
      } else {
        m->emoji = MessageDice::DEFAULT_EMOJI;
      }
      parse(m->dice_value, parser);
      is_bad = !m->is_valid();
      content = std::move(m);
      break;
    }
    case MessageContentType::ProximityAlertTriggered: {
      auto m = make_unique<MessageProximityAlertTriggered>();
      parse(m->traveler_dialog_id, parser);
      parse(m->watcher_dialog_id, parser);
      parse(m->distance, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::GroupCall: {
      auto m = make_unique<MessageGroupCall>();
      bool has_duration;
      bool has_schedule_date;
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(has_duration);
      PARSE_FLAG(has_schedule_date);
      END_PARSE_FLAGS();
      parse(m->input_group_call_id, parser);
      if (has_duration) {
        parse(m->duration, parser);
      }
      if (has_schedule_date) {
        parse(m->schedule_date, parser);
      }
      content = std::move(m);
      break;
    }
    case MessageContentType::InviteToGroupCall: {
      auto m = make_unique<MessageInviteToGroupCall>();
      parse(m->input_group_call_id, parser);
      parse(m->user_ids, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::ChatSetTheme: {
      auto m = make_unique<MessageChatSetTheme>();
      parse(m->emoji, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::WebViewDataSent: {
      auto m = make_unique<MessageWebViewDataSent>();
      parse(m->button_text, parser);
      content = std::move(m);
      break;
    }
    case MessageContentType::WebViewDataReceived: {
      auto m = make_unique<MessageWebViewDataReceived>();
      parse(m->button_text, parser);
      parse(m->data, parser);
      content = std::move(m);
      break;
    }
    default:
      LOG(FATAL) << "Have unknown message content type " << static_cast<int32>(content_type);
  }
  if (is_bad) {
    LOG(ERROR) << "Load a message with an invalid content of type " << content_type;
    content = make_unique<MessageUnsupported>(0);
  }
}

void store_message_content(const MessageContent *content, LogEventStorerCalcLength &storer) {
  store(content, storer);
}

void store_message_content(const MessageContent *content, LogEventStorerUnsafe &storer) {
  store(content, storer);
}

void parse_message_content(unique_ptr<MessageContent> &content, LogEventParser &parser) {
  parse(content, parser);
}

InlineMessageContent create_inline_message_content(Td *td, FileId file_id,
                                                   tl_object_ptr<telegram_api::BotInlineMessage> &&bot_inline_message,
                                                   int32 allowed_media_content_id, Photo *photo, Game *game) {
  CHECK(bot_inline_message != nullptr);
  CHECK((allowed_media_content_id == td_api::inputMessagePhoto::ID) == (photo != nullptr));
  CHECK((allowed_media_content_id == td_api::inputMessageGame::ID) == (game != nullptr));
  CHECK((allowed_media_content_id != td_api::inputMessagePhoto::ID &&
         allowed_media_content_id != td_api::inputMessageGame::ID && allowed_media_content_id != -1) ==
        file_id.is_valid());

  InlineMessageContent result;
  tl_object_ptr<telegram_api::ReplyMarkup> reply_markup;
  result.disable_web_page_preview = false;
  switch (bot_inline_message->get_id()) {
    case telegram_api::botInlineMessageText::ID: {
      auto inline_message = move_tl_object_as<telegram_api::botInlineMessageText>(bot_inline_message);
      auto entities = get_message_entities(td->contacts_manager_.get(), std::move(inline_message->entities_),
                                           "botInlineMessageText");
      auto status = fix_formatted_text(inline_message->message_, entities, false, true, true, false, false);
      if (status.is_error()) {
        LOG(ERROR) << "Receive error " << status << " while parsing botInlineMessageText " << inline_message->message_;
        break;
      }

      result.disable_web_page_preview = inline_message->no_webpage_;
      WebPageId web_page_id;
      if (!result.disable_web_page_preview) {
        web_page_id = td->web_pages_manager_->get_web_page_by_url(get_first_url(inline_message->message_, entities));
      }
      result.message_content = make_unique<MessageText>(
          FormattedText{std::move(inline_message->message_), std::move(entities)}, web_page_id);
      reply_markup = std::move(inline_message->reply_markup_);
      break;
    }
    case telegram_api::botInlineMessageMediaInvoice::ID: {
      auto inline_message = move_tl_object_as<telegram_api::botInlineMessageMediaInvoice>(bot_inline_message);
      reply_markup = std::move(inline_message->reply_markup_);
      result.message_content =
          make_unique<MessageInvoice>(get_input_invoice(std::move(inline_message), td, DialogId()));
      break;
    }
    case telegram_api::botInlineMessageMediaGeo::ID: {
      auto inline_message = move_tl_object_as<telegram_api::botInlineMessageMediaGeo>(bot_inline_message);
      if ((inline_message->flags_ & telegram_api::botInlineMessageMediaGeo::PERIOD_MASK) != 0 &&
          inline_message->period_ > 0) {
        auto heading = (inline_message->flags_ & telegram_api::botInlineMessageMediaGeo::HEADING_MASK) != 0
                           ? inline_message->heading_
                           : 0;
        auto approacing_notification_radius =
            (inline_message->flags_ & telegram_api::botInlineMessageMediaGeo::PROXIMITY_NOTIFICATION_RADIUS_MASK) != 0
                ? inline_message->proximity_notification_radius_
                : 0;
        result.message_content = make_unique<MessageLiveLocation>(
            Location(inline_message->geo_), inline_message->period_, heading, approacing_notification_radius);
      } else {
        result.message_content = make_unique<MessageLocation>(Location(inline_message->geo_));
      }
      reply_markup = std::move(inline_message->reply_markup_);
      break;
    }
    case telegram_api::botInlineMessageMediaVenue::ID: {
      auto inline_message = move_tl_object_as<telegram_api::botInlineMessageMediaVenue>(bot_inline_message);
      result.message_content = make_unique<MessageVenue>(
          Venue(inline_message->geo_, std::move(inline_message->title_), std::move(inline_message->address_),
                std::move(inline_message->provider_), std::move(inline_message->venue_id_),
                std::move(inline_message->venue_type_)));
      reply_markup = std::move(inline_message->reply_markup_);
      break;
    }
    case telegram_api::botInlineMessageMediaContact::ID: {
      auto inline_message = move_tl_object_as<telegram_api::botInlineMessageMediaContact>(bot_inline_message);
      result.message_content = make_unique<MessageContact>(
          Contact(std::move(inline_message->phone_number_), std::move(inline_message->first_name_),
                  std::move(inline_message->last_name_), std::move(inline_message->vcard_), UserId()));
      reply_markup = std::move(inline_message->reply_markup_);
      break;
    }
    case telegram_api::botInlineMessageMediaAuto::ID: {
      auto inline_message = move_tl_object_as<telegram_api::botInlineMessageMediaAuto>(bot_inline_message);
      auto caption =
          get_message_text(td->contacts_manager_.get(), inline_message->message_, std::move(inline_message->entities_),
                           true, false, 0, false, "create_inline_message_content");
      if (allowed_media_content_id == td_api::inputMessageAnimation::ID) {
        result.message_content = make_unique<MessageAnimation>(file_id, std::move(caption));
      } else if (allowed_media_content_id == td_api::inputMessageAudio::ID) {
        result.message_content = make_unique<MessageAudio>(file_id, std::move(caption));
      } else if (allowed_media_content_id == td_api::inputMessageDocument::ID) {
        result.message_content = make_unique<MessageDocument>(file_id, std::move(caption));
      } else if (allowed_media_content_id == td_api::inputMessageGame::ID) {
        CHECK(game != nullptr);
        // TODO game->set_short_name(std::move(caption));
        result.message_content = make_unique<MessageGame>(std::move(*game));
      } else if (allowed_media_content_id == td_api::inputMessagePhoto::ID) {
        result.message_content = make_unique<MessagePhoto>(std::move(*photo), std::move(caption));
      } else if (allowed_media_content_id == td_api::inputMessageSticker::ID) {
        result.message_content = make_unique<MessageSticker>(file_id, false);
      } else if (allowed_media_content_id == td_api::inputMessageVideo::ID) {
        result.message_content = make_unique<MessageVideo>(file_id, std::move(caption));
      } else if (allowed_media_content_id == td_api::inputMessageVoiceNote::ID) {
        result.message_content = make_unique<MessageVoiceNote>(file_id, std::move(caption), true);
      } else {
        LOG(WARNING) << "Unallowed bot inline message " << to_string(inline_message);
      }

      reply_markup = std::move(inline_message->reply_markup_);
      break;
    }
    default:
      UNREACHABLE();
  }
  result.message_reply_markup = get_reply_markup(std::move(reply_markup), td->auth_manager_->is_bot(), true, false);
  return result;
}

unique_ptr<MessageContent> create_text_message_content(string text, vector<MessageEntity> entities,
                                                       WebPageId web_page_id) {
  return make_unique<MessageText>(FormattedText{std::move(text), std::move(entities)}, web_page_id);
}

unique_ptr<MessageContent> create_contact_registered_message_content() {
  return make_unique<MessageContactRegistered>();
}

unique_ptr<MessageContent> create_screenshot_taken_message_content() {
  return make_unique<MessageScreenshotTaken>();
}

unique_ptr<MessageContent> create_chat_set_ttl_message_content(int32 ttl) {
  return make_unique<MessageChatSetTtl>(ttl);
}

static Result<InputMessageContent> create_input_message_content(
    DialogId dialog_id, tl_object_ptr<td_api::InputMessageContent> &&input_message_content, Td *td,
    FormattedText caption, FileId file_id, PhotoSize thumbnail, vector<FileId> sticker_file_ids, bool is_premium) {
  CHECK(input_message_content != nullptr);
  LOG(INFO) << "Create InputMessageContent with file " << file_id << " and thumbnail " << thumbnail.file_id;

  FileView file_view;
  string file_name;
  string mime_type;
  if (file_id.is_valid()) {
    file_view = td->file_manager_->get_file_view(file_id);
    auto suggested_path = file_view.suggested_path();
    const PathView path_view(suggested_path);
    file_name = path_view.file_name().str();
    mime_type = MimeType::from_extension(path_view.extension());
  }

  bool disable_web_page_preview = false;
  bool clear_draft = false;
  unique_ptr<MessageContent> content;
  UserId via_bot_user_id;
  int32 ttl = 0;
  string emoji;
  bool is_bot = td->auth_manager_->is_bot();
  switch (input_message_content->get_id()) {
    case td_api::inputMessageText::ID: {
      TRY_RESULT(input_message_text, process_input_message_text(td->contacts_manager_.get(), dialog_id,
                                                                std::move(input_message_content), is_bot));
      disable_web_page_preview = input_message_text.disable_web_page_preview;
      clear_draft = input_message_text.clear_draft;

      WebPageId web_page_id;
      bool can_add_web_page_previews =
          dialog_id.get_type() != DialogType::Channel ||
          td->contacts_manager_->get_channel_permissions(dialog_id.get_channel_id()).can_add_web_page_previews();
      if (!is_bot && !disable_web_page_preview && can_add_web_page_previews) {
        web_page_id = td->web_pages_manager_->get_web_page_by_url(
            get_first_url(input_message_text.text.text, input_message_text.text.entities));
      }
      content = make_unique<MessageText>(std::move(input_message_text.text), web_page_id);
      break;
    }
    case td_api::inputMessageAnimation::ID: {
      auto input_animation = static_cast<td_api::inputMessageAnimation *>(input_message_content.get());

      bool has_stickers = !sticker_file_ids.empty();
      td->animations_manager_->create_animation(
          file_id, string(), thumbnail, AnimationSize(), has_stickers, std::move(sticker_file_ids),
          std::move(file_name), std::move(mime_type), input_animation->duration_,
          get_dimensions(input_animation->width_, input_animation->height_, nullptr), false);

      content = make_unique<MessageAnimation>(file_id, std::move(caption));
      break;
    }
    case td_api::inputMessageAudio::ID: {
      auto input_audio = static_cast<td_api::inputMessageAudio *>(input_message_content.get());

      if (!clean_input_string(input_audio->title_)) {
        return Status::Error(400, "Audio title must be encoded in UTF-8");
      }
      if (!clean_input_string(input_audio->performer_)) {
        return Status::Error(400, "Audio performer must be encoded in UTF-8");
      }

      td->audios_manager_->create_audio(file_id, string(), thumbnail, std::move(file_name), std::move(mime_type),
                                        input_audio->duration_, std::move(input_audio->title_),
                                        std::move(input_audio->performer_), 0, false);

      content = make_unique<MessageAudio>(file_id, std::move(caption));
      break;
    }
    case td_api::inputMessageDice::ID: {
      auto input_dice = static_cast<td_api::inputMessageDice *>(input_message_content.get());
      if (!clean_input_string(input_dice->emoji_)) {
        return Status::Error(400, "Dice emoji must be encoded in UTF-8");
      }
      content = td::make_unique<MessageDice>(input_dice->emoji_, 0);
      clear_draft = input_dice->clear_draft_;
      break;
    }
    case td_api::inputMessageDocument::ID:
      td->documents_manager_->create_document(file_id, string(), thumbnail, std::move(file_name), std::move(mime_type),
                                              false);

      content = make_unique<MessageDocument>(file_id, std::move(caption));
      break;
    case td_api::inputMessagePhoto::ID: {
      auto input_photo = static_cast<td_api::inputMessagePhoto *>(input_message_content.get());

      if (input_photo->width_ < 0 || input_photo->width_ > 10000) {
        return Status::Error(400, "Wrong photo width");
      }
      if (input_photo->height_ < 0 || input_photo->height_ > 10000) {
        return Status::Error(400, "Wrong photo height");
      }
      ttl = input_photo->ttl_;

      auto message_photo = make_unique<MessagePhoto>();

      if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
        message_photo->photo.id = file_view.remote_location().get_id();
      }
      if (message_photo->photo.is_empty()) {
        message_photo->photo.id = 0;
      }
      message_photo->photo.date = G()->unix_time();
      int32 type = 'i';
      if (file_view.has_remote_location() && !file_view.remote_location().is_web()) {
        auto photo_size_source = file_view.remote_location().get_source();
        if (photo_size_source.get_type("create_input_message_content") == PhotoSizeSource::Type::Thumbnail) {
          auto old_type = photo_size_source.thumbnail().thumbnail_type;
          if (old_type != 't') {
            type = old_type;
          }
        }
      }

      PhotoSize s;
      s.type = type;
      s.dimensions = get_dimensions(input_photo->width_, input_photo->height_, nullptr);
      auto size = file_view.size();
      if (size < 0 || size >= 1000000000) {
        return Status::Error(400, "Wrong photo size");
      }
      s.size = static_cast<int32>(size);
      s.file_id = file_id;

      if (thumbnail.file_id.is_valid()) {
        message_photo->photo.photos.push_back(std::move(thumbnail));
      }

      message_photo->photo.photos.push_back(s);

      message_photo->photo.has_stickers = !sticker_file_ids.empty();
      message_photo->photo.sticker_file_ids = std::move(sticker_file_ids);

      message_photo->caption = std::move(caption);

      content = std::move(message_photo);
      break;
    }
    case td_api::inputMessageSticker::ID: {
      auto input_sticker = static_cast<td_api::inputMessageSticker *>(input_message_content.get());

      emoji = std::move(input_sticker->emoji_);

      td->stickers_manager_->create_sticker(file_id, FileId(), string(), thumbnail,
                                            get_dimensions(input_sticker->width_, input_sticker->height_, nullptr),
                                            nullptr, StickerFormat::Unknown, nullptr);

      content = make_unique<MessageSticker>(file_id, is_premium);
      break;
    }
    case td_api::inputMessageVideo::ID: {
      auto input_video = static_cast<td_api::inputMessageVideo *>(input_message_content.get());

      ttl = input_video->ttl_;

      bool has_stickers = !sticker_file_ids.empty();
      td->videos_manager_->create_video(
          file_id, string(), thumbnail, AnimationSize(), has_stickers, std::move(sticker_file_ids),
          std::move(file_name), std::move(mime_type), input_video->duration_,
          get_dimensions(input_video->width_, input_video->height_, nullptr), input_video->supports_streaming_, false);

      content = make_unique<MessageVideo>(file_id, std::move(caption));
      break;
    }
    case td_api::inputMessageVideoNote::ID: {
      auto input_video_note = static_cast<td_api::inputMessageVideoNote *>(input_message_content.get());

      auto length = input_video_note->length_;
      if (length < 0 || length >= 640) {
        return Status::Error(400, "Wrong video note length");
      }

      td->video_notes_manager_->create_video_note(file_id, string(), thumbnail, input_video_note->duration_,
                                                  get_dimensions(length, length, nullptr), false);

      content = make_unique<MessageVideoNote>(file_id, false);
      break;
    }
    case td_api::inputMessageVoiceNote::ID: {
      auto input_voice_note = static_cast<td_api::inputMessageVoiceNote *>(input_message_content.get());

      td->voice_notes_manager_->create_voice_note(file_id, std::move(mime_type), input_voice_note->duration_,
                                                  std::move(input_voice_note->waveform_), false);

      content = make_unique<MessageVoiceNote>(file_id, std::move(caption), false);
      break;
    }
    case td_api::inputMessageLocation::ID: {
      TRY_RESULT(location, process_input_message_location(std::move(input_message_content)));
      if (location.live_period == 0) {
        content = make_unique<MessageLocation>(std::move(location.location));
      } else {
        content = make_unique<MessageLiveLocation>(std::move(location.location), location.live_period, location.heading,
                                                   location.proximity_alert_radius);
      }
      break;
    }
    case td_api::inputMessageVenue::ID: {
      TRY_RESULT(venue, process_input_message_venue(std::move(input_message_content)));
      content = make_unique<MessageVenue>(std::move(venue));
      break;
    }
    case td_api::inputMessageContact::ID: {
      TRY_RESULT(contact, process_input_message_contact(std::move(input_message_content)));
      content = make_unique<MessageContact>(std::move(contact));
      break;
    }
    case td_api::inputMessageGame::ID: {
      TRY_RESULT(game, process_input_message_game(td->contacts_manager_.get(), std::move(input_message_content)));
      via_bot_user_id = game.get_bot_user_id();
      if (via_bot_user_id == td->contacts_manager_->get_my_id()) {
        via_bot_user_id = UserId();
      }

      content = make_unique<MessageGame>(std::move(game));
      break;
    }
    case td_api::inputMessageInvoice::ID: {
      if (!is_bot) {
        return Status::Error(400, "Invoices can be sent only by bots");
      }

      TRY_RESULT(input_invoice, process_input_message_invoice(std::move(input_message_content), td));
      content = make_unique<MessageInvoice>(std::move(input_invoice));
      break;
    }
    case td_api::inputMessagePoll::ID: {
      const size_t MAX_POLL_QUESTION_LENGTH = is_bot ? 300 : 255;  // server-side limit
      constexpr size_t MAX_POLL_OPTION_LENGTH = 100;               // server-side limit
      constexpr size_t MAX_POLL_OPTIONS = 10;                      // server-side limit
      auto input_poll = static_cast<td_api::inputMessagePoll *>(input_message_content.get());
      if (!clean_input_string(input_poll->question_)) {
        return Status::Error(400, "Poll question must be encoded in UTF-8");
      }
      if (input_poll->question_.empty()) {
        return Status::Error(400, "Poll question must be non-empty");
      }
      if (utf8_length(input_poll->question_) > MAX_POLL_QUESTION_LENGTH) {
        return Status::Error(400, PSLICE() << "Poll question length must not exceed " << MAX_POLL_QUESTION_LENGTH);
      }
      if (input_poll->options_.size() <= 1) {
        return Status::Error(400, "Poll must have at least 2 option");
      }
      if (input_poll->options_.size() > MAX_POLL_OPTIONS) {
        return Status::Error(400, PSLICE() << "Poll can't have more than " << MAX_POLL_OPTIONS << " options");
      }
      for (auto &option : input_poll->options_) {
        if (!clean_input_string(option)) {
          return Status::Error(400, "Poll options must be encoded in UTF-8");
        }
        if (option.empty()) {
          return Status::Error(400, "Poll options must be non-empty");
        }
        if (utf8_length(option) > MAX_POLL_OPTION_LENGTH) {
          return Status::Error(400, PSLICE() << "Poll options length must not exceed " << MAX_POLL_OPTION_LENGTH);
        }
      }

      bool allow_multiple_answers = false;
      bool is_quiz = false;
      int32 correct_option_id = -1;
      FormattedText explanation;
      if (input_poll->type_ == nullptr) {
        return Status::Error(400, "Poll type must be non-empty");
      }
      switch (input_poll->type_->get_id()) {
        case td_api::pollTypeRegular::ID: {
          auto type = td_api::move_object_as<td_api::pollTypeRegular>(input_poll->type_);
          allow_multiple_answers = type->allow_multiple_answers_;
          break;
        }
        case td_api::pollTypeQuiz::ID: {
          auto type = td_api::move_object_as<td_api::pollTypeQuiz>(input_poll->type_);
          is_quiz = true;
          correct_option_id = type->correct_option_id_;
          if (correct_option_id < 0 || correct_option_id >= static_cast<int32>(input_poll->options_.size())) {
            return Status::Error(400, "Wrong correct option ID specified");
          }
          auto r_explanation =
              process_input_caption(td->contacts_manager_.get(), dialog_id, std::move(type->explanation_), is_bot);
          if (r_explanation.is_error()) {
            return r_explanation.move_as_error();
          }
          explanation = r_explanation.move_as_ok();
          break;
        }
        default:
          UNREACHABLE();
      }

      int32 open_period = is_bot ? input_poll->open_period_ : 0;
      int32 close_date = is_bot ? input_poll->close_date_ : 0;
      if (open_period != 0) {
        close_date = 0;
      }
      bool is_closed = is_bot ? input_poll->is_closed_ : false;
      content = make_unique<MessagePoll>(
          td->poll_manager_->create_poll(std::move(input_poll->question_), std::move(input_poll->options_),
                                         input_poll->is_anonymous_, allow_multiple_answers, is_quiz, correct_option_id,
                                         std::move(explanation), open_period, close_date, is_closed));
      break;
    }
    default:
      UNREACHABLE();
  }
  return InputMessageContent{std::move(content), disable_web_page_preview, clear_draft, ttl,
                             via_bot_user_id,    std::move(emoji)};
}

Result<InputMessageContent> get_input_message_content(
    DialogId dialog_id, tl_object_ptr<td_api::InputMessageContent> &&input_message_content, Td *td, bool is_premium) {
  bool is_secret = dialog_id.get_type() == DialogType::SecretChat;

  LOG(INFO) << "Get input message content from " << to_string(input_message_content);

  bool have_file = true;
  // TODO: send from secret chat to common
  Result<FileId> r_file_id = Status::Error(500, "Have no file");
  tl_object_ptr<td_api::inputThumbnail> input_thumbnail;
  vector<FileId> sticker_file_ids;
  switch (input_message_content->get_id()) {
    case td_api::inputMessageAnimation::ID: {
      auto input_message = static_cast<td_api::inputMessageAnimation *>(input_message_content.get());
      r_file_id = td->file_manager_->get_input_file_id(FileType::Animation, input_message->animation_, dialog_id, false,
                                                       is_secret, true);
      input_thumbnail = std::move(input_message->thumbnail_);
      if (!input_message->added_sticker_file_ids_.empty()) {
        sticker_file_ids = td->stickers_manager_->get_attached_sticker_file_ids(input_message->added_sticker_file_ids_);
      }
      break;
    }
    case td_api::inputMessageAudio::ID: {
      auto input_message = static_cast<td_api::inputMessageAudio *>(input_message_content.get());
      r_file_id =
          td->file_manager_->get_input_file_id(FileType::Audio, input_message->audio_, dialog_id, false, is_secret);
      input_thumbnail = std::move(input_message->album_cover_thumbnail_);
      break;
    }
    case td_api::inputMessageDocument::ID: {
      auto input_message = static_cast<td_api::inputMessageDocument *>(input_message_content.get());
      auto file_type = input_message->disable_content_type_detection_ ? FileType::DocumentAsFile : FileType::Document;
      r_file_id =
          td->file_manager_->get_input_file_id(file_type, input_message->document_, dialog_id, false, is_secret, true);
      input_thumbnail = std::move(input_message->thumbnail_);
      break;
    }
    case td_api::inputMessagePhoto::ID: {
      auto input_message = static_cast<td_api::inputMessagePhoto *>(input_message_content.get());
      r_file_id =
          td->file_manager_->get_input_file_id(FileType::Photo, input_message->photo_, dialog_id, false, is_secret);
      input_thumbnail = std::move(input_message->thumbnail_);
      if (!input_message->added_sticker_file_ids_.empty()) {
        sticker_file_ids = td->stickers_manager_->get_attached_sticker_file_ids(input_message->added_sticker_file_ids_);
      }
      break;
    }
    case td_api::inputMessageSticker::ID: {
      auto input_message = static_cast<td_api::inputMessageSticker *>(input_message_content.get());
      r_file_id =
          td->file_manager_->get_input_file_id(FileType::Sticker, input_message->sticker_, dialog_id, false, is_secret);
      input_thumbnail = std::move(input_message->thumbnail_);
      break;
    }
    case td_api::inputMessageVideo::ID: {
      auto input_message = static_cast<td_api::inputMessageVideo *>(input_message_content.get());
      r_file_id =
          td->file_manager_->get_input_file_id(FileType::Video, input_message->video_, dialog_id, false, is_secret);
      input_thumbnail = std::move(input_message->thumbnail_);
      if (!input_message->added_sticker_file_ids_.empty()) {
        sticker_file_ids = td->stickers_manager_->get_attached_sticker_file_ids(input_message->added_sticker_file_ids_);
      }
      break;
    }
    case td_api::inputMessageVideoNote::ID: {
      auto input_message = static_cast<td_api::inputMessageVideoNote *>(input_message_content.get());
      r_file_id = td->file_manager_->get_input_file_id(FileType::VideoNote, input_message->video_note_, dialog_id,
                                                       false, is_secret);
      input_thumbnail = std::move(input_message->thumbnail_);
      break;
    }
    case td_api::inputMessageVoiceNote::ID: {
      auto input_message = static_cast<td_api::inputMessageVoiceNote *>(input_message_content.get());
      r_file_id = td->file_manager_->get_input_file_id(FileType::VoiceNote, input_message->voice_note_, dialog_id,
                                                       false, is_secret);
      break;
    }
    default:
      have_file = false;
      break;
  }
  // TODO is path of files must be stored in bytes instead of UTF-8 string?

  FileId file_id;
  if (have_file) {
    if (r_file_id.is_error()) {
      return Status::Error(400, r_file_id.error().message());
    }
    file_id = r_file_id.ok();
    CHECK(file_id.is_valid());
  }

  PhotoSize thumbnail;
  if (input_thumbnail != nullptr) {
    auto r_thumbnail_file_id =
        td->file_manager_->get_input_thumbnail_file_id(input_thumbnail->thumbnail_, dialog_id, is_secret);
    if (r_thumbnail_file_id.is_error()) {
      LOG(WARNING) << "Ignore thumbnail file: " << r_thumbnail_file_id.error().message();
    } else {
      thumbnail.type = 't';
      thumbnail.dimensions = get_dimensions(input_thumbnail->width_, input_thumbnail->height_, nullptr);
      thumbnail.file_id = r_thumbnail_file_id.ok();
      CHECK(thumbnail.file_id.is_valid());

      FileView thumbnail_file_view = td->file_manager_->get_file_view(thumbnail.file_id);
      if (thumbnail_file_view.has_remote_location()) {
        // TODO td->file_manager_->delete_remote_location(thumbnail.file_id);
      }
    }
  }

  TRY_RESULT(caption, process_input_caption(td->contacts_manager_.get(), dialog_id,
                                            extract_input_caption(input_message_content), td->auth_manager_->is_bot()));
  return create_input_message_content(dialog_id, std::move(input_message_content), td, std::move(caption), file_id,
                                      std::move(thumbnail), std::move(sticker_file_ids), is_premium);
}

bool can_have_input_media(const Td *td, const MessageContent *content, bool is_server) {
  switch (content->get_type()) {
    case MessageContentType::Game:
      return is_server || static_cast<const MessageGame *>(content)->game.has_input_media();
    case MessageContentType::Poll:
      return td->poll_manager_->has_input_media(static_cast<const MessagePoll *>(content)->poll_id);
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
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
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
      return false;
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Contact:
    case MessageContentType::Dice:
    case MessageContentType::Document:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Photo:
    case MessageContentType::Sticker:
    case MessageContentType::Text:
    case MessageContentType::Venue:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
      return true;
    default:
      UNREACHABLE();
  }
}

SecretInputMedia get_secret_input_media(const MessageContent *content, Td *td,
                                        tl_object_ptr<telegram_api::InputEncryptedFile> input_file,
                                        BufferSlice thumbnail, int32 layer) {
  switch (content->get_type()) {
    case MessageContentType::Animation: {
      const auto *m = static_cast<const MessageAnimation *>(content);
      return td->animations_manager_->get_secret_input_media(m->file_id, std::move(input_file), m->caption.text,
                                                             std::move(thumbnail), layer);
    }
    case MessageContentType::Audio: {
      const auto *m = static_cast<const MessageAudio *>(content);
      return td->audios_manager_->get_secret_input_media(m->file_id, std::move(input_file), m->caption.text,
                                                         std::move(thumbnail), layer);
    }
    case MessageContentType::Contact: {
      const auto *m = static_cast<const MessageContact *>(content);
      return m->contact.get_secret_input_media_contact();
    }
    case MessageContentType::Document: {
      const auto *m = static_cast<const MessageDocument *>(content);
      return td->documents_manager_->get_secret_input_media(m->file_id, std::move(input_file), m->caption.text,
                                                            std::move(thumbnail), layer);
    }
    case MessageContentType::Location: {
      const auto *m = static_cast<const MessageLocation *>(content);
      return m->location.get_secret_input_media_geo_point();
    }
    case MessageContentType::Photo: {
      const auto *m = static_cast<const MessagePhoto *>(content);
      return photo_get_secret_input_media(td->file_manager_.get(), m->photo, std::move(input_file), m->caption.text,
                                          std::move(thumbnail));
    }
    case MessageContentType::Sticker: {
      const auto *m = static_cast<const MessageSticker *>(content);
      return td->stickers_manager_->get_secret_input_media(m->file_id, std::move(input_file), std::move(thumbnail),
                                                           layer);
    }
    case MessageContentType::Text: {
      CHECK(input_file == nullptr);
      CHECK(thumbnail.empty());
      const auto *m = static_cast<const MessageText *>(content);
      return td->web_pages_manager_->get_secret_input_media(m->web_page_id);
    }
    case MessageContentType::Venue: {
      const auto *m = static_cast<const MessageVenue *>(content);
      return m->venue.get_secret_input_media_venue();
    }
    case MessageContentType::Video: {
      const auto *m = static_cast<const MessageVideo *>(content);
      return td->videos_manager_->get_secret_input_media(m->file_id, std::move(input_file), m->caption.text,
                                                         std::move(thumbnail), layer);
    }
    case MessageContentType::VideoNote: {
      const auto *m = static_cast<const MessageVideoNote *>(content);
      return td->video_notes_manager_->get_secret_input_media(m->file_id, std::move(input_file), std::move(thumbnail),
                                                              layer);
    }
    case MessageContentType::VoiceNote: {
      const auto *m = static_cast<const MessageVoiceNote *>(content);
      return td->voice_notes_manager_->get_secret_input_media(m->file_id, std::move(input_file), m->caption.text,
                                                              layer);
    }
    case MessageContentType::Call:
    case MessageContentType::Dice:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Poll:
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
    case MessageContentType::PaymentSuccessful:
    case MessageContentType::ContactRegistered:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
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
      break;
    default:
      UNREACHABLE();
  }
  return SecretInputMedia{};
}

static tl_object_ptr<telegram_api::InputMedia> get_input_media_impl(
    const MessageContent *content, Td *td, tl_object_ptr<telegram_api::InputFile> input_file,
    tl_object_ptr<telegram_api::InputFile> input_thumbnail, int32 ttl, const string &emoji) {
  if (!can_have_input_media(td, content, false)) {
    return nullptr;
  }
  switch (content->get_type()) {
    case MessageContentType::Animation: {
      const auto *m = static_cast<const MessageAnimation *>(content);
      return td->animations_manager_->get_input_media(m->file_id, std::move(input_file), std::move(input_thumbnail));
    }
    case MessageContentType::Audio: {
      const auto *m = static_cast<const MessageAudio *>(content);
      return td->audios_manager_->get_input_media(m->file_id, std::move(input_file), std::move(input_thumbnail));
    }
    case MessageContentType::Contact: {
      const auto *m = static_cast<const MessageContact *>(content);
      return m->contact.get_input_media_contact();
    }
    case MessageContentType::Dice: {
      const auto *m = static_cast<const MessageDice *>(content);
      return make_tl_object<telegram_api::inputMediaDice>(m->emoji);
    }
    case MessageContentType::Document: {
      const auto *m = static_cast<const MessageDocument *>(content);
      return td->documents_manager_->get_input_media(m->file_id, std::move(input_file), std::move(input_thumbnail));
    }
    case MessageContentType::Game: {
      const auto *m = static_cast<const MessageGame *>(content);
      return m->game.get_input_media_game(td);
    }
    case MessageContentType::Invoice: {
      const auto *m = static_cast<const MessageInvoice *>(content);
      return get_input_media_invoice(m->input_invoice, td);
    }
    case MessageContentType::LiveLocation: {
      const auto *m = static_cast<const MessageLiveLocation *>(content);
      int32 flags = telegram_api::inputMediaGeoLive::PERIOD_MASK;
      if (m->heading != 0) {
        flags |= telegram_api::inputMediaGeoLive::HEADING_MASK;
      }
      flags |= telegram_api::inputMediaGeoLive::PROXIMITY_NOTIFICATION_RADIUS_MASK;
      return make_tl_object<telegram_api::inputMediaGeoLive>(flags, false /*ignored*/,
                                                             m->location.get_input_geo_point(), m->heading, m->period,
                                                             m->proximity_alert_radius);
    }
    case MessageContentType::Location: {
      const auto *m = static_cast<const MessageLocation *>(content);
      return m->location.get_input_media_geo_point();
    }
    case MessageContentType::Photo: {
      const auto *m = static_cast<const MessagePhoto *>(content);
      return photo_get_input_media(td->file_manager_.get(), m->photo, std::move(input_file), ttl);
    }
    case MessageContentType::Poll: {
      const auto *m = static_cast<const MessagePoll *>(content);
      return td->poll_manager_->get_input_media(m->poll_id);
    }
    case MessageContentType::Sticker: {
      const auto *m = static_cast<const MessageSticker *>(content);
      return td->stickers_manager_->get_input_media(m->file_id, std::move(input_file), std::move(input_thumbnail),
                                                    emoji);
    }
    case MessageContentType::Venue: {
      const auto *m = static_cast<const MessageVenue *>(content);
      return m->venue.get_input_media_venue();
    }
    case MessageContentType::Video: {
      const auto *m = static_cast<const MessageVideo *>(content);
      return td->videos_manager_->get_input_media(m->file_id, std::move(input_file), std::move(input_thumbnail), ttl);
    }
    case MessageContentType::VideoNote: {
      const auto *m = static_cast<const MessageVideoNote *>(content);
      return td->video_notes_manager_->get_input_media(m->file_id, std::move(input_file), std::move(input_thumbnail));
    }
    case MessageContentType::VoiceNote: {
      const auto *m = static_cast<const MessageVoiceNote *>(content);
      return td->voice_notes_manager_->get_input_media(m->file_id, std::move(input_file));
    }
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
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
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
      break;
    default:
      UNREACHABLE();
  }
  return nullptr;
}

tl_object_ptr<telegram_api::InputMedia> get_input_media(const MessageContent *content, Td *td,
                                                        tl_object_ptr<telegram_api::InputFile> input_file,
                                                        tl_object_ptr<telegram_api::InputFile> input_thumbnail,
                                                        FileId file_id, FileId thumbnail_file_id, int32 ttl,
                                                        bool force) {
  bool had_input_file = input_file != nullptr;
  bool had_input_thumbnail = input_thumbnail != nullptr;
  auto input_media =
      get_input_media_impl(content, td, std::move(input_file), std::move(input_thumbnail), ttl, string());
  auto was_uploaded = FileManager::extract_was_uploaded(input_media);
  if (had_input_file) {
    if (!was_uploaded) {
      // if we had InputFile, but has failed to use it, then we need to immediately cancel file upload
      // so the next upload with the same file can succeed
      CHECK(file_id.is_valid());
      td->file_manager_->cancel_upload(file_id);
      if (had_input_thumbnail) {
        CHECK(thumbnail_file_id.is_valid());
        td->file_manager_->cancel_upload(thumbnail_file_id);
      }
    }
  } else {
    CHECK(!had_input_thumbnail);
  }
  if (!was_uploaded) {
    auto file_reference = FileManager::extract_file_reference(input_media);
    if (file_reference == FileReferenceView::invalid_file_reference()) {
      if (!force) {
        LOG(INFO) << "File " << file_id << " has invalid file reference";
        return nullptr;
      }
      LOG(ERROR) << "File " << file_id << " has invalid file reference, but we forced to use it";
    }
  }
  return input_media;
}

tl_object_ptr<telegram_api::InputMedia> get_input_media(const MessageContent *content, Td *td, int32 ttl,
                                                        const string &emoji, bool force) {
  auto input_media = get_input_media_impl(content, td, nullptr, nullptr, ttl, emoji);
  auto file_reference = FileManager::extract_file_reference(input_media);
  if (file_reference == FileReferenceView::invalid_file_reference()) {
    auto file_id = get_message_content_any_file_id(content);
    if (!force) {
      LOG(INFO) << "File " << file_id << " has invalid file reference";
      return nullptr;
    }
    LOG(ERROR) << "File " << file_id << " has invalid file reference, but we forced to use it";
  }
  return input_media;
}

tl_object_ptr<telegram_api::InputMedia> get_fake_input_media(Td *td, tl_object_ptr<telegram_api::InputFile> input_file,
                                                             FileId file_id) {
  FileView file_view = td->file_manager_->get_file_view(file_id);
  auto file_type = file_view.get_type();
  if (is_document_file_type(file_type)) {
    vector<tl_object_ptr<telegram_api::DocumentAttribute>> attributes;
    auto file_path = file_view.suggested_path();
    const PathView path_view(file_path);
    Slice file_name = path_view.file_name();
    if (!file_name.empty()) {
      attributes.push_back(make_tl_object<telegram_api::documentAttributeFilename>(file_name.str()));
    }
    string mime_type = MimeType::from_extension(path_view.extension());
    int32 flags = 0;
    if (file_type == FileType::Video) {
      flags |= telegram_api::inputMediaUploadedDocument::NOSOUND_VIDEO_MASK;
    }
    if (file_type == FileType::DocumentAsFile) {
      flags |= telegram_api::inputMediaUploadedDocument::FORCE_FILE_MASK;
    }
    return make_tl_object<telegram_api::inputMediaUploadedDocument>(
        flags, false /*ignored*/, false /*ignored*/, std::move(input_file), nullptr, mime_type, std::move(attributes),
        vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  } else {
    CHECK(file_type == FileType::Photo);
    return make_tl_object<telegram_api::inputMediaUploadedPhoto>(
        0, std::move(input_file), vector<tl_object_ptr<telegram_api::InputDocument>>(), 0);
  }
}

void delete_message_content_thumbnail(MessageContent *content, Td *td) {
  switch (content->get_type()) {
    case MessageContentType::Animation: {
      auto *m = static_cast<MessageAnimation *>(content);
      return td->animations_manager_->delete_animation_thumbnail(m->file_id);
    }
    case MessageContentType::Audio: {
      auto *m = static_cast<MessageAudio *>(content);
      return td->audios_manager_->delete_audio_thumbnail(m->file_id);
    }
    case MessageContentType::Document: {
      auto *m = static_cast<MessageDocument *>(content);
      return td->documents_manager_->delete_document_thumbnail(m->file_id);
    }
    case MessageContentType::Photo: {
      auto *m = static_cast<MessagePhoto *>(content);
      return photo_delete_thumbnail(m->photo);
    }
    case MessageContentType::Sticker: {
      auto *m = static_cast<MessageSticker *>(content);
      return td->stickers_manager_->delete_sticker_thumbnail(m->file_id);
    }
    case MessageContentType::Video: {
      auto *m = static_cast<MessageVideo *>(content);
      return td->videos_manager_->delete_video_thumbnail(m->file_id);
    }
    case MessageContentType::VideoNote: {
      auto *m = static_cast<MessageVideoNote *>(content);
      return td->video_notes_manager_->delete_video_note_thumbnail(m->file_id);
    }
    case MessageContentType::Contact:
    case MessageContentType::Dice:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Venue:
    case MessageContentType::VoiceNote:
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
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
    case MessageContentType::CustomServiceAction:
    case MessageContentType::WebsiteConnected:
    case MessageContentType::PassportDataSent:
    case MessageContentType::PassportDataReceived:
    case MessageContentType::Poll:
    case MessageContentType::ProximityAlertTriggered:
    case MessageContentType::GroupCall:
    case MessageContentType::InviteToGroupCall:
    case MessageContentType::ChatSetTheme:
    case MessageContentType::WebViewDataSent:
    case MessageContentType::WebViewDataReceived:
      break;
    default:
      UNREACHABLE();
  }
}

Status can_send_message_content(DialogId dialog_id, const MessageContent *content, bool is_forward, const Td *td) {
  auto dialog_type = dialog_id.get_type();
  RestrictedRights permissions = [&] {
    switch (dialog_type) {
      case DialogType::User:
        return td->contacts_manager_->get_user_default_permissions(dialog_id.get_user_id());
      case DialogType::Chat:
        return td->contacts_manager_->get_chat_permissions(dialog_id.get_chat_id()).get_effective_restricted_rights();
      case DialogType::Channel:
        return td->contacts_manager_->get_channel_permissions(dialog_id.get_channel_id())
            .get_effective_restricted_rights();
      case DialogType::SecretChat:
        return td->contacts_manager_->get_secret_chat_default_permissions(dialog_id.get_secret_chat_id());
      case DialogType::None:
      default:
        UNREACHABLE();
        return td->contacts_manager_->get_user_default_permissions(UserId());
    }
  }();

  auto content_type = content->get_type();
  switch (content_type) {
    case MessageContentType::Animation:
      if (!permissions.can_send_animations()) {
        return Status::Error(400, "Not enough rights to send animations to the chat");
      }
      break;
    case MessageContentType::Audio:
      if (!permissions.can_send_media()) {
        return Status::Error(400, "Not enough rights to send music to the chat");
      }
      break;
    case MessageContentType::Contact:
      if (!permissions.can_send_messages()) {
        return Status::Error(400, "Not enough rights to send contacts to the chat");
      }
      break;
    case MessageContentType::Dice:
      if (!permissions.can_send_stickers()) {
        return Status::Error(400, "Not enough rights to send dice to the chat");
      }
      if (dialog_type == DialogType::SecretChat) {
        return Status::Error(400, "Dice can't be sent to secret chats");
      }
      break;
    case MessageContentType::Document:
      if (!permissions.can_send_media()) {
        return Status::Error(400, "Not enough rights to send documents to the chat");
      }
      break;
    case MessageContentType::Game:
      if (dialog_type == DialogType::Channel &&
          td->contacts_manager_->is_broadcast_channel(dialog_id.get_channel_id())) {
        // return Status::Error(400, "Games can't be sent to channel chats");
      }
      if (dialog_type == DialogType::SecretChat) {
        return Status::Error(400, "Games can't be sent to secret chats");
      }
      if (!permissions.can_send_games()) {
        return Status::Error(400, "Not enough rights to send games to the chat");
      }
      break;
    case MessageContentType::Invoice:
      if (!permissions.can_send_messages()) {
        return Status::Error(400, "Not enough rights to send invoice messages to the chat");
      }
      if (dialog_type == DialogType::SecretChat) {
        return Status::Error(400, "Invoice messages can't be sent to secret chats");
      }
      break;
    case MessageContentType::LiveLocation:
      if (!permissions.can_send_messages()) {
        return Status::Error(400, "Not enough rights to send live locations to the chat");
      }
      break;
    case MessageContentType::Location:
      if (!permissions.can_send_messages()) {
        return Status::Error(400, "Not enough rights to send locations to the chat");
      }
      break;
    case MessageContentType::Photo:
      if (!permissions.can_send_media()) {
        return Status::Error(400, "Not enough rights to send photos to the chat");
      }
      break;
    case MessageContentType::Poll:
      if (!permissions.can_send_polls()) {
        return Status::Error(400, "Not enough rights to send polls to the chat");
      }
      if (dialog_type == DialogType::Channel &&
          td->contacts_manager_->is_broadcast_channel(dialog_id.get_channel_id()) &&
          !td->poll_manager_->get_poll_is_anonymous(static_cast<const MessagePoll *>(content)->poll_id)) {
        return Status::Error(400, "Non-anonymous polls can't be sent to channel chats");
      }
      if (dialog_type == DialogType::User && !is_forward && !td->auth_manager_->is_bot() &&
          !td->contacts_manager_->is_user_bot(dialog_id.get_user_id())) {
        return Status::Error(400, "Polls can't be sent to the private chat");
      }
      if (dialog_type == DialogType::SecretChat) {
        return Status::Error(400, "Polls can't be sent to secret chats");
      }
      break;
    case MessageContentType::Sticker:
      if (!permissions.can_send_stickers()) {
        return Status::Error(400, "Not enough rights to send stickers to the chat");
      }
      break;
    case MessageContentType::Text:
      if (!permissions.can_send_messages()) {
        return Status::Error(400, "Not enough rights to send text messages to the chat");
      }
      break;
    case MessageContentType::Venue:
      if (!permissions.can_send_messages()) {
        return Status::Error(400, "Not enough rights to send venues to the chat");
      }
      break;
    case MessageContentType::Video:
      if (!permissions.can_send_media()) {
        return Status::Error(400, "Not enough rights to send videos to the chat");
      }
      break;
    case MessageContentType::VideoNote:
      if (!permissions.can_send_media()) {
        return Status::Error(400, "Not enough rights to send video notes to the chat");
      }
      break;
    case MessageContentType::VoiceNote:
      if (!permissions.can_send_media()) {
        return Status::Error(400, "Not enough rights to send voice notes to the chat");
      }
      break;
    case MessageContentType::None:
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
    case MessageContentType::Unsupported:
    case MessageContentType::Call:
    case MessageContentType::PaymentSuccessful:
    case MessageContentType::ContactRegistered:
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
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
      UNREACHABLE();
  }
  return Status::OK();
}

bool can_forward_message_content(const MessageContent *content) {
  auto content_type = content->get_type();
  if (content_type == MessageContentType::Text) {
    auto *text = static_cast<const MessageText *>(content);
    return !is_empty_string(text->text.text);  // text can't be empty in the new message
  }
  if (content_type == MessageContentType::Poll) {
    auto *poll = static_cast<const MessagePoll *>(content);
    return !PollManager::is_local_poll_id(poll->poll_id);
  }

  return !is_service_message_content(content_type) && content_type != MessageContentType::Unsupported &&
         content_type != MessageContentType::ExpiredPhoto && content_type != MessageContentType::ExpiredVideo;
}

bool update_opened_message_content(MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::VideoNote: {
      auto video_note_content = static_cast<MessageVideoNote *>(content);
      if (video_note_content->is_viewed) {
        return false;
      }
      video_note_content->is_viewed = true;
      return true;
    }
    case MessageContentType::VoiceNote: {
      auto voice_note_content = static_cast<MessageVoiceNote *>(content);
      if (voice_note_content->is_listened) {
        return false;
      }
      voice_note_content->is_listened = true;
      return true;
    }
    default:
      return false;
  }
}

static int32 get_message_content_text_index_mask(const MessageContent *content) {
  const FormattedText *text = get_message_content_text(content);
  if (text == nullptr || content->get_type() == MessageContentType::Game) {
    return 0;
  }

  for (auto &entity : text->entities) {
    if (entity.type == MessageEntity::Type::Url || entity.type == MessageEntity::Type::EmailAddress ||
        entity.type == MessageEntity::Type::TextUrl) {
      return message_search_filter_index_mask(MessageSearchFilter::Url);
    }
  }
  return 0;
}

static int32 get_message_content_media_index_mask(const MessageContent *content, const Td *td, bool is_outgoing) {
  switch (content->get_type()) {
    case MessageContentType::Animation:
      return message_search_filter_index_mask(MessageSearchFilter::Animation);
    case MessageContentType::Audio:
      return message_search_filter_index_mask(MessageSearchFilter::Audio);
    case MessageContentType::Document:
      return message_search_filter_index_mask(MessageSearchFilter::Document);
    case MessageContentType::Photo:
      return message_search_filter_index_mask(MessageSearchFilter::Photo) |
             message_search_filter_index_mask(MessageSearchFilter::PhotoAndVideo);
    case MessageContentType::Video:
      return message_search_filter_index_mask(MessageSearchFilter::Video) |
             message_search_filter_index_mask(MessageSearchFilter::PhotoAndVideo);
    case MessageContentType::VideoNote:
      return message_search_filter_index_mask(MessageSearchFilter::VideoNote) |
             message_search_filter_index_mask(MessageSearchFilter::VoiceAndVideoNote);
    case MessageContentType::VoiceNote:
      return message_search_filter_index_mask(MessageSearchFilter::VoiceNote) |
             message_search_filter_index_mask(MessageSearchFilter::VoiceAndVideoNote);
    case MessageContentType::ChatChangePhoto:
      return message_search_filter_index_mask(MessageSearchFilter::ChatPhoto);
    case MessageContentType::Call: {
      int32 index_mask = message_search_filter_index_mask(MessageSearchFilter::Call);
      const auto *m = static_cast<const MessageCall *>(content);
      if (!is_outgoing &&
          (m->discard_reason == CallDiscardReason::Declined || m->discard_reason == CallDiscardReason::Missed)) {
        index_mask |= message_search_filter_index_mask(MessageSearchFilter::MissedCall);
      }
      return index_mask;
    }
    case MessageContentType::Text:
    case MessageContentType::Contact:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Sticker:
    case MessageContentType::Unsupported:
    case MessageContentType::Venue:
    case MessageContentType::ChatCreate:
    case MessageContentType::ChatChangeTitle:
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
      return 0;
    default:
      UNREACHABLE();
      return 0;
  }
  return 0;
}

int32 get_message_content_index_mask(const MessageContent *content, const Td *td, bool is_outgoing) {
  return get_message_content_text_index_mask(content) | get_message_content_media_index_mask(content, td, is_outgoing);
}

MessageId get_message_content_pinned_message_id(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::PinMessage:
      return static_cast<const MessagePinMessage *>(content)->message_id;
    default:
      return MessageId();
  }
}

string get_message_content_theme_name(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::ChatSetTheme:
      return static_cast<const MessageChatSetTheme *>(content)->emoji;
    default:
      return string();
  }
}

FullMessageId get_message_content_replied_message_id(DialogId dialog_id, const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::PinMessage:
      return {dialog_id, static_cast<const MessagePinMessage *>(content)->message_id};
    case MessageContentType::GameScore:
      return {dialog_id, static_cast<const MessageGameScore *>(content)->game_message_id};
    case MessageContentType::PaymentSuccessful: {
      auto *m = static_cast<const MessagePaymentSuccessful *>(content);
      if (!m->invoice_message_id.is_valid()) {
        return FullMessageId();
      }

      auto reply_in_dialog_id = m->invoice_dialog_id.is_valid() ? m->invoice_dialog_id : dialog_id;
      return {reply_in_dialog_id, m->invoice_message_id};
    }
    default:
      return FullMessageId();
  }
}

std::pair<InputGroupCallId, bool> get_message_content_group_call_info(const MessageContent *content) {
  CHECK(content->get_type() == MessageContentType::GroupCall);
  const auto *m = static_cast<const MessageGroupCall *>(content);
  return {m->input_group_call_id, m->duration >= 0};
}

vector<UserId> get_message_content_added_user_ids(const MessageContent *content) {
  CHECK(content->get_type() == MessageContentType::ChatAddUsers);
  return static_cast<const MessageChatAddUsers *>(content)->user_ids;
}

UserId get_message_content_deleted_user_id(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::ChatDeleteUser:
      return static_cast<const MessageChatDeleteUser *>(content)->user_id;
    default:
      return UserId();
  }
}

int32 get_message_content_live_location_period(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::LiveLocation:
      return static_cast<const MessageLiveLocation *>(content)->period;
    default:
      return 0;
  }
}

bool get_message_content_poll_is_anonymous(const Td *td, const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Poll:
      return td->poll_manager_->get_poll_is_anonymous(static_cast<const MessagePoll *>(content)->poll_id);
    default:
      return false;
  }
}

bool get_message_content_poll_is_closed(const Td *td, const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Poll:
      return td->poll_manager_->get_poll_is_closed(static_cast<const MessagePoll *>(content)->poll_id);
    default:
      return true;
  }
}

bool has_message_content_web_page(const MessageContent *content) {
  if (content->get_type() == MessageContentType::Text) {
    return static_cast<const MessageText *>(content)->web_page_id.is_valid();
  }
  return false;
}

void remove_message_content_web_page(MessageContent *content) {
  CHECK(content->get_type() == MessageContentType::Text);
  static_cast<MessageText *>(content)->web_page_id = WebPageId();
}

bool can_message_content_have_media_timestamp(const MessageContent *content) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case MessageContentType::Audio:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
      return true;
    default:
      return has_message_content_web_page(content);
  }
}

void set_message_content_poll_answer(Td *td, const MessageContent *content, FullMessageId full_message_id,
                                     vector<int32> &&option_ids, Promise<Unit> &&promise) {
  CHECK(content->get_type() == MessageContentType::Poll);
  td->poll_manager_->set_poll_answer(static_cast<const MessagePoll *>(content)->poll_id, full_message_id,
                                     std::move(option_ids), std::move(promise));
}

void get_message_content_poll_voters(Td *td, const MessageContent *content, FullMessageId full_message_id,
                                     int32 option_id, int32 offset, int32 limit,
                                     Promise<std::pair<int32, vector<UserId>>> &&promise) {
  CHECK(content->get_type() == MessageContentType::Poll);
  td->poll_manager_->get_poll_voters(static_cast<const MessagePoll *>(content)->poll_id, full_message_id, option_id,
                                     offset, limit, std::move(promise));
}

void stop_message_content_poll(Td *td, const MessageContent *content, FullMessageId full_message_id,
                               unique_ptr<ReplyMarkup> &&reply_markup, Promise<Unit> &&promise) {
  CHECK(content->get_type() == MessageContentType::Poll);
  td->poll_manager_->stop_poll(static_cast<const MessagePoll *>(content)->poll_id, full_message_id,
                               std::move(reply_markup), std::move(promise));
}

static void merge_location_access_hash(const Location &first, const Location &second) {
  if (second.get_access_hash() != 0) {
    first.set_access_hash(second.get_access_hash());
  } else {
    second.set_access_hash(first.get_access_hash());
  }
}

static bool need_message_text_changed_warning(const MessageText *old_content, const MessageText *new_content) {
  if (new_content->text.text == "Unsupported characters" ||
      new_content->text.text == "This channel is blocked because it was used to spread pornographic content.") {
    // message contained unsupported characters, text is replaced
    return false;
  }
  if (/* old_message->message_id.is_yet_unsent() && */ !old_content->text.entities.empty() &&
      old_content->text.entities[0].offset == 0 &&
      (new_content->text.entities.empty() || new_content->text.entities[0].offset != 0) &&
      old_content->text.text != new_content->text.text && ends_with(old_content->text.text, new_content->text.text)) {
    // server has deleted first entity and ltrim the message
    return false;
  }
  return true;
}

static bool need_message_entities_changed_warning(const vector<MessageEntity> &old_entities,
                                                  const vector<MessageEntity> &new_entities) {
  size_t old_pos = 0;
  size_t new_pos = 0;
  // compare entities, skipping some known to be different
  while (old_pos < old_entities.size() || new_pos < new_entities.size()) {
    // TODO remove after find_phone_numbers is implemented
    while (new_pos < new_entities.size() && new_entities[new_pos].type == MessageEntity::Type::PhoneNumber) {
      new_pos++;
    }

    if (old_pos < old_entities.size() && new_pos < new_entities.size() &&
        old_entities[old_pos] == new_entities[new_pos]) {
      old_pos++;
      new_pos++;
      continue;
    }

    if (old_pos < old_entities.size() && old_entities[old_pos].type == MessageEntity::Type::MentionName) {
      // server could delete sime MentionName entities
      old_pos++;
      continue;
    }

    if (old_pos < old_entities.size() || new_pos < new_entities.size()) {
      return true;
    }
  }

  return false;
}

void merge_message_contents(Td *td, const MessageContent *old_content, MessageContent *new_content,
                            bool need_message_changed_warning, DialogId dialog_id, bool need_merge_files,
                            bool &is_content_changed, bool &need_update) {
  MessageContentType content_type = new_content->get_type();
  CHECK(old_content->get_type() == content_type);

  switch (content_type) {
    case MessageContentType::Text: {
      const auto *old_ = static_cast<const MessageText *>(old_content);
      const auto *new_ = static_cast<const MessageText *>(new_content);
      auto get_content_object = [td, dialog_id](const MessageContent *content) {
        return to_string(
            get_message_content_object(content, td, dialog_id, -1, false, false, std::numeric_limits<int32>::max()));
      };
      if (old_->text.text != new_->text.text) {
        if (need_message_changed_warning && need_message_text_changed_warning(old_, new_)) {
          LOG(ERROR) << "Message text has changed in " << get_content_object(old_content) << ". New content is "
                     << get_content_object(new_content);
        }
        need_update = true;
      }
      if (old_->text.entities != new_->text.entities) {
        const int32 MAX_CUSTOM_ENTITIES_COUNT = 100;  // server-side limit
        if (need_message_changed_warning && need_message_text_changed_warning(old_, new_) &&
            old_->text.entities.size() <= MAX_CUSTOM_ENTITIES_COUNT &&
            need_message_entities_changed_warning(old_->text.entities, new_->text.entities)) {
          LOG(WARNING) << "Entities have changed in " << get_content_object(old_content) << ". New content is "
                       << get_content_object(new_content);
        }
        need_update = true;
      }
      if (old_->web_page_id != new_->web_page_id) {
        LOG(INFO) << "Old: " << old_->web_page_id << ", new: " << new_->web_page_id;
        is_content_changed = true;
        need_update |= td->web_pages_manager_->have_web_page(old_->web_page_id) ||
                       td->web_pages_manager_->have_web_page(new_->web_page_id);
      }
      break;
    }
    case MessageContentType::Animation: {
      const auto *old_ = static_cast<const MessageAnimation *>(old_content);
      const auto *new_ = static_cast<const MessageAnimation *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->animations_manager_->merge_animations(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->caption != new_->caption) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Audio: {
      const auto *old_ = static_cast<const MessageAudio *>(old_content);
      const auto *new_ = static_cast<const MessageAudio *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->audios_manager_->merge_audios(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->caption != new_->caption) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Contact: {
      const auto *old_ = static_cast<const MessageContact *>(old_content);
      const auto *new_ = static_cast<const MessageContact *>(new_content);
      if (old_->contact != new_->contact) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Document: {
      const auto *old_ = static_cast<const MessageDocument *>(old_content);
      const auto *new_ = static_cast<const MessageDocument *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->documents_manager_->merge_documents(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->caption != new_->caption) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Game: {
      const auto *old_ = static_cast<const MessageGame *>(old_content);
      const auto *new_ = static_cast<const MessageGame *>(new_content);
      if (old_->game != new_->game) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Invoice: {
      const auto *old_ = static_cast<const MessageInvoice *>(old_content);
      const auto *new_ = static_cast<const MessageInvoice *>(new_content);
      if (old_->input_invoice != new_->input_invoice) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::LiveLocation: {
      const auto *old_ = static_cast<const MessageLiveLocation *>(old_content);
      const auto *new_ = static_cast<const MessageLiveLocation *>(new_content);
      if (old_->location != new_->location) {
        need_update = true;
      }
      if (old_->period != new_->period || old_->heading != new_->heading ||
          old_->proximity_alert_radius != new_->proximity_alert_radius) {
        need_update = true;
      }
      if (old_->location.get_access_hash() != new_->location.get_access_hash()) {
        is_content_changed = true;
        merge_location_access_hash(old_->location, new_->location);
      }
      break;
    }
    case MessageContentType::Location: {
      const auto *old_ = static_cast<const MessageLocation *>(old_content);
      const auto *new_ = static_cast<const MessageLocation *>(new_content);
      if (old_->location != new_->location) {
        need_update = true;
      }
      if (old_->location.get_access_hash() != new_->location.get_access_hash()) {
        is_content_changed = true;
        merge_location_access_hash(old_->location, new_->location);
      }
      break;
    }
    case MessageContentType::Photo: {
      const auto *old_ = static_cast<const MessagePhoto *>(old_content);
      auto *new_ = static_cast<MessagePhoto *>(new_content);
      const Photo *old_photo = &old_->photo;
      Photo *new_photo = &new_->photo;
      if (old_photo->date != new_photo->date) {
        LOG(DEBUG) << "Photo date has changed from " << old_photo->date << " to " << new_photo->date;
        is_content_changed = true;
      }
      if (old_photo->id.get() != new_photo->id.get() || old_->caption != new_->caption) {
        need_update = true;
      }
      if (old_photo->minithumbnail != new_photo->minithumbnail) {
        need_update = true;
      }
      if (old_photo->photos != new_photo->photos) {
        LOG(DEBUG) << "Merge photos " << old_photo->photos << " and " << new_photo->photos
                   << ", need_merge_files = " << need_merge_files;
        auto new_photos_size = new_photo->photos.size();
        auto old_photos_size = old_photo->photos.size();

        bool need_merge = false;
        if (need_merge_files && (old_photos_size == 1 || (old_photos_size == 2 && old_photo->photos[0].type == 't')) &&
            old_photo->photos.back().type == 'i') {
          // first time get info about sent photo
          if (old_photos_size == 2) {
            new_photo->photos.push_back(old_photo->photos[0]);
          }
          new_photo->photos.push_back(old_photo->photos.back());
          need_merge = true;
          need_update = true;
        } else {
          // get sent photo again
          if (old_photos_size == 2 + new_photos_size && old_photo->photos[new_photos_size].type == 't') {
            new_photo->photos.push_back(old_photo->photos[new_photos_size]);
          }
          if (old_photos_size == 1 + new_photo->photos.size() && old_photo->photos.back().type == 'i') {
            new_photo->photos.push_back(old_photo->photos.back());
            need_merge = true;
          }
          if (old_photo->photos != new_photo->photos) {
            new_photo->photos.resize(
                new_photos_size);  // return previous size, because we shouldn't add local photo sizes
            need_merge = false;
            need_update = true;
          }
        }

        LOG(DEBUG) << "Merge photos " << old_photo->photos << " and " << new_photo->photos
                   << " with new photos size = " << new_photos_size << ", need_merge = " << need_merge
                   << ", need_update = " << need_update;
        if (need_merge && new_photos_size != 0) {
          FileId old_file_id = get_message_content_upload_file_id(old_content);
          FileView old_file_view = td->file_manager_->get_file_view(old_file_id);
          FileId new_file_id = new_photo->photos[0].file_id;
          FileView new_file_view = td->file_manager_->get_file_view(new_file_id);
          CHECK(new_file_view.has_remote_location());

          LOG(DEBUG) << "Trying to merge old file " << old_file_id << " and new file " << new_file_id;
          if (new_file_view.remote_location().is_web()) {
            LOG(ERROR) << "Have remote web photo location";
          } else if (!old_file_view.has_remote_location() ||
                     old_file_view.main_remote_location().get_file_reference() !=
                         new_file_view.remote_location().get_file_reference() ||
                     old_file_view.main_remote_location().get_access_hash() !=
                         new_file_view.remote_location().get_access_hash()) {
            FileId file_id = td->file_manager_->register_remote(
                FullRemoteFileLocation(PhotoSizeSource::thumbnail(FileType::Photo, 'i'),
                                       new_file_view.remote_location().get_id(),
                                       new_file_view.remote_location().get_access_hash(), DcId::invalid(),
                                       new_file_view.remote_location().get_file_reference().str()),
                FileLocationSource::FromServer, dialog_id, old_photo->photos.back().size, 0, "");
            LOG_STATUS(td->file_manager_->merge(file_id, old_file_id));
          }
        }
      }
      break;
    }
    case MessageContentType::Sticker: {
      const auto *old_ = static_cast<const MessageSticker *>(old_content);
      const auto *new_ = static_cast<const MessageSticker *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->stickers_manager_->merge_stickers(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->is_premium != new_->is_premium) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Venue: {
      const auto *old_ = static_cast<const MessageVenue *>(old_content);
      const auto *new_ = static_cast<const MessageVenue *>(new_content);
      if (old_->venue != new_->venue) {
        need_update = true;
      }
      if (old_->venue.location().get_access_hash() != new_->venue.location().get_access_hash()) {
        is_content_changed = true;
        merge_location_access_hash(old_->venue.location(), new_->venue.location());
      }
      break;
    }
    case MessageContentType::Video: {
      const auto *old_ = static_cast<const MessageVideo *>(old_content);
      const auto *new_ = static_cast<const MessageVideo *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->videos_manager_->merge_videos(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->caption != new_->caption) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::VideoNote: {
      const auto *old_ = static_cast<const MessageVideoNote *>(old_content);
      const auto *new_ = static_cast<const MessageVideoNote *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->video_notes_manager_->merge_video_notes(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->is_viewed != new_->is_viewed) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::VoiceNote: {
      const auto *old_ = static_cast<const MessageVoiceNote *>(old_content);
      const auto *new_ = static_cast<const MessageVoiceNote *>(new_content);
      if (old_->file_id != new_->file_id) {
        if (need_merge_files) {
          td->voice_notes_manager_->merge_voice_notes(new_->file_id, old_->file_id, false);
        }
        need_update = true;
      }
      if (old_->caption != new_->caption || old_->is_listened != new_->is_listened) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatCreate: {
      const auto *old_ = static_cast<const MessageChatCreate *>(old_content);
      const auto *new_ = static_cast<const MessageChatCreate *>(new_content);
      if (old_->title != new_->title || old_->participant_user_ids != new_->participant_user_ids) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatChangeTitle: {
      const auto *old_ = static_cast<const MessageChatChangeTitle *>(old_content);
      const auto *new_ = static_cast<const MessageChatChangeTitle *>(new_content);
      if (old_->title != new_->title) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatChangePhoto: {
      const auto *old_ = static_cast<const MessageChatChangePhoto *>(old_content);
      const auto *new_ = static_cast<const MessageChatChangePhoto *>(new_content);
      if (old_->photo != new_->photo) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatDeletePhoto:
      break;
    case MessageContentType::ChatDeleteHistory:
      break;
    case MessageContentType::ChatAddUsers: {
      const auto *old_ = static_cast<const MessageChatAddUsers *>(old_content);
      const auto *new_ = static_cast<const MessageChatAddUsers *>(new_content);
      if (old_->user_ids != new_->user_ids) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatJoinedByLink: {
      auto old_ = static_cast<const MessageChatJoinedByLink *>(old_content);
      auto new_ = static_cast<const MessageChatJoinedByLink *>(new_content);
      if (old_->is_approved != new_->is_approved) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatDeleteUser: {
      const auto *old_ = static_cast<const MessageChatDeleteUser *>(old_content);
      const auto *new_ = static_cast<const MessageChatDeleteUser *>(new_content);
      if (old_->user_id != new_->user_id) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChatMigrateTo: {
      const auto *old_ = static_cast<const MessageChatMigrateTo *>(old_content);
      const auto *new_ = static_cast<const MessageChatMigrateTo *>(new_content);
      if (old_->migrated_to_channel_id != new_->migrated_to_channel_id) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChannelCreate: {
      const auto *old_ = static_cast<const MessageChannelCreate *>(old_content);
      const auto *new_ = static_cast<const MessageChannelCreate *>(new_content);
      if (old_->title != new_->title) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ChannelMigrateFrom: {
      const auto *old_ = static_cast<const MessageChannelMigrateFrom *>(old_content);
      const auto *new_ = static_cast<const MessageChannelMigrateFrom *>(new_content);
      if (old_->title != new_->title || old_->migrated_from_chat_id != new_->migrated_from_chat_id) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::PinMessage: {
      const auto *old_ = static_cast<const MessagePinMessage *>(old_content);
      const auto *new_ = static_cast<const MessagePinMessage *>(new_content);
      if (old_->message_id != new_->message_id) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::GameScore: {
      const auto *old_ = static_cast<const MessageGameScore *>(old_content);
      const auto *new_ = static_cast<const MessageGameScore *>(new_content);
      if (old_->game_message_id != new_->game_message_id || old_->game_id != new_->game_id ||
          old_->score != new_->score) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ScreenshotTaken:
      break;
    case MessageContentType::ChatSetTtl: {
      const auto *old_ = static_cast<const MessageChatSetTtl *>(old_content);
      const auto *new_ = static_cast<const MessageChatSetTtl *>(new_content);
      if (old_->ttl != new_->ttl) {
        LOG(ERROR) << "Ttl has changed from " << old_->ttl << " to " << new_->ttl;
        need_update = true;
      }
      break;
    }
    case MessageContentType::Call: {
      const auto *old_ = static_cast<const MessageCall *>(old_content);
      const auto *new_ = static_cast<const MessageCall *>(new_content);
      if (old_->call_id != new_->call_id || old_->is_video != new_->is_video) {
        is_content_changed = true;
      }
      if (old_->duration != new_->duration || old_->discard_reason != new_->discard_reason) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::PaymentSuccessful: {
      const auto *old_ = static_cast<const MessagePaymentSuccessful *>(old_content);
      const auto *new_ = static_cast<const MessagePaymentSuccessful *>(new_content);
      if (old_->invoice_dialog_id != new_->invoice_dialog_id || old_->invoice_message_id != new_->invoice_message_id ||
          old_->currency != new_->currency || old_->total_amount != new_->total_amount ||
          old_->invoice_payload != new_->invoice_payload || old_->shipping_option_id != new_->shipping_option_id ||
          old_->telegram_payment_charge_id != new_->telegram_payment_charge_id ||
          old_->provider_payment_charge_id != new_->provider_payment_charge_id ||
          ((old_->order_info != nullptr || new_->order_info != nullptr) &&
           (old_->order_info == nullptr || new_->order_info == nullptr || *old_->order_info != *new_->order_info ||
            old_->is_recurring != new_->is_recurring || old_->is_first_recurring != new_->is_first_recurring))) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ContactRegistered:
      break;
    case MessageContentType::ExpiredPhoto:
      break;
    case MessageContentType::ExpiredVideo:
      break;
    case MessageContentType::CustomServiceAction: {
      const auto *old_ = static_cast<const MessageCustomServiceAction *>(old_content);
      const auto *new_ = static_cast<const MessageCustomServiceAction *>(new_content);
      if (old_->message != new_->message) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::WebsiteConnected: {
      const auto *old_ = static_cast<const MessageWebsiteConnected *>(old_content);
      const auto *new_ = static_cast<const MessageWebsiteConnected *>(new_content);
      if (old_->domain_name != new_->domain_name) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::PassportDataSent: {
      const auto *old_ = static_cast<const MessagePassportDataSent *>(old_content);
      const auto *new_ = static_cast<const MessagePassportDataSent *>(new_content);
      if (old_->types != new_->types) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::PassportDataReceived: {
      const auto *old_ = static_cast<const MessagePassportDataReceived *>(old_content);
      const auto *new_ = static_cast<const MessagePassportDataReceived *>(new_content);
      if (old_->values != new_->values || old_->credentials != new_->credentials) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Poll: {
      const auto *old_ = static_cast<const MessagePoll *>(old_content);
      const auto *new_ = static_cast<const MessagePoll *>(new_content);
      if (old_->poll_id != new_->poll_id) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Dice: {
      const auto *old_ = static_cast<const MessageDice *>(old_content);
      const auto *new_ = static_cast<const MessageDice *>(new_content);
      if (old_->emoji != new_->emoji || old_->dice_value != new_->dice_value) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::ProximityAlertTriggered: {
      const auto *old_ = static_cast<const MessageProximityAlertTriggered *>(old_content);
      const auto *new_ = static_cast<const MessageProximityAlertTriggered *>(new_content);
      if (old_->traveler_dialog_id != new_->traveler_dialog_id || old_->watcher_dialog_id != new_->watcher_dialog_id ||
          old_->distance != new_->distance) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::GroupCall: {
      const auto *old_ = static_cast<const MessageGroupCall *>(old_content);
      const auto *new_ = static_cast<const MessageGroupCall *>(new_content);
      if (old_->input_group_call_id != new_->input_group_call_id || old_->duration != new_->duration ||
          old_->schedule_date != new_->schedule_date) {
        need_update = true;
      }
      if (!old_->input_group_call_id.is_identical(new_->input_group_call_id)) {
        is_content_changed = true;
      }
      break;
    }
    case MessageContentType::InviteToGroupCall: {
      const auto *old_ = static_cast<const MessageInviteToGroupCall *>(old_content);
      const auto *new_ = static_cast<const MessageInviteToGroupCall *>(new_content);
      if (old_->input_group_call_id != new_->input_group_call_id || old_->user_ids != new_->user_ids) {
        need_update = true;
      }
      if (!old_->input_group_call_id.is_identical(new_->input_group_call_id)) {
        is_content_changed = true;
      }
      break;
    }
    case MessageContentType::ChatSetTheme: {
      const auto *old_ = static_cast<const MessageChatSetTheme *>(old_content);
      const auto *new_ = static_cast<const MessageChatSetTheme *>(new_content);
      if (old_->emoji != new_->emoji) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::WebViewDataSent: {
      const auto *old_ = static_cast<const MessageWebViewDataSent *>(old_content);
      const auto *new_ = static_cast<const MessageWebViewDataSent *>(new_content);
      if (old_->button_text != new_->button_text) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::WebViewDataReceived: {
      const auto *old_ = static_cast<const MessageWebViewDataReceived *>(old_content);
      const auto *new_ = static_cast<const MessageWebViewDataReceived *>(new_content);
      if (old_->button_text != new_->button_text || old_->data != new_->data) {
        need_update = true;
      }
      break;
    }
    case MessageContentType::Unsupported: {
      const auto *old_ = static_cast<const MessageUnsupported *>(old_content);
      const auto *new_ = static_cast<const MessageUnsupported *>(new_content);
      if (old_->version != new_->version) {
        is_content_changed = true;
      }
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}

bool merge_message_content_file_id(Td *td, MessageContent *message_content, FileId new_file_id) {
  if (!new_file_id.is_valid()) {
    return false;
  }

  LOG(INFO) << "Merge message content of a message with file " << new_file_id;
  MessageContentType content_type = message_content->get_type();
  switch (content_type) {
    case MessageContentType::Animation: {
      auto content = static_cast<MessageAnimation *>(message_content);
      if (new_file_id != content->file_id) {
        td->animations_manager_->merge_animations(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::Audio: {
      auto content = static_cast<MessageAudio *>(message_content);
      if (new_file_id != content->file_id) {
        td->audios_manager_->merge_audios(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::Document: {
      auto content = static_cast<MessageDocument *>(message_content);
      if (new_file_id != content->file_id) {
        td->documents_manager_->merge_documents(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::Photo: {
      auto content = static_cast<MessagePhoto *>(message_content);
      Photo *photo = &content->photo;
      if (!photo->photos.empty() && photo->photos.back().type == 'i') {
        FileId &old_file_id = photo->photos.back().file_id;
        if (old_file_id != new_file_id) {
          LOG_STATUS(td->file_manager_->merge(new_file_id, old_file_id));
          old_file_id = new_file_id;
          return true;
        }
      }
      break;
    }
    case MessageContentType::Sticker: {
      auto content = static_cast<MessageSticker *>(message_content);
      if (new_file_id != content->file_id) {
        td->stickers_manager_->merge_stickers(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::Video: {
      auto content = static_cast<MessageVideo *>(message_content);
      if (new_file_id != content->file_id) {
        td->videos_manager_->merge_videos(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::VideoNote: {
      auto content = static_cast<MessageVideoNote *>(message_content);
      if (new_file_id != content->file_id) {
        td->video_notes_manager_->merge_video_notes(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::VoiceNote: {
      auto content = static_cast<MessageVoiceNote *>(message_content);
      if (new_file_id != content->file_id) {
        td->voice_notes_manager_->merge_voice_notes(new_file_id, content->file_id, false);
        content->file_id = new_file_id;
        return true;
      }
      break;
    }
    case MessageContentType::Contact:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Text:
    case MessageContentType::Venue:
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
    case MessageContentType::Unsupported:
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
      LOG(ERROR) << "Receive new file " << new_file_id << " in a sent message of the type " << content_type;
      break;
    default:
      UNREACHABLE();
      break;
  }
  return false;
}

static bool can_be_animated_emoji(const FormattedText &text) {
  return text.entities.empty() && is_emoji(text.text);
}

void register_message_content(Td *td, const MessageContent *content, FullMessageId full_message_id,
                              const char *source) {
  switch (content->get_type()) {
    case MessageContentType::Text: {
      auto text = static_cast<const MessageText *>(content);
      if (text->web_page_id.is_valid()) {
        td->web_pages_manager_->register_web_page(text->web_page_id, full_message_id, source);
      } else if (can_be_animated_emoji(text->text)) {
        td->stickers_manager_->register_emoji(text->text.text, full_message_id, source);
      }
      return;
    }
    case MessageContentType::VoiceNote:
      return td->voice_notes_manager_->register_voice_note(static_cast<const MessageVoiceNote *>(content)->file_id,
                                                           full_message_id, source);
    case MessageContentType::Poll:
      return td->poll_manager_->register_poll(static_cast<const MessagePoll *>(content)->poll_id, full_message_id,
                                              source);
    case MessageContentType::Dice: {
      auto dice = static_cast<const MessageDice *>(content);
      return td->stickers_manager_->register_dice(dice->emoji, dice->dice_value, full_message_id, source);
    }
    default:
      return;
  }
}

void reregister_message_content(Td *td, const MessageContent *old_content, const MessageContent *new_content,
                                FullMessageId full_message_id, const char *source) {
  auto old_content_type = old_content->get_type();
  auto new_content_type = new_content->get_type();
  if (old_content_type == new_content_type) {
    switch (old_content_type) {
      case MessageContentType::Text: {
        auto old_text = static_cast<const MessageText *>(old_content);
        auto new_text = static_cast<const MessageText *>(new_content);
        if (old_text->web_page_id == new_text->web_page_id &&
            (old_text->text == new_text->text ||
             (!can_be_animated_emoji(old_text->text) && !can_be_animated_emoji(new_text->text)))) {
          return;
        }
        break;
      }
      case MessageContentType::VoiceNote:
        if (static_cast<const MessageVoiceNote *>(old_content)->file_id ==
            static_cast<const MessageVoiceNote *>(new_content)->file_id) {
          return;
        }
        break;
      case MessageContentType::Poll:
        if (static_cast<const MessagePoll *>(old_content)->poll_id ==
            static_cast<const MessagePoll *>(new_content)->poll_id) {
          return;
        }
        break;
      case MessageContentType::Dice:
        if (static_cast<const MessageDice *>(old_content)->emoji ==
                static_cast<const MessageDice *>(new_content)->emoji &&
            static_cast<const MessageDice *>(old_content)->dice_value ==
                static_cast<const MessageDice *>(new_content)->dice_value) {
          return;
        }
        break;
      default:
        return;
    }
  }
  unregister_message_content(td, old_content, full_message_id, source);
  register_message_content(td, new_content, full_message_id, source);
}

void unregister_message_content(Td *td, const MessageContent *content, FullMessageId full_message_id,
                                const char *source) {
  switch (content->get_type()) {
    case MessageContentType::Text: {
      auto text = static_cast<const MessageText *>(content);
      if (text->web_page_id.is_valid()) {
        td->web_pages_manager_->unregister_web_page(text->web_page_id, full_message_id, source);
      } else if (can_be_animated_emoji(text->text)) {
        td->stickers_manager_->unregister_emoji(text->text.text, full_message_id, source);
      }
      return;
    }
    case MessageContentType::VoiceNote:
      return td->voice_notes_manager_->unregister_voice_note(static_cast<const MessageVoiceNote *>(content)->file_id,
                                                             full_message_id, source);
    case MessageContentType::Poll:
      return td->poll_manager_->unregister_poll(static_cast<const MessagePoll *>(content)->poll_id, full_message_id,
                                                source);
    case MessageContentType::Dice: {
      auto dice = static_cast<const MessageDice *>(content);
      return td->stickers_manager_->unregister_dice(dice->emoji, dice->dice_value, full_message_id, source);
    }
    default:
      return;
  }
}

template <class ToT, class FromT>
static tl_object_ptr<ToT> secret_to_telegram(FromT &from);

// photoSizeEmpty type:string = PhotoSize;
static auto secret_to_telegram(secret_api::photoSizeEmpty &empty) {
  if (!clean_input_string(empty.type_)) {
    empty.type_.clear();
  }
  return make_tl_object<telegram_api::photoSizeEmpty>(empty.type_);
}

// photoSize type:string location:FileLocation w:int h:int size:int = PhotoSize;
static auto secret_to_telegram(secret_api::photoSize &photo_size) {
  if (!clean_input_string(photo_size.type_)) {
    photo_size.type_.clear();
  }
  return make_tl_object<telegram_api::photoSize>(photo_size.type_, photo_size.w_, photo_size.h_, photo_size.size_);
}

// photoCachedSize type:string location:FileLocation w:int h:int bytes:bytes = PhotoSize;
static auto secret_to_telegram(secret_api::photoCachedSize &photo_size) {
  if (!clean_input_string(photo_size.type_)) {
    photo_size.type_.clear();
  }
  return make_tl_object<telegram_api::photoCachedSize>(photo_size.type_, photo_size.w_, photo_size.h_,
                                                       photo_size.bytes_.clone());
}

// documentAttributeImageSize w:int h:int = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeImageSize &image_size) {
  return make_tl_object<telegram_api::documentAttributeImageSize>(image_size.w_, image_size.h_);
}

// documentAttributeAnimated = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeAnimated &animated) {
  return make_tl_object<telegram_api::documentAttributeAnimated>();
}

// documentAttributeSticker23 = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeSticker23 &sticker) {
  return make_tl_object<telegram_api::documentAttributeSticker>(
      0, false /*ignored*/, "", make_tl_object<telegram_api::inputStickerSetEmpty>(), nullptr);
}

static auto secret_to_telegram(secret_api::inputStickerSetEmpty &sticker_set) {
  return make_tl_object<telegram_api::inputStickerSetEmpty>();
}

static auto secret_to_telegram(secret_api::inputStickerSetShortName &sticker_set) {
  if (!clean_input_string(sticker_set.short_name_)) {
    sticker_set.short_name_.clear();
  }
  return make_tl_object<telegram_api::inputStickerSetShortName>(sticker_set.short_name_);
}

// documentAttributeSticker alt:string stickerset:InputStickerSet = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeSticker &sticker) {
  if (!clean_input_string(sticker.alt_)) {
    sticker.alt_.clear();
  }
  return make_tl_object<telegram_api::documentAttributeSticker>(
      0, false /*ignored*/, sticker.alt_, secret_to_telegram<telegram_api::InputStickerSet>(*sticker.stickerset_),
      nullptr);
}

// documentAttributeVideo duration:int w:int h:int = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeVideo &video) {
  return make_tl_object<telegram_api::documentAttributeVideo>(0, false /*ignored*/, false /*ignored*/, video.duration_,
                                                              video.w_, video.h_);
}

// documentAttributeFilename file_name:string = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeFilename &filename) {
  if (!clean_input_string(filename.file_name_)) {
    filename.file_name_.clear();
  }
  return make_tl_object<telegram_api::documentAttributeFilename>(filename.file_name_);
}

// documentAttributeVideo66 flags:# round_message:flags.0?true duration:int w:int h:int = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeVideo66 &video) {
  return make_tl_object<telegram_api::documentAttributeVideo>(
      (video.flags_ & secret_api::documentAttributeVideo66::ROUND_MESSAGE_MASK) != 0
          ? telegram_api::documentAttributeVideo::ROUND_MESSAGE_MASK
          : 0,
      video.round_message_, false, video.duration_, video.w_, video.h_);
}

static auto telegram_documentAttributeAudio(bool is_voice_note, int duration, string title, string performer,
                                            BufferSlice waveform) {
  if (!clean_input_string(title)) {
    title.clear();
  }
  if (!clean_input_string(performer)) {
    performer.clear();
  }

  int32 flags = 0;
  if (is_voice_note) {
    flags |= telegram_api::documentAttributeAudio::VOICE_MASK;
  }
  if (!title.empty()) {
    flags |= telegram_api::documentAttributeAudio::TITLE_MASK;
  }
  if (!performer.empty()) {
    flags |= telegram_api::documentAttributeAudio::PERFORMER_MASK;
  }
  if (!waveform.empty()) {
    flags |= telegram_api::documentAttributeAudio::WAVEFORM_MASK;
  }
  return make_tl_object<telegram_api::documentAttributeAudio>(flags, is_voice_note, duration, std::move(title),
                                                              std::move(performer), std::move(waveform));
}

// documentAttributeAudio23 duration:int = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeAudio23 &audio) {
  return telegram_documentAttributeAudio(false, audio.duration_, "", "", Auto());
}
// documentAttributeAudio45 duration:int title:string performer:string = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeAudio45 &audio) {
  return telegram_documentAttributeAudio(false, audio.duration_, audio.title_, audio.performer_, Auto());
}

// documentAttributeAudio flags:# voice:flags.10?true duration:int title:flags.0?string
//    performer:flags.1?string waveform:flags.2?bytes = DocumentAttribute;
static auto secret_to_telegram(secret_api::documentAttributeAudio &audio) {
  return telegram_documentAttributeAudio((audio.flags_ & secret_api::documentAttributeAudio::VOICE_MASK) != 0,
                                         audio.duration_, audio.title_, audio.performer_, audio.waveform_.clone());
}

static auto secret_to_telegram(std::vector<tl_object_ptr<secret_api::DocumentAttribute>> &attributes) {
  std::vector<tl_object_ptr<telegram_api::DocumentAttribute>> res;
  for (auto &attribute : attributes) {
    auto telegram_attribute = secret_to_telegram<telegram_api::DocumentAttribute>(*attribute);
    if (telegram_attribute) {
      res.push_back(std::move(telegram_attribute));
    }
  }
  return res;
}

// decryptedMessageMediaExternalDocument id:long access_hash:long date:int mime_type:string size:int
// thumb:PhotoSize dc_id:int attributes:Vector<DocumentAttribute> = DecryptedMessageMedia;
static auto secret_to_telegram_document(secret_api::decryptedMessageMediaExternalDocument &from) {
  if (!clean_input_string(from.mime_type_)) {
    from.mime_type_.clear();
  }
  vector<telegram_api::object_ptr<telegram_api::PhotoSize>> thumbnails;
  thumbnails.push_back(secret_to_telegram<telegram_api::PhotoSize>(*from.thumb_));
  return make_tl_object<telegram_api::document>(0, from.id_, from.access_hash_, BufferSlice(), from.date_,
                                                from.mime_type_, from.size_, std::move(thumbnails), Auto(), from.dc_id_,
                                                secret_to_telegram(from.attributes_));
}

template <class ToT, class FromT>
static tl_object_ptr<ToT> secret_to_telegram(FromT &from) {
  tl_object_ptr<ToT> res;
  downcast_call(from, [&](auto &p) { res = secret_to_telegram(p); });
  return res;
}

static unique_ptr<MessageContent> get_document_message_content(Document &&parsed_document, FormattedText &&caption,
                                                               bool is_opened, bool is_premium) {
  auto file_id = parsed_document.file_id;
  if (!parsed_document.empty()) {
    CHECK(file_id.is_valid());
  }
  switch (parsed_document.type) {
    case Document::Type::Animation:
      return make_unique<MessageAnimation>(file_id, std::move(caption));
    case Document::Type::Audio:
      return make_unique<MessageAudio>(file_id, std::move(caption));
    case Document::Type::General:
      return make_unique<MessageDocument>(file_id, std::move(caption));
    case Document::Type::Sticker:
      return make_unique<MessageSticker>(file_id, is_premium);
    case Document::Type::Unknown:
      return make_unique<MessageUnsupported>();
    case Document::Type::Video:
      return make_unique<MessageVideo>(file_id, std::move(caption));
    case Document::Type::VideoNote:
      return make_unique<MessageVideoNote>(file_id, is_opened);
    case Document::Type::VoiceNote:
      return make_unique<MessageVoiceNote>(file_id, std::move(caption), is_opened);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

static unique_ptr<MessageContent> get_document_message_content(Td *td, tl_object_ptr<telegram_api::document> &&document,
                                                               DialogId owner_dialog_id, FormattedText &&caption,
                                                               bool is_opened, bool is_premium,
                                                               MultiPromiseActor *load_data_multipromise_ptr) {
  return get_document_message_content(
      td->documents_manager_->on_get_document(std::move(document), owner_dialog_id, load_data_multipromise_ptr),
      std::move(caption), is_opened, is_premium);
}

unique_ptr<MessageContent> get_secret_message_content(
    Td *td, string message_text, unique_ptr<EncryptedFile> file,
    tl_object_ptr<secret_api::DecryptedMessageMedia> &&media_ptr,
    vector<tl_object_ptr<secret_api::MessageEntity>> &&secret_entities, DialogId owner_dialog_id,
    MultiPromiseActor &load_data_multipromise, bool is_premium) {
  int32 constructor_id = media_ptr == nullptr ? secret_api::decryptedMessageMediaEmpty::ID : media_ptr->get_id();
  auto caption = [&] {
    switch (constructor_id) {
      case secret_api::decryptedMessageMediaVideo::ID: {
        auto media = static_cast<secret_api::decryptedMessageMediaVideo *>(media_ptr.get());
        return std::move(media->caption_);
      }
      case secret_api::decryptedMessageMediaPhoto::ID: {
        auto media = static_cast<secret_api::decryptedMessageMediaPhoto *>(media_ptr.get());
        return std::move(media->caption_);
      }
      case secret_api::decryptedMessageMediaDocument46::ID: {
        auto media = static_cast<secret_api::decryptedMessageMediaDocument46 *>(media_ptr.get());
        return std::move(media->caption_);
      }
      case secret_api::decryptedMessageMediaDocument::ID: {
        auto media = static_cast<secret_api::decryptedMessageMediaDocument *>(media_ptr.get());
        return std::move(media->caption_);
      }
      default:
        return string();
    }
  }();
  if (!clean_input_string(caption)) {
    caption.clear();
  }

  if (message_text.empty()) {
    message_text = std::move(caption);
  } else if (!caption.empty()) {
    message_text = message_text + "\n\n" + caption;
  }

  auto entities = get_message_entities(std::move(secret_entities));
  auto status = fix_formatted_text(message_text, entities, true, false, true, td->auth_manager_->is_bot(), false);
  if (status.is_error()) {
    LOG(WARNING) << "Receive error " << status << " while parsing secret message \"" << message_text
                 << "\" with entities " << format::as_array(entities);
    if (!clean_input_string(message_text)) {
      message_text.clear();
    }
    entities = find_entities(message_text, true, td->auth_manager_->is_bot());
  }

  // support of old layer and old constructions
  switch (constructor_id) {
    case secret_api::decryptedMessageMediaDocument46::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaDocument46>(media_ptr);
      media_ptr = make_tl_object<secret_api::decryptedMessageMediaDocument>(
          std::move(media->thumb_), media->thumb_w_, media->thumb_h_, media->mime_type_, media->size_,
          std::move(media->key_), std::move(media->iv_), std::move(media->attributes_), string());

      constructor_id = secret_api::decryptedMessageMediaDocument::ID;
      break;
    }
    case secret_api::decryptedMessageMediaVideo::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaVideo>(media_ptr);
      vector<tl_object_ptr<secret_api::DocumentAttribute>> attributes;
      attributes.emplace_back(
          make_tl_object<secret_api::documentAttributeVideo>(media->duration_, media->w_, media->h_));
      media_ptr = make_tl_object<secret_api::decryptedMessageMediaDocument>(
          std::move(media->thumb_), media->thumb_w_, media->thumb_h_, media->mime_type_, media->size_,
          std::move(media->key_), std::move(media->iv_), std::move(attributes), string());

      constructor_id = secret_api::decryptedMessageMediaDocument::ID;
      break;
    }
    default:
      break;
  }

  bool is_media_empty = false;
  switch (constructor_id) {
    case secret_api::decryptedMessageMediaEmpty::ID:
      if (message_text.empty()) {
        LOG(ERROR) << "Receive empty message text and media";
      }
      is_media_empty = true;
      break;
    case secret_api::decryptedMessageMediaGeoPoint::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaGeoPoint>(media_ptr);

      auto m = make_unique<MessageLocation>(Location(media));
      if (m->location.empty()) {
        is_media_empty = true;
        break;
      }

      return std::move(m);
    }
    case secret_api::decryptedMessageMediaVenue::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaVenue>(media_ptr);

      if (!clean_input_string(media->title_)) {
        media->title_.clear();
      }
      if (!clean_input_string(media->address_)) {
        media->address_.clear();
      }
      if (!clean_input_string(media->provider_)) {
        media->provider_.clear();
      }
      if (!clean_input_string(media->venue_id_)) {
        media->venue_id_.clear();
      }

      auto m = make_unique<MessageVenue>(Venue(Location(media->lat_, media->long_, 0.0, 0), std::move(media->title_),
                                               std::move(media->address_), std::move(media->provider_),
                                               std::move(media->venue_id_), string()));
      if (m->venue.empty()) {
        is_media_empty = true;
        break;
      }

      return std::move(m);
    }
    case secret_api::decryptedMessageMediaContact::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaContact>(media_ptr);
      if (!clean_input_string(media->phone_number_)) {
        media->phone_number_.clear();
      }
      if (!clean_input_string(media->first_name_)) {
        media->first_name_.clear();
      }
      if (!clean_input_string(media->last_name_)) {
        media->last_name_.clear();
      }
      return make_unique<MessageContact>(Contact(std::move(media->phone_number_), std::move(media->first_name_),
                                                 std::move(media->last_name_), string(), UserId()));
    }
    case secret_api::decryptedMessageMediaWebPage::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaWebPage>(media_ptr);
      if (!clean_input_string(media->url_)) {
        media->url_.clear();
      }
      auto r_http_url = parse_url(media->url_);
      if (r_http_url.is_error()) {
        is_media_empty = true;
        break;
      }
      auto url = r_http_url.ok().get_url();

      auto result = make_unique<MessageText>(FormattedText{std::move(message_text), std::move(entities)}, WebPageId());
      td->web_pages_manager_->get_web_page_by_url(
          url,
          PromiseCreator::lambda([&web_page_id = result->web_page_id, promise = load_data_multipromise.get_promise()](
                                     Result<WebPageId> r_web_page_id) mutable {
            if (r_web_page_id.is_ok()) {
              web_page_id = r_web_page_id.move_as_ok();
            }
            promise.set_value(Unit());
          }));
      return std::move(result);
    }
    case secret_api::decryptedMessageMediaExternalDocument::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaExternalDocument>(media_ptr);
      return get_document_message_content(td, secret_to_telegram_document(*media), owner_dialog_id,
                                          FormattedText{std::move(message_text), std::move(entities)}, false,
                                          is_premium, &load_data_multipromise);
    }
    default:
      break;
  }
  if (file == nullptr && !is_media_empty) {
    LOG(ERROR) << "Received secret message with media, but without a file";
    is_media_empty = true;
  }
  if (is_media_empty) {
    return create_text_message_content(std::move(message_text), std::move(entities), WebPageId());
  }
  switch (constructor_id) {
    case secret_api::decryptedMessageMediaPhoto::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaPhoto>(media_ptr);
      return make_unique<MessagePhoto>(
          get_encrypted_file_photo(td->file_manager_.get(), std::move(file), std::move(media), owner_dialog_id),
          FormattedText{std::move(message_text), std::move(entities)});
    }
    case secret_api::decryptedMessageMediaDocument::ID: {
      auto media = move_tl_object_as<secret_api::decryptedMessageMediaDocument>(media_ptr);
      if (!clean_input_string(media->mime_type_)) {
        media->mime_type_.clear();
      }
      auto attributes = secret_to_telegram(media->attributes_);
      for (auto &attribute : attributes) {
        CHECK(attribute != nullptr);
        if (attribute->get_id() == telegram_api::documentAttributeSticker::ID) {
          auto attribute_sticker = static_cast<telegram_api::documentAttributeSticker *>(attribute.get());
          CHECK(attribute_sticker->stickerset_ != nullptr);
          if (attribute_sticker->stickerset_->get_id() != telegram_api::inputStickerSetEmpty::ID) {
            attribute_sticker->stickerset_ = make_tl_object<telegram_api::inputStickerSetEmpty>();
          }
        }
      }

      media->attributes_.clear();
      auto document = td->documents_manager_->on_get_document(
          {std::move(file), std::move(media), std::move(attributes)}, owner_dialog_id);
      return get_document_message_content(std::move(document), {std::move(message_text), std::move(entities)}, false,
                                          false);
    }
    default:
      LOG(ERROR) << "Unsupported: " << to_string(media_ptr);
      return make_unique<MessageUnsupported>();
  }
}

unique_ptr<MessageContent> get_message_content(Td *td, FormattedText message,
                                               tl_object_ptr<telegram_api::MessageMedia> &&media_ptr,
                                               DialogId owner_dialog_id, bool is_content_read, UserId via_bot_user_id,
                                               int32 *ttl, bool *disable_web_page_preview) {
  if (!td->auth_manager_->was_authorized() && !G()->close_flag() && media_ptr != nullptr &&
      media_ptr->get_id() != telegram_api::messageMediaEmpty::ID) {
    LOG(ERROR) << "Receive without authorization " << to_string(media_ptr);
    media_ptr = nullptr;
  }
  if (disable_web_page_preview != nullptr) {
    *disable_web_page_preview = false;
  }

  int32 constructor_id = media_ptr == nullptr ? telegram_api::messageMediaEmpty::ID : media_ptr->get_id();
  switch (constructor_id) {
    case telegram_api::messageMediaEmpty::ID:
      if (message.text.empty()) {
        LOG(ERROR) << "Receive empty message text and media for message from " << owner_dialog_id;
      }
      if (disable_web_page_preview != nullptr) {
        *disable_web_page_preview = true;
      }
      return make_unique<MessageText>(std::move(message), WebPageId());
    case telegram_api::messageMediaPhoto::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaPhoto>(media_ptr);
      if (media->photo_ == nullptr) {
        if ((media->flags_ & telegram_api::messageMediaPhoto::TTL_SECONDS_MASK) == 0) {
          LOG(ERROR) << "Receive messageMediaPhoto without photo and TTL: " << oneline(to_string(media));
          break;
        }

        return make_unique<MessageExpiredPhoto>();
      }

      auto photo = get_photo(td->file_manager_.get(), std::move(media->photo_), owner_dialog_id);
      if (photo.is_empty()) {
        return make_unique<MessageExpiredPhoto>();
      }

      if (ttl != nullptr && (media->flags_ & telegram_api::messageMediaPhoto::TTL_SECONDS_MASK) != 0) {
        *ttl = media->ttl_seconds_;
      }
      return make_unique<MessagePhoto>(std::move(photo), std::move(message));
    }
    case telegram_api::messageMediaDice::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaDice>(media_ptr);

      auto m = td::make_unique<MessageDice>(media->emoticon_, media->value_);
      if (!m->is_valid()) {
        break;
      }

      return std::move(m);
    }
    case telegram_api::messageMediaGeo::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaGeo>(media_ptr);

      auto m = make_unique<MessageLocation>(Location(media->geo_));
      if (m->location.empty()) {
        break;
      }

      return std::move(m);
    }
    case telegram_api::messageMediaGeoLive::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaGeoLive>(media_ptr);
      auto location = Location(media->geo_);
      if (location.empty()) {
        break;
      }

      int32 period = media->period_;
      if (period <= 0) {
        LOG(ERROR) << "Receive wrong live location period = " << period;
        return make_unique<MessageLocation>(std::move(location));
      }
      return make_unique<MessageLiveLocation>(std::move(location), period, media->heading_,
                                              media->proximity_notification_radius_);
    }
    case telegram_api::messageMediaVenue::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaVenue>(media_ptr);
      auto m = make_unique<MessageVenue>(Venue(media->geo_, std::move(media->title_), std::move(media->address_),
                                               std::move(media->provider_), std::move(media->venue_id_),
                                               std::move(media->venue_type_)));
      if (m->venue.empty()) {
        break;
      }

      return std::move(m);
    }
    case telegram_api::messageMediaContact::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaContact>(media_ptr);
      if (media->user_id_ != 0) {
        td->contacts_manager_->get_user_id_object(UserId(media->user_id_),
                                                  "MessageMediaContact");  // to ensure updateUser
      }
      return make_unique<MessageContact>(Contact(std::move(media->phone_number_), std::move(media->first_name_),
                                                 std::move(media->last_name_), std::move(media->vcard_),
                                                 UserId(media->user_id_)));
    }
    case telegram_api::messageMediaDocument::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaDocument>(media_ptr);
      if (media->document_ == nullptr) {
        if ((media->flags_ & telegram_api::messageMediaDocument::TTL_SECONDS_MASK) == 0) {
          LOG(ERROR) << "Receive messageMediaDocument without document and TTL: " << oneline(to_string(media));
          break;
        }

        return make_unique<MessageExpiredVideo>();
      }

      auto document_ptr = std::move(media->document_);
      int32 document_id = document_ptr->get_id();
      if (document_id == telegram_api::documentEmpty::ID) {
        break;
      }
      CHECK(document_id == telegram_api::document::ID);

      if (ttl != nullptr && (media->flags_ & telegram_api::messageMediaDocument::TTL_SECONDS_MASK) != 0) {
        *ttl = media->ttl_seconds_;
      }
      return get_document_message_content(td, move_tl_object_as<telegram_api::document>(document_ptr), owner_dialog_id,
                                          std::move(message), is_content_read, !media->nopremium_, nullptr);
    }
    case telegram_api::messageMediaGame::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaGame>(media_ptr);

      auto m = make_unique<MessageGame>(Game(td, via_bot_user_id, std::move(media->game_), message, owner_dialog_id));
      if (m->game.is_empty()) {
        break;
      }
      return std::move(m);
    }
    case telegram_api::messageMediaInvoice::ID:
      return td::make_unique<MessageInvoice>(
          get_input_invoice(move_tl_object_as<telegram_api::messageMediaInvoice>(media_ptr), td, owner_dialog_id));
    case telegram_api::messageMediaWebPage::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaWebPage>(media_ptr);
      if (disable_web_page_preview != nullptr) {
        *disable_web_page_preview = (media->webpage_ == nullptr);
      }
      auto web_page_id = td->web_pages_manager_->on_get_web_page(std::move(media->webpage_), owner_dialog_id);
      return make_unique<MessageText>(std::move(message), web_page_id);
    }
    case telegram_api::messageMediaPoll::ID: {
      auto media = move_tl_object_as<telegram_api::messageMediaPoll>(media_ptr);
      auto poll_id = td->poll_manager_->on_get_poll(PollId(), std::move(media->poll_), std::move(media->results_),
                                                    "messageMediaPoll");
      if (!poll_id.is_valid()) {
        break;
      }
      return make_unique<MessagePoll>(poll_id);
    }
    case telegram_api::messageMediaUnsupported::ID:
      return make_unique<MessageUnsupported>();
    default:
      UNREACHABLE();
  }

  // explicit empty media message
  if (disable_web_page_preview != nullptr) {
    *disable_web_page_preview = true;
  }
  return make_unique<MessageText>(std::move(message), WebPageId());
}

unique_ptr<MessageContent> dup_message_content(Td *td, DialogId dialog_id, const MessageContent *content,
                                               MessageContentDupType type, MessageCopyOptions &&copy_options) {
  CHECK(content != nullptr);
  if (copy_options.send_copy) {
    CHECK(type == MessageContentDupType::Copy || type == MessageContentDupType::ServerCopy);
  }
  if (type != MessageContentDupType::Forward && type != MessageContentDupType::SendViaBot &&
      !can_have_input_media(td, content, type == MessageContentDupType::ServerCopy)) {
    return nullptr;
  }

  bool to_secret = dialog_id.get_type() == DialogType::SecretChat;
  auto fix_file_id = [dialog_id, to_secret, file_manager = td->file_manager_.get()](FileId file_id) {
    auto file_view = file_manager->get_file_view(file_id);
    if (to_secret && !file_view.is_encrypted_secret()) {
      auto download_file_id = file_manager->dup_file_id(file_id);
      file_id = file_manager
                    ->register_generate(FileType::Encrypted, FileLocationSource::FromServer, file_view.suggested_path(),
                                        PSTRING() << "#file_id#" << download_file_id.get(), dialog_id, file_view.size())
                    .ok();
    }
    return file_manager->dup_file_id(file_id);
  };

  FileId thumbnail_file_id;
  if (to_secret) {
    thumbnail_file_id = get_message_content_thumbnail_file_id(content, td);
  }
  auto replace_caption = (type == MessageContentDupType::Copy || type == MessageContentDupType::ServerCopy) &&
                         copy_options.replace_caption;
  switch (content->get_type()) {
    case MessageContentType::Animation: {
      auto result = make_unique<MessageAnimation>(*static_cast<const MessageAnimation *>(content));
      if (replace_caption) {
        result->caption = std::move(copy_options.new_caption);
      }
      if (td->documents_manager_->has_input_media(result->file_id, thumbnail_file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->animations_manager_->dup_animation(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
    case MessageContentType::Audio: {
      auto result = make_unique<MessageAudio>(*static_cast<const MessageAudio *>(content));
      if (replace_caption) {
        result->caption = std::move(copy_options.new_caption);
      }
      if (td->documents_manager_->has_input_media(result->file_id, thumbnail_file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->audios_manager_->dup_audio(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
    case MessageContentType::Contact:
      return make_unique<MessageContact>(*static_cast<const MessageContact *>(content));
    case MessageContentType::Dice: {
      auto result = td::make_unique<MessageDice>(*static_cast<const MessageDice *>(content));
      if (type != MessageContentDupType::Forward) {
        result->dice_value = 0;
      }
      return std::move(result);
    }
    case MessageContentType::Document: {
      auto result = make_unique<MessageDocument>(*static_cast<const MessageDocument *>(content));
      if (replace_caption) {
        result->caption = std::move(copy_options.new_caption);
      }
      if (td->documents_manager_->has_input_media(result->file_id, thumbnail_file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->documents_manager_->dup_document(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
    case MessageContentType::Game:
      return make_unique<MessageGame>(*static_cast<const MessageGame *>(content));
    case MessageContentType::Invoice:
      if (type == MessageContentDupType::Copy) {
        return nullptr;
      }
      return make_unique<MessageInvoice>(*static_cast<const MessageInvoice *>(content));
    case MessageContentType::LiveLocation:
      if (!to_secret && (type == MessageContentDupType::Send || type == MessageContentDupType::SendViaBot)) {
        return make_unique<MessageLiveLocation>(*static_cast<const MessageLiveLocation *>(content));
      } else {
        return make_unique<MessageLocation>(Location(static_cast<const MessageLiveLocation *>(content)->location));
      }
    case MessageContentType::Location:
      return make_unique<MessageLocation>(*static_cast<const MessageLocation *>(content));
    case MessageContentType::Photo: {
      auto result = make_unique<MessagePhoto>(*static_cast<const MessagePhoto *>(content));
      if (replace_caption) {
        result->caption = std::move(copy_options.new_caption);
      }

      CHECK(!result->photo.photos.empty());
      if ((result->photo.photos.size() > 2 || result->photo.photos.back().type != 'i') && !to_secret) {
        // already sent photo
        // having remote location is not enough to have InputMedia, because the file may not have valid file_reference
        // also file_id needs to be duped, because upload can be called to repair the file_reference and every upload
        // request must have unique file_id
        if (!td->auth_manager_->is_bot()) {
          result->photo.photos.back().file_id = fix_file_id(result->photo.photos.back().file_id);
        }
        return std::move(result);
      }

      // Find 'i' or largest
      PhotoSize photo;
      for (const auto &size : result->photo.photos) {
        if (size.type == 'i') {
          photo = size;
        }
      }
      if (photo.type == 0) {
        for (const auto &size : result->photo.photos) {
          if (photo.type == 0 || photo < size) {
            photo = size;
          }
        }
      }

      // Find 't' or smallest
      PhotoSize thumbnail;
      for (const auto &size : result->photo.photos) {
        if (size.type == 't') {
          thumbnail = size;
        }
      }
      if (thumbnail.type == 0) {
        for (const auto &size : result->photo.photos) {
          if (size.type != photo.type && (thumbnail.type == 0 || size < thumbnail)) {
            thumbnail = size;
          }
        }
      }

      result->photo.photos.clear();
      bool has_thumbnail = thumbnail.type != 0;
      if (has_thumbnail) {
        thumbnail.type = 't';
        result->photo.photos.push_back(std::move(thumbnail));
      }
      photo.type = 'i';
      result->photo.photos.push_back(std::move(photo));

      if (photo_has_input_media(td->file_manager_.get(), result->photo, to_secret, td->auth_manager_->is_bot())) {
        return std::move(result);
      }

      result->photo.photos.back().file_id = fix_file_id(result->photo.photos.back().file_id);
      if (has_thumbnail) {
        result->photo.photos[0].file_id = td->file_manager_->dup_file_id(result->photo.photos[0].file_id);
      }
      return std::move(result);
    }
    case MessageContentType::Poll:
      if (type == MessageContentDupType::Copy || type == MessageContentDupType::ServerCopy) {
        return make_unique<MessagePoll>(
            td->poll_manager_->dup_poll(static_cast<const MessagePoll *>(content)->poll_id));
      } else {
        return make_unique<MessagePoll>(*static_cast<const MessagePoll *>(content));
      }
    case MessageContentType::Sticker: {
      auto result = make_unique<MessageSticker>(*static_cast<const MessageSticker *>(content));
      result->is_premium = G()->shared_config().get_option_boolean("is_premium");
      if (td->stickers_manager_->has_input_media(result->file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->stickers_manager_->dup_sticker(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
    case MessageContentType::Text:
      return make_unique<MessageText>(*static_cast<const MessageText *>(content));
    case MessageContentType::Venue:
      return make_unique<MessageVenue>(*static_cast<const MessageVenue *>(content));
    case MessageContentType::Video: {
      auto result = make_unique<MessageVideo>(*static_cast<const MessageVideo *>(content));
      if (replace_caption) {
        result->caption = std::move(copy_options.new_caption);
      }
      if (td->documents_manager_->has_input_media(result->file_id, thumbnail_file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->videos_manager_->dup_video(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
    case MessageContentType::VideoNote: {
      auto result = make_unique<MessageVideoNote>(*static_cast<const MessageVideoNote *>(content));
      result->is_viewed = false;
      if (td->documents_manager_->has_input_media(result->file_id, thumbnail_file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->video_notes_manager_->dup_video_note(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
    case MessageContentType::VoiceNote: {
      auto result = make_unique<MessageVoiceNote>(*static_cast<const MessageVoiceNote *>(content));
      if (replace_caption) {
        result->caption = std::move(copy_options.new_caption);
      }
      result->is_listened = false;
      if (td->documents_manager_->has_input_media(result->file_id, thumbnail_file_id, to_secret)) {
        return std::move(result);
      }
      result->file_id = td->voice_notes_manager_->dup_voice_note(fix_file_id(result->file_id), result->file_id);
      CHECK(result->file_id.is_valid());
      return std::move(result);
    }
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
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
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
      return nullptr;
    default:
      UNREACHABLE();
  }
  UNREACHABLE();
  return nullptr;
}

unique_ptr<MessageContent> get_action_message_content(Td *td, tl_object_ptr<telegram_api::MessageAction> &&action_ptr,
                                                      DialogId owner_dialog_id, DialogId reply_in_dialog_id,
                                                      MessageId reply_to_message_id) {
  CHECK(action_ptr != nullptr);

  switch (action_ptr->get_id()) {
    case telegram_api::messageActionEmpty::ID:
      LOG(ERROR) << "Receive empty message action in " << owner_dialog_id;
      break;
    case telegram_api::messageActionChatCreate::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChatCreate>(action_ptr);

      vector<UserId> participant_user_ids;
      participant_user_ids.reserve(action->users_.size());
      for (auto &user : action->users_) {
        UserId user_id(user);
        if (user_id.is_valid()) {
          participant_user_ids.push_back(user_id);
        } else {
          LOG(ERROR) << "Receive messageActionChatCreate with invalid " << user_id << " in " << owner_dialog_id;
        }
      }

      return td::make_unique<MessageChatCreate>(std::move(action->title_), std::move(participant_user_ids));
    }
    case telegram_api::messageActionChatEditTitle::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChatEditTitle>(action_ptr);
      return td::make_unique<MessageChatChangeTitle>(std::move(action->title_));
    }
    case telegram_api::messageActionChatEditPhoto::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChatEditPhoto>(action_ptr);
      auto photo = get_photo(td->file_manager_.get(), std::move(action->photo_), owner_dialog_id);
      if (photo.is_empty()) {
        break;
      }
      return make_unique<MessageChatChangePhoto>(std::move(photo));
    }
    case telegram_api::messageActionChatDeletePhoto::ID: {
      return make_unique<MessageChatDeletePhoto>();
    }
    case telegram_api::messageActionHistoryClear::ID: {
      return make_unique<MessageChatDeleteHistory>();
    }
    case telegram_api::messageActionChatAddUser::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChatAddUser>(action_ptr);

      vector<UserId> user_ids;
      user_ids.reserve(action->users_.size());
      for (auto &user : action->users_) {
        UserId user_id(user);
        if (user_id.is_valid()) {
          user_ids.push_back(user_id);
        } else {
          LOG(ERROR) << "Receive messageActionChatAddUser with invalid " << user_id << " in " << owner_dialog_id;
        }
      }

      return td::make_unique<MessageChatAddUsers>(std::move(user_ids));
    }
    case telegram_api::messageActionChatJoinedByLink::ID:
      return make_unique<MessageChatJoinedByLink>(false);
    case telegram_api::messageActionChatDeleteUser::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChatDeleteUser>(action_ptr);

      UserId user_id(action->user_id_);
      if (!user_id.is_valid()) {
        LOG(ERROR) << "Receive messageActionChatDeleteUser with invalid " << user_id << " in " << owner_dialog_id;
        break;
      }

      return make_unique<MessageChatDeleteUser>(user_id);
    }
    case telegram_api::messageActionChatMigrateTo::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChatMigrateTo>(action_ptr);

      ChannelId migrated_to_channel_id(action->channel_id_);
      if (!migrated_to_channel_id.is_valid()) {
        LOG(ERROR) << "Receive messageActionChatMigrateTo with invalid " << migrated_to_channel_id << " in "
                   << owner_dialog_id;
        break;
      }

      return make_unique<MessageChatMigrateTo>(migrated_to_channel_id);
    }
    case telegram_api::messageActionChannelCreate::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChannelCreate>(action_ptr);
      return td::make_unique<MessageChannelCreate>(std::move(action->title_));
    }
    case telegram_api::messageActionChannelMigrateFrom::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionChannelMigrateFrom>(action_ptr);
      ChatId chat_id(action->chat_id_);
      LOG_IF(ERROR, !chat_id.is_valid()) << "Receive messageActionChannelMigrateFrom with invalid " << chat_id << " in "
                                         << owner_dialog_id;

      return td::make_unique<MessageChannelMigrateFrom>(std::move(action->title_), chat_id);
    }
    case telegram_api::messageActionPinMessage::ID: {
      if (reply_in_dialog_id.is_valid() && reply_in_dialog_id != owner_dialog_id) {
        LOG(ERROR) << "Receive pinned message with " << reply_to_message_id << " in " << owner_dialog_id
                   << " in another " << reply_in_dialog_id;
        reply_to_message_id = MessageId();
        reply_in_dialog_id = DialogId();
      }
      if (!reply_to_message_id.is_valid()) {
        // possible in basic groups
        LOG(INFO) << "Receive pinned message with " << reply_to_message_id << " in " << owner_dialog_id;
        reply_to_message_id = MessageId();
      }
      return make_unique<MessagePinMessage>(reply_to_message_id);
    }
    case telegram_api::messageActionGameScore::ID: {
      if (reply_in_dialog_id.is_valid() && reply_in_dialog_id != owner_dialog_id) {
        LOG(ERROR) << "Receive game score with " << reply_to_message_id << " in " << owner_dialog_id << " in another "
                   << reply_in_dialog_id;
        reply_to_message_id = MessageId();
        reply_in_dialog_id = DialogId();
      }
      if (!reply_to_message_id.is_valid()) {
        // possible in basic groups
        LOG(INFO) << "Receive game score with " << reply_to_message_id << " in " << owner_dialog_id;
        reply_to_message_id = MessageId();
      }
      auto action = move_tl_object_as<telegram_api::messageActionGameScore>(action_ptr);
      return make_unique<MessageGameScore>(reply_to_message_id, action->game_id_, action->score_);
    }
    case telegram_api::messageActionPhoneCall::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionPhoneCall>(action_ptr);
      auto duration =
          (action->flags_ & telegram_api::messageActionPhoneCall::DURATION_MASK) != 0 ? action->duration_ : 0;
      if (duration < 0) {
        LOG(ERROR) << "Receive invalid " << oneline(to_string(action));
        break;
      }
      return make_unique<MessageCall>(action->call_id_, duration, get_call_discard_reason(action->reason_),
                                      action->video_);
    }
    case telegram_api::messageActionPaymentSent::ID: {
      if (td->auth_manager_->is_bot()) {
        LOG(ERROR) << "Receive MessageActionPaymentSent in " << owner_dialog_id;
        break;
      }
      auto action = move_tl_object_as<telegram_api::messageActionPaymentSent>(action_ptr);
      if (!reply_to_message_id.is_valid()) {
        if (reply_to_message_id != MessageId()) {
          LOG(ERROR) << "Receive succesful payment message with " << reply_to_message_id << " in " << owner_dialog_id;
        }
        reply_in_dialog_id = DialogId();
        reply_to_message_id = MessageId();
      }
      return td::make_unique<MessagePaymentSuccessful>(
          reply_in_dialog_id, reply_to_message_id, std::move(action->currency_), action->total_amount_,
          std::move(action->invoice_slug_), action->recurring_used_, action->recurring_init_);
    }
    case telegram_api::messageActionPaymentSentMe::ID: {
      if (!td->auth_manager_->is_bot()) {
        LOG(ERROR) << "Receive MessageActionPaymentSentMe in " << owner_dialog_id;
        break;
      }
      auto action = move_tl_object_as<telegram_api::messageActionPaymentSentMe>(action_ptr);
      auto result = td::make_unique<MessagePaymentSuccessful>(DialogId(), MessageId(), std::move(action->currency_),
                                                              action->total_amount_, action->payload_.as_slice().str(),
                                                              action->recurring_used_, action->recurring_init_);
      result->shipping_option_id = std::move(action->shipping_option_id_);
      result->order_info = get_order_info(std::move(action->info_));
      result->telegram_payment_charge_id = std::move(action->charge_->id_);
      result->provider_payment_charge_id = std::move(action->charge_->provider_charge_id_);
      return std::move(result);
    }
    case telegram_api::messageActionScreenshotTaken::ID: {
      return make_unique<MessageScreenshotTaken>();
    }
    case telegram_api::messageActionCustomAction::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionCustomAction>(action_ptr);
      return td::make_unique<MessageCustomServiceAction>(std::move(action->message_));
    }
    case telegram_api::messageActionBotAllowed::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionBotAllowed>(action_ptr);
      return td::make_unique<MessageWebsiteConnected>(std::move(action->domain_));
    }
    case telegram_api::messageActionSecureValuesSent::ID: {
      LOG_IF(ERROR, td->auth_manager_->is_bot()) << "Receive MessageActionSecureValuesSent in " << owner_dialog_id;
      auto action = move_tl_object_as<telegram_api::messageActionSecureValuesSent>(action_ptr);
      return td::make_unique<MessagePassportDataSent>(get_secure_value_types(action->types_));
    }
    case telegram_api::messageActionSecureValuesSentMe::ID: {
      LOG_IF(ERROR, !td->auth_manager_->is_bot()) << "Receive MessageActionSecureValuesSentMe in " << owner_dialog_id;
      auto action = move_tl_object_as<telegram_api::messageActionSecureValuesSentMe>(action_ptr);
      return td::make_unique<MessagePassportDataReceived>(
          get_encrypted_secure_values(td->file_manager_.get(), std::move(action->values_)),
          get_encrypted_secure_credentials(std::move(action->credentials_)));
    }
    case telegram_api::messageActionContactSignUp::ID: {
      LOG_IF(ERROR, td->auth_manager_->is_bot()) << "Receive ContactRegistered in " << owner_dialog_id;
      return td::make_unique<MessageContactRegistered>();
    }
    case telegram_api::messageActionGeoProximityReached::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionGeoProximityReached>(action_ptr);
      DialogId traveler_id(action->from_id_);
      DialogId watcher_id(action->to_id_);
      int32 distance = action->distance_;
      if (!traveler_id.is_valid() || !watcher_id.is_valid() || distance < 0) {
        LOG(ERROR) << "Receive invalid " << oneline(to_string(action));
        break;
      }

      return make_unique<MessageProximityAlertTriggered>(traveler_id, watcher_id, distance);
    }
    case telegram_api::messageActionGroupCall::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionGroupCall>(action_ptr);
      int32 duration = -1;
      if ((action->flags_ & telegram_api::messageActionGroupCall::DURATION_MASK) != 0) {
        duration = action->duration_;
        if (duration < 0) {
          LOG(ERROR) << "Receive invalid " << oneline(to_string(action));
          break;
        }
      }
      return make_unique<MessageGroupCall>(InputGroupCallId(action->call_), duration, -1);
    }
    case telegram_api::messageActionInviteToGroupCall::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionInviteToGroupCall>(action_ptr);

      vector<UserId> user_ids;
      user_ids.reserve(action->users_.size());
      for (auto &user : action->users_) {
        UserId user_id(user);
        if (user_id.is_valid()) {
          user_ids.push_back(user_id);
        } else {
          LOG(ERROR) << "Receive messageActionInviteToGroupCall with invalid " << user_id << " in " << owner_dialog_id;
        }
      }

      return td::make_unique<MessageInviteToGroupCall>(InputGroupCallId(action->call_), std::move(user_ids));
    }
    case telegram_api::messageActionSetMessagesTTL::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionSetMessagesTTL>(action_ptr);
      if (action->period_ < 0) {
        LOG(ERROR) << "Receive wrong TTL = " << action->period_;
        break;
      }
      return make_unique<MessageChatSetTtl>(action->period_);
    }
    case telegram_api::messageActionGroupCallScheduled::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionGroupCallScheduled>(action_ptr);
      if (action->schedule_date_ <= 0) {
        LOG(ERROR) << "Receive wrong schedule_date = " << action->schedule_date_;
        break;
      }
      return make_unique<MessageGroupCall>(InputGroupCallId(action->call_), -1, action->schedule_date_);
    }
    case telegram_api::messageActionSetChatTheme::ID: {
      auto action = move_tl_object_as<telegram_api::messageActionSetChatTheme>(action_ptr);
      return td::make_unique<MessageChatSetTheme>(std::move(action->emoticon_));
    }
    case telegram_api::messageActionChatJoinedByRequest::ID:
      return make_unique<MessageChatJoinedByLink>(true);
    case telegram_api::messageActionWebViewDataSent::ID: {
      if (td->auth_manager_->is_bot()) {
        LOG(ERROR) << "Receive messageActionWebViewDataSent in " << owner_dialog_id;
        break;
      }
      auto action = move_tl_object_as<telegram_api::messageActionWebViewDataSent>(action_ptr);
      return td::make_unique<MessageWebViewDataSent>(std::move(action->text_));
    }
    case telegram_api::messageActionWebViewDataSentMe::ID: {
      if (!td->auth_manager_->is_bot()) {
        LOG(ERROR) << "Receive messageActionWebViewDataSentMe in " << owner_dialog_id;
        break;
      }
      auto action = move_tl_object_as<telegram_api::messageActionWebViewDataSentMe>(action_ptr);
      return td::make_unique<MessageWebViewDataReceived>(std::move(action->text_), std::move(action->data_));
    }
    default:
      UNREACHABLE();
  }
  // explicit empty or wrong action
  return make_unique<MessageText>(FormattedText(), WebPageId());
}

tl_object_ptr<td_api::MessageContent> get_message_content_object(const MessageContent *content, Td *td,
                                                                 DialogId dialog_id, int32 message_date,
                                                                 bool is_content_secret, bool skip_bot_commands,
                                                                 int32 max_media_timestamp) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case MessageContentType::Animation: {
      const auto *m = static_cast<const MessageAnimation *>(content);
      return make_tl_object<td_api::messageAnimation>(
          td->animations_manager_->get_animation_object(m->file_id),
          get_formatted_text_object(m->caption, skip_bot_commands, max_media_timestamp), is_content_secret);
    }
    case MessageContentType::Audio: {
      const auto *m = static_cast<const MessageAudio *>(content);
      return make_tl_object<td_api::messageAudio>(
          td->audios_manager_->get_audio_object(m->file_id),
          get_formatted_text_object(m->caption, skip_bot_commands, max_media_timestamp));
    }
    case MessageContentType::Contact: {
      const auto *m = static_cast<const MessageContact *>(content);
      return make_tl_object<td_api::messageContact>(m->contact.get_contact_object());
    }
    case MessageContentType::Document: {
      const auto *m = static_cast<const MessageDocument *>(content);
      return make_tl_object<td_api::messageDocument>(
          td->documents_manager_->get_document_object(m->file_id, PhotoFormat::Jpeg),
          get_formatted_text_object(m->caption, skip_bot_commands, max_media_timestamp));
    }
    case MessageContentType::Game: {
      const auto *m = static_cast<const MessageGame *>(content);
      return make_tl_object<td_api::messageGame>(m->game.get_game_object(td, skip_bot_commands));
    }
    case MessageContentType::Invoice: {
      const auto *m = static_cast<const MessageInvoice *>(content);
      return get_message_invoice_object(m->input_invoice, td);
    }
    case MessageContentType::LiveLocation: {
      const auto *m = static_cast<const MessageLiveLocation *>(content);
      auto passed = max(G()->unix_time_cached() - message_date, 0);
      auto expires_in = max(0, m->period - passed);
      auto heading = expires_in == 0 ? 0 : m->heading;
      auto proximity_alert_radius = expires_in == 0 ? 0 : m->proximity_alert_radius;
      return make_tl_object<td_api::messageLocation>(m->location.get_location_object(), m->period, expires_in, heading,
                                                     proximity_alert_radius);
    }
    case MessageContentType::Location: {
      const auto *m = static_cast<const MessageLocation *>(content);
      return make_tl_object<td_api::messageLocation>(m->location.get_location_object(), 0, 0, 0, 0);
    }
    case MessageContentType::Photo: {
      const auto *m = static_cast<const MessagePhoto *>(content);
      auto photo = get_photo_object(td->file_manager_.get(), m->photo);
      if (photo == nullptr) {
        LOG(ERROR) << "Have empty " << m->photo;
        return make_tl_object<td_api::messageExpiredPhoto>();
      }
      auto caption = get_formatted_text_object(m->caption, skip_bot_commands, max_media_timestamp);
      return make_tl_object<td_api::messagePhoto>(std::move(photo), std::move(caption), is_content_secret);
    }
    case MessageContentType::Sticker: {
      const auto *m = static_cast<const MessageSticker *>(content);
      auto sticker = td->stickers_manager_->get_sticker_object(m->file_id);
      CHECK(sticker != nullptr);
      auto is_premium = m->is_premium && sticker->premium_animation_ != nullptr;
      return make_tl_object<td_api::messageSticker>(std::move(sticker), is_premium);
    }
    case MessageContentType::Text: {
      const auto *m = static_cast<const MessageText *>(content);
      if (can_be_animated_emoji(m->text) && !m->web_page_id.is_valid()) {
        auto animated_emoji = td->stickers_manager_->get_animated_emoji_object(m->text.text);
        if (animated_emoji != nullptr) {
          return td_api::make_object<td_api::messageAnimatedEmoji>(std::move(animated_emoji), m->text.text);
        }
      }
      return make_tl_object<td_api::messageText>(
          get_formatted_text_object(m->text, skip_bot_commands, max_media_timestamp),
          td->web_pages_manager_->get_web_page_object(m->web_page_id));
    }
    case MessageContentType::Unsupported:
      return make_tl_object<td_api::messageUnsupported>();
    case MessageContentType::Venue: {
      const auto *m = static_cast<const MessageVenue *>(content);
      return make_tl_object<td_api::messageVenue>(m->venue.get_venue_object());
    }
    case MessageContentType::Video: {
      const auto *m = static_cast<const MessageVideo *>(content);
      return make_tl_object<td_api::messageVideo>(
          td->videos_manager_->get_video_object(m->file_id),
          get_formatted_text_object(m->caption, skip_bot_commands, max_media_timestamp), is_content_secret);
    }
    case MessageContentType::VideoNote: {
      const auto *m = static_cast<const MessageVideoNote *>(content);
      return make_tl_object<td_api::messageVideoNote>(td->video_notes_manager_->get_video_note_object(m->file_id),
                                                      m->is_viewed, is_content_secret);
    }
    case MessageContentType::VoiceNote: {
      const auto *m = static_cast<const MessageVoiceNote *>(content);
      return make_tl_object<td_api::messageVoiceNote>(
          td->voice_notes_manager_->get_voice_note_object(m->file_id),
          get_formatted_text_object(m->caption, skip_bot_commands, max_media_timestamp), m->is_listened);
    }
    case MessageContentType::ChatCreate: {
      const auto *m = static_cast<const MessageChatCreate *>(content);
      return make_tl_object<td_api::messageBasicGroupChatCreate>(
          m->title, td->contacts_manager_->get_user_ids_object(m->participant_user_ids, "MessageChatCreate"));
    }
    case MessageContentType::ChatChangeTitle: {
      const auto *m = static_cast<const MessageChatChangeTitle *>(content);
      return make_tl_object<td_api::messageChatChangeTitle>(m->title);
    }
    case MessageContentType::ChatChangePhoto: {
      const auto *m = static_cast<const MessageChatChangePhoto *>(content);
      auto photo = get_chat_photo_object(td->file_manager_.get(), m->photo);
      if (photo == nullptr) {
        LOG(ERROR) << "Have empty chat " << m->photo;
        return make_tl_object<td_api::messageChatDeletePhoto>();
      }
      return make_tl_object<td_api::messageChatChangePhoto>(std::move(photo));
    }
    case MessageContentType::ChatDeletePhoto:
      return make_tl_object<td_api::messageChatDeletePhoto>();
    case MessageContentType::ChatDeleteHistory:
      return make_tl_object<td_api::messageUnsupported>();
    case MessageContentType::ChatAddUsers: {
      const auto *m = static_cast<const MessageChatAddUsers *>(content);
      return make_tl_object<td_api::messageChatAddMembers>(
          td->contacts_manager_->get_user_ids_object(m->user_ids, "MessageChatAddUsers"));
    }
    case MessageContentType::ChatJoinedByLink: {
      const MessageChatJoinedByLink *m = static_cast<const MessageChatJoinedByLink *>(content);
      if (m->is_approved) {
        return make_tl_object<td_api::messageChatJoinByRequest>();
      }
      return make_tl_object<td_api::messageChatJoinByLink>();
    }
    case MessageContentType::ChatDeleteUser: {
      const auto *m = static_cast<const MessageChatDeleteUser *>(content);
      return make_tl_object<td_api::messageChatDeleteMember>(
          td->contacts_manager_->get_user_id_object(m->user_id, "MessageChatDeleteMember"));
    }
    case MessageContentType::ChatMigrateTo: {
      const auto *m = static_cast<const MessageChatMigrateTo *>(content);
      return make_tl_object<td_api::messageChatUpgradeTo>(
          td->contacts_manager_->get_supergroup_id_object(m->migrated_to_channel_id, "MessageChatUpgradeTo"));
    }
    case MessageContentType::ChannelCreate: {
      const auto *m = static_cast<const MessageChannelCreate *>(content);
      return make_tl_object<td_api::messageSupergroupChatCreate>(m->title);
    }
    case MessageContentType::ChannelMigrateFrom: {
      const auto *m = static_cast<const MessageChannelMigrateFrom *>(content);
      return make_tl_object<td_api::messageChatUpgradeFrom>(
          m->title,
          td->contacts_manager_->get_basic_group_id_object(m->migrated_from_chat_id, "MessageChatUpgradeFrom"));
    }
    case MessageContentType::PinMessage: {
      const auto *m = static_cast<const MessagePinMessage *>(content);
      return make_tl_object<td_api::messagePinMessage>(m->message_id.get());
    }
    case MessageContentType::GameScore: {
      const auto *m = static_cast<const MessageGameScore *>(content);
      return make_tl_object<td_api::messageGameScore>(m->game_message_id.get(), m->game_id, m->score);
    }
    case MessageContentType::ScreenshotTaken:
      return make_tl_object<td_api::messageScreenshotTaken>();
    case MessageContentType::ChatSetTtl: {
      const auto *m = static_cast<const MessageChatSetTtl *>(content);
      return make_tl_object<td_api::messageChatSetTtl>(m->ttl);
    }
    case MessageContentType::Call: {
      const auto *m = static_cast<const MessageCall *>(content);
      return make_tl_object<td_api::messageCall>(m->is_video, get_call_discard_reason_object(m->discard_reason),
                                                 m->duration);
    }
    case MessageContentType::PaymentSuccessful: {
      const auto *m = static_cast<const MessagePaymentSuccessful *>(content);
      if (td->auth_manager_->is_bot()) {
        return make_tl_object<td_api::messagePaymentSuccessfulBot>(
            m->currency, m->total_amount, m->is_recurring, m->is_first_recurring, m->invoice_payload,
            m->shipping_option_id, get_order_info_object(m->order_info), m->telegram_payment_charge_id,
            m->provider_payment_charge_id);
      } else {
        auto invoice_dialog_id = m->invoice_dialog_id.is_valid() ? m->invoice_dialog_id : dialog_id;
        return make_tl_object<td_api::messagePaymentSuccessful>(invoice_dialog_id.get(), m->invoice_message_id.get(),
                                                                m->currency, m->total_amount, m->is_recurring,
                                                                m->is_first_recurring, m->invoice_payload);
      }
    }
    case MessageContentType::ContactRegistered:
      return make_tl_object<td_api::messageContactRegistered>();
    case MessageContentType::ExpiredPhoto:
      return make_tl_object<td_api::messageExpiredPhoto>();
    case MessageContentType::ExpiredVideo:
      return make_tl_object<td_api::messageExpiredVideo>();
    case MessageContentType::CustomServiceAction: {
      const auto *m = static_cast<const MessageCustomServiceAction *>(content);
      return make_tl_object<td_api::messageCustomServiceAction>(m->message);
    }
    case MessageContentType::WebsiteConnected: {
      const auto *m = static_cast<const MessageWebsiteConnected *>(content);
      return make_tl_object<td_api::messageWebsiteConnected>(m->domain_name);
    }
    case MessageContentType::PassportDataSent: {
      const auto *m = static_cast<const MessagePassportDataSent *>(content);
      return make_tl_object<td_api::messagePassportDataSent>(get_passport_element_types_object(m->types));
    }
    case MessageContentType::PassportDataReceived: {
      const auto *m = static_cast<const MessagePassportDataReceived *>(content);
      return make_tl_object<td_api::messagePassportDataReceived>(
          get_encrypted_passport_element_object(td->file_manager_.get(), m->values),
          get_encrypted_credentials_object(m->credentials));
    }
    case MessageContentType::Poll: {
      const auto *m = static_cast<const MessagePoll *>(content);
      return make_tl_object<td_api::messagePoll>(td->poll_manager_->get_poll_object(m->poll_id));
    }
    case MessageContentType::Dice: {
      const auto *m = static_cast<const MessageDice *>(content);
      auto initial_state = td->stickers_manager_->get_dice_stickers_object(m->emoji, 0);
      auto final_state =
          m->dice_value == 0 ? nullptr : td->stickers_manager_->get_dice_stickers_object(m->emoji, m->dice_value);
      auto success_animation_frame_number =
          td->stickers_manager_->get_dice_success_animation_frame_number(m->emoji, m->dice_value);
      return make_tl_object<td_api::messageDice>(std::move(initial_state), std::move(final_state), m->emoji,
                                                 m->dice_value, success_animation_frame_number);
    }
    case MessageContentType::ProximityAlertTriggered: {
      const auto *m = static_cast<const MessageProximityAlertTriggered *>(content);
      return make_tl_object<td_api::messageProximityAlertTriggered>(
          get_message_sender_object(td, m->traveler_dialog_id, "messageProximityAlertTriggered 1"),
          get_message_sender_object(td, m->watcher_dialog_id, "messageProximityAlertTriggered 2"), m->distance);
    }
    case MessageContentType::GroupCall: {
      const auto *m = static_cast<const MessageGroupCall *>(content);
      if (m->duration >= 0) {
        return make_tl_object<td_api::messageVideoChatEnded>(m->duration);
      } else {
        auto group_call_id = td->group_call_manager_->get_group_call_id(m->input_group_call_id, DialogId()).get();
        if (m->schedule_date > 0) {
          return make_tl_object<td_api::messageVideoChatScheduled>(group_call_id, m->schedule_date);
        } else {
          return make_tl_object<td_api::messageVideoChatStarted>(group_call_id);
        }
      }
    }
    case MessageContentType::InviteToGroupCall: {
      const auto *m = static_cast<const MessageInviteToGroupCall *>(content);
      return make_tl_object<td_api::messageInviteVideoChatParticipants>(
          td->group_call_manager_->get_group_call_id(m->input_group_call_id, DialogId()).get(),
          td->contacts_manager_->get_user_ids_object(m->user_ids, "MessageInviteToGroupCall"));
    }
    case MessageContentType::ChatSetTheme: {
      const auto *m = static_cast<const MessageChatSetTheme *>(content);
      return make_tl_object<td_api::messageChatSetTheme>(m->emoji);
    }
    case MessageContentType::WebViewDataSent: {
      const auto *m = static_cast<const MessageWebViewDataSent *>(content);
      return make_tl_object<td_api::messageWebAppDataSent>(m->button_text);
    }
    case MessageContentType::WebViewDataReceived: {
      const auto *m = static_cast<const MessageWebViewDataReceived *>(content);
      return make_tl_object<td_api::messageWebAppDataReceived>(m->button_text, m->data);
    }
    default:
      UNREACHABLE();
      return nullptr;
  }
  UNREACHABLE();
  return nullptr;
}

FormattedText *get_message_content_text_mutable(MessageContent *content) {
  return const_cast<FormattedText *>(get_message_content_text(content));
}

const FormattedText *get_message_content_text(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Text:
      return &static_cast<const MessageText *>(content)->text;
    case MessageContentType::Game:
      return &static_cast<const MessageGame *>(content)->game.get_text();
    default:
      return get_message_content_caption(content);
  }
}

const FormattedText *get_message_content_caption(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Animation:
      return &static_cast<const MessageAnimation *>(content)->caption;
    case MessageContentType::Audio:
      return &static_cast<const MessageAudio *>(content)->caption;
    case MessageContentType::Document:
      return &static_cast<const MessageDocument *>(content)->caption;
    case MessageContentType::Photo:
      return &static_cast<const MessagePhoto *>(content)->caption;
    case MessageContentType::Video:
      return &static_cast<const MessageVideo *>(content)->caption;
    case MessageContentType::VoiceNote:
      return &static_cast<const MessageVoiceNote *>(content)->caption;
    default:
      return nullptr;
  }
}

int32 get_message_content_duration(const MessageContent *content, const Td *td) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case MessageContentType::Animation: {
      auto animation_file_id = static_cast<const MessageAnimation *>(content)->file_id;
      return td->animations_manager_->get_animation_duration(animation_file_id);
    }
    case MessageContentType::Audio: {
      auto audio_file_id = static_cast<const MessageAudio *>(content)->file_id;
      return td->audios_manager_->get_audio_duration(audio_file_id);
    }
    case MessageContentType::Video: {
      auto video_file_id = static_cast<const MessageVideo *>(content)->file_id;
      return td->videos_manager_->get_video_duration(video_file_id);
    }
    case MessageContentType::VideoNote: {
      auto video_note_file_id = static_cast<const MessageVideoNote *>(content)->file_id;
      return td->video_notes_manager_->get_video_note_duration(video_note_file_id);
    }
    case MessageContentType::VoiceNote: {
      auto voice_file_id = static_cast<const MessageVoiceNote *>(content)->file_id;
      return td->voice_notes_manager_->get_voice_note_duration(voice_file_id);
    }
    default:
      return 0;
  }
}

int32 get_message_content_media_duration(const MessageContent *content, const Td *td) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case MessageContentType::Audio: {
      auto audio_file_id = static_cast<const MessageAudio *>(content)->file_id;
      return td->audios_manager_->get_audio_duration(audio_file_id);
    }
    case MessageContentType::Text: {
      auto web_page_id = static_cast<const MessageText *>(content)->web_page_id;
      return td->web_pages_manager_->get_web_page_media_duration(web_page_id);
    }
    case MessageContentType::Video: {
      auto video_file_id = static_cast<const MessageVideo *>(content)->file_id;
      return td->videos_manager_->get_video_duration(video_file_id);
    }
    case MessageContentType::VideoNote: {
      auto video_note_file_id = static_cast<const MessageVideoNote *>(content)->file_id;
      return td->video_notes_manager_->get_video_note_duration(video_note_file_id);
    }
    case MessageContentType::VoiceNote: {
      auto voice_file_id = static_cast<const MessageVoiceNote *>(content)->file_id;
      return td->voice_notes_manager_->get_voice_note_duration(voice_file_id);
    }
    default:
      return -1;
  }
}

FileId get_message_content_upload_file_id(const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Animation:
      return static_cast<const MessageAnimation *>(content)->file_id;
    case MessageContentType::Audio:
      return static_cast<const MessageAudio *>(content)->file_id;
    case MessageContentType::Document:
      return static_cast<const MessageDocument *>(content)->file_id;
    case MessageContentType::Photo:
      for (auto &size : static_cast<const MessagePhoto *>(content)->photo.photos) {
        if (size.type == 'i') {
          return size.file_id;
        }
      }
      break;
    case MessageContentType::Sticker:
      return static_cast<const MessageSticker *>(content)->file_id;
    case MessageContentType::Video:
      return static_cast<const MessageVideo *>(content)->file_id;
    case MessageContentType::VideoNote:
      return static_cast<const MessageVideoNote *>(content)->file_id;
    case MessageContentType::VoiceNote:
      return static_cast<const MessageVoiceNote *>(content)->file_id;
    default:
      break;
  }
  return FileId();
}

FileId get_message_content_any_file_id(const MessageContent *content) {
  FileId result = get_message_content_upload_file_id(content);
  if (!result.is_valid() && content->get_type() == MessageContentType::Photo) {
    const auto &sizes = static_cast<const MessagePhoto *>(content)->photo.photos;
    if (!sizes.empty()) {
      result = sizes.back().file_id;
    }
  }
  return result;
}

void update_message_content_file_id_remote(MessageContent *content, FileId file_id) {
  if (file_id.get_remote() == 0) {
    return;
  }
  FileId *old_file_id = [&] {
    switch (content->get_type()) {
      case MessageContentType::Animation:
        return &static_cast<MessageAnimation *>(content)->file_id;
      case MessageContentType::Audio:
        return &static_cast<MessageAudio *>(content)->file_id;
      case MessageContentType::Document:
        return &static_cast<MessageDocument *>(content)->file_id;
      case MessageContentType::Sticker:
        return &static_cast<MessageSticker *>(content)->file_id;
      case MessageContentType::Video:
        return &static_cast<MessageVideo *>(content)->file_id;
      case MessageContentType::VideoNote:
        return &static_cast<MessageVideoNote *>(content)->file_id;
      case MessageContentType::VoiceNote:
        return &static_cast<MessageVoiceNote *>(content)->file_id;
      default:
        return static_cast<FileId *>(nullptr);
    }
  }();
  if (old_file_id != nullptr && *old_file_id == file_id && old_file_id->get_remote() == 0) {
    *old_file_id = file_id;
  }
}

FileId get_message_content_thumbnail_file_id(const MessageContent *content, const Td *td) {
  switch (content->get_type()) {
    case MessageContentType::Animation:
      return td->animations_manager_->get_animation_thumbnail_file_id(
          static_cast<const MessageAnimation *>(content)->file_id);
    case MessageContentType::Audio:
      return td->audios_manager_->get_audio_thumbnail_file_id(static_cast<const MessageAudio *>(content)->file_id);
    case MessageContentType::Document:
      return td->documents_manager_->get_document_thumbnail_file_id(
          static_cast<const MessageDocument *>(content)->file_id);
    case MessageContentType::Photo:
      for (auto &size : static_cast<const MessagePhoto *>(content)->photo.photos) {
        if (size.type == 't') {
          return size.file_id;
        }
      }
      break;
    case MessageContentType::Sticker:
      return td->stickers_manager_->get_sticker_thumbnail_file_id(
          static_cast<const MessageSticker *>(content)->file_id);
    case MessageContentType::Video:
      return td->videos_manager_->get_video_thumbnail_file_id(static_cast<const MessageVideo *>(content)->file_id);
    case MessageContentType::VideoNote:
      return td->video_notes_manager_->get_video_note_thumbnail_file_id(
          static_cast<const MessageVideoNote *>(content)->file_id);
    case MessageContentType::VoiceNote:
      return FileId();
    default:
      break;
  }
  return FileId();
}

static FileId get_message_content_animated_thumbnail_file_id(const MessageContent *content, const Td *td) {
  switch (content->get_type()) {
    case MessageContentType::Animation:
      return td->animations_manager_->get_animation_animated_thumbnail_file_id(
          static_cast<const MessageAnimation *>(content)->file_id);
    case MessageContentType::Video:
      return td->videos_manager_->get_video_animated_thumbnail_file_id(
          static_cast<const MessageVideo *>(content)->file_id);
    default:
      break;
  }
  return FileId();
}

vector<FileId> get_message_content_file_ids(const MessageContent *content, const Td *td) {
  switch (content->get_type()) {
    case MessageContentType::Photo:
      return photo_get_file_ids(static_cast<const MessagePhoto *>(content)->photo);
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Document:
    case MessageContentType::Video:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote: {
      vector<FileId> result;
      result.reserve(2);
      FileId file_id = get_message_content_upload_file_id(content);
      if (file_id.is_valid()) {
        result.push_back(file_id);
      }
      FileId thumbnail_file_id = get_message_content_thumbnail_file_id(content, td);
      if (thumbnail_file_id.is_valid()) {
        result.push_back(thumbnail_file_id);
      }
      FileId animated_thumbnail_file_id = get_message_content_animated_thumbnail_file_id(content, td);
      if (animated_thumbnail_file_id.is_valid()) {
        result.push_back(animated_thumbnail_file_id);
      }
      return result;
    }
    case MessageContentType::Sticker:
      return td->stickers_manager_->get_sticker_file_ids(static_cast<const MessageSticker *>(content)->file_id);
    case MessageContentType::Game:
      return static_cast<const MessageGame *>(content)->game.get_file_ids(td);
    case MessageContentType::Invoice:
      return get_input_invoice_file_ids(static_cast<const MessageInvoice *>(content)->input_invoice);
    case MessageContentType::ChatChangePhoto:
      return photo_get_file_ids(static_cast<const MessageChatChangePhoto *>(content)->photo);
    case MessageContentType::PassportDataReceived: {
      vector<FileId> result;
      for (auto &value : static_cast<const MessagePassportDataReceived *>(content)->values) {
        auto process_encrypted_secure_file = [&result](const EncryptedSecureFile &file) {
          if (file.file.file_id.is_valid()) {
            result.push_back(file.file.file_id);
          }
        };
        for (auto &file : value.files) {
          process_encrypted_secure_file(file);
        }
        process_encrypted_secure_file(value.front_side);
        process_encrypted_secure_file(value.reverse_side);
        process_encrypted_secure_file(value.selfie);
        for (auto &file : value.translations) {
          process_encrypted_secure_file(file);
        }
      }
      return result;
    }
    default:
      return {};
  }
}

string get_message_content_search_text(const Td *td, const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Text: {
      const auto *text = static_cast<const MessageText *>(content);
      if (!text->web_page_id.is_valid()) {
        return text->text.text;
      }
      return PSTRING() << text->text.text << " " << td->web_pages_manager_->get_web_page_search_text(text->web_page_id);
    }
    case MessageContentType::Animation: {
      const auto *animation = static_cast<const MessageAnimation *>(content);
      return PSTRING() << td->animations_manager_->get_animation_search_text(animation->file_id) << " "
                       << animation->caption.text;
    }
    case MessageContentType::Audio: {
      const auto *audio = static_cast<const MessageAudio *>(content);
      return PSTRING() << td->audios_manager_->get_audio_search_text(audio->file_id) << " " << audio->caption.text;
    }
    case MessageContentType::Document: {
      const auto *document = static_cast<const MessageDocument *>(content);
      return PSTRING() << td->documents_manager_->get_document_search_text(document->file_id) << " "
                       << document->caption.text;
    }
    case MessageContentType::Photo: {
      const auto *photo = static_cast<const MessagePhoto *>(content);
      return photo->caption.text;
    }
    case MessageContentType::Video: {
      const auto *video = static_cast<const MessageVideo *>(content);
      return PSTRING() << td->videos_manager_->get_video_search_text(video->file_id) << " " << video->caption.text;
    }
    case MessageContentType::Poll: {
      const auto *poll = static_cast<const MessagePoll *>(content);
      return td->poll_manager_->get_poll_search_text(poll->poll_id);
    }
    case MessageContentType::Contact:
    case MessageContentType::Game:
    case MessageContentType::Invoice:
    case MessageContentType::LiveLocation:
    case MessageContentType::Location:
    case MessageContentType::Sticker:
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
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
    case MessageContentType::CustomServiceAction:
    case MessageContentType::WebsiteConnected:
    case MessageContentType::PassportDataSent:
    case MessageContentType::PassportDataReceived:
    case MessageContentType::Dice:
    case MessageContentType::ProximityAlertTriggered:
    case MessageContentType::GroupCall:
    case MessageContentType::InviteToGroupCall:
    case MessageContentType::ChatSetTheme:
    case MessageContentType::WebViewDataSent:
    case MessageContentType::WebViewDataReceived:
      return string();
    default:
      UNREACHABLE();
      return string();
  }
}

void get_message_content_animated_emoji_click_sticker(const MessageContent *content, FullMessageId full_message_id,
                                                      Td *td, Promise<td_api::object_ptr<td_api::sticker>> &&promise) {
  if (content->get_type() != MessageContentType::Text) {
    return promise.set_error(Status::Error(400, "Message is not an animated emoji message"));
  }

  const auto &text = static_cast<const MessageText *>(content)->text;
  if (!can_be_animated_emoji(text)) {
    return promise.set_error(Status::Error(400, "Message is not an animated emoji message"));
  }
  td->stickers_manager_->get_animated_emoji_click_sticker(text.text, full_message_id, std::move(promise));
}

void on_message_content_animated_emoji_clicked(const MessageContent *content, FullMessageId full_message_id, Td *td,
                                               string &&emoji, string &&data) {
  if (content->get_type() != MessageContentType::Text) {
    return;
  }

  remove_emoji_modifiers_in_place(emoji);
  auto &text = static_cast<const MessageText *>(content)->text;
  if (!text.entities.empty() || remove_emoji_modifiers(text.text) != emoji) {
    return;
  }
  auto error = td->stickers_manager_->on_animated_emoji_message_clicked(std::move(emoji), full_message_id, data);
  if (error.is_error()) {
    LOG(WARNING) << "Failed to process animated emoji click with data \"" << data << "\": " << error;
  }
}

bool need_reget_message_content(const MessageContent *content) {
  CHECK(content != nullptr);
  switch (content->get_type()) {
    case MessageContentType::Unsupported: {
      const auto *m = static_cast<const MessageUnsupported *>(content);
      return m->version != MessageUnsupported::CURRENT_VERSION;
    }
    default:
      return false;
  }
}

bool need_delay_message_content_notification(const MessageContent *content, UserId my_user_id) {
  switch (content->get_type()) {
    case MessageContentType::ChatChangeTitle:
    case MessageContentType::ChatChangePhoto:
    case MessageContentType::ChatDeletePhoto:
    case MessageContentType::ChatJoinedByLink:
      return true;
    case MessageContentType::ChatAddUsers: {
      auto &added_user_ids = static_cast<const MessageChatAddUsers *>(content)->user_ids;
      return !td::contains(added_user_ids, my_user_id);
    }
    case MessageContentType::ChatDeleteUser:
      return static_cast<const MessageChatDeleteUser *>(content)->user_id != my_user_id;
    default:
      return false;
  }
}

void update_expired_message_content(unique_ptr<MessageContent> &content) {
  switch (content->get_type()) {
    case MessageContentType::Photo:
      content = make_unique<MessageExpiredPhoto>();
      break;
    case MessageContentType::Video:
      content = make_unique<MessageExpiredVideo>();
      break;
    case MessageContentType::Unsupported:
      // can happen if message content file identifier is broken
      break;
    case MessageContentType::ExpiredPhoto:
    case MessageContentType::ExpiredVideo:
      // can happen if message content has been reget from somewhere
      break;
    case MessageContentType::Animation:
    case MessageContentType::Audio:
    case MessageContentType::Document:
    case MessageContentType::Sticker:
    case MessageContentType::VideoNote:
    case MessageContentType::VoiceNote:
      // can happen if server will send a document with a wrong content
      content = make_unique<MessageExpiredVideo>();
      break;
    default:
      UNREACHABLE();
  }
}

void update_failed_to_send_message_content(Td *td, unique_ptr<MessageContent> &content) {
  // do not forget about failed to send message forwards
  switch (content->get_type()) {
    case MessageContentType::Poll: {
      const auto *message_poll = static_cast<const MessagePoll *>(content.get());
      if (PollManager::is_local_poll_id(message_poll->poll_id)) {
        td->poll_manager_->stop_local_poll(message_poll->poll_id);
      }
      break;
    }
    default:
      // nothing to do
      break;
  }
}

void add_message_content_dependencies(Dependencies &dependencies, const MessageContent *message_content) {
  switch (message_content->get_type()) {
    case MessageContentType::Text: {
      const auto *content = static_cast<const MessageText *>(message_content);
      dependencies.add(content->web_page_id);
      break;
    }
    case MessageContentType::Animation:
      break;
    case MessageContentType::Audio:
      break;
    case MessageContentType::Contact: {
      const auto *content = static_cast<const MessageContact *>(message_content);
      dependencies.add(content->contact.get_user_id());
      break;
    }
    case MessageContentType::Document:
      break;
    case MessageContentType::Game: {
      const auto *content = static_cast<const MessageGame *>(message_content);
      dependencies.add(content->game.get_bot_user_id());
      break;
    }
    case MessageContentType::Invoice:
      break;
    case MessageContentType::LiveLocation:
      break;
    case MessageContentType::Location:
      break;
    case MessageContentType::Photo:
      break;
    case MessageContentType::Sticker:
      break;
    case MessageContentType::Venue:
      break;
    case MessageContentType::Video:
      break;
    case MessageContentType::VideoNote:
      break;
    case MessageContentType::VoiceNote:
      break;
    case MessageContentType::ChatCreate: {
      const auto *content = static_cast<const MessageChatCreate *>(message_content);
      for (auto &participant_user_id : content->participant_user_ids) {
        dependencies.add(participant_user_id);
      }
      break;
    }
    case MessageContentType::ChatChangeTitle:
      break;
    case MessageContentType::ChatChangePhoto:
      break;
    case MessageContentType::ChatDeletePhoto:
      break;
    case MessageContentType::ChatDeleteHistory:
      break;
    case MessageContentType::ChatAddUsers: {
      const auto *content = static_cast<const MessageChatAddUsers *>(message_content);
      for (auto &user_id : content->user_ids) {
        dependencies.add(user_id);
      }
      break;
    }
    case MessageContentType::ChatJoinedByLink:
      break;
    case MessageContentType::ChatDeleteUser: {
      const auto *content = static_cast<const MessageChatDeleteUser *>(message_content);
      dependencies.add(content->user_id);
      break;
    }
    case MessageContentType::ChatMigrateTo: {
      const auto *content = static_cast<const MessageChatMigrateTo *>(message_content);
      dependencies.add(content->migrated_to_channel_id);
      break;
    }
    case MessageContentType::ChannelCreate:
      break;
    case MessageContentType::ChannelMigrateFrom: {
      const auto *content = static_cast<const MessageChannelMigrateFrom *>(message_content);
      dependencies.add(content->migrated_from_chat_id);
      break;
    }
    case MessageContentType::PinMessage:
      break;
    case MessageContentType::GameScore:
      break;
    case MessageContentType::ScreenshotTaken:
      break;
    case MessageContentType::ChatSetTtl:
      break;
    case MessageContentType::Unsupported:
      break;
    case MessageContentType::Call:
      break;
    case MessageContentType::PaymentSuccessful: {
      const auto *content = static_cast<const MessagePaymentSuccessful *>(message_content);
      dependencies.add_dialog_and_dependencies(content->invoice_dialog_id);
      break;
    }
    case MessageContentType::ContactRegistered:
      break;
    case MessageContentType::ExpiredPhoto:
      break;
    case MessageContentType::ExpiredVideo:
      break;
    case MessageContentType::CustomServiceAction:
      break;
    case MessageContentType::WebsiteConnected:
      break;
    case MessageContentType::PassportDataSent:
      break;
    case MessageContentType::PassportDataReceived:
      break;
    case MessageContentType::Poll:
      // no need to add poll dependencies, because they are forcely loaded with the poll
      break;
    case MessageContentType::Dice:
      break;
    case MessageContentType::ProximityAlertTriggered: {
      const auto *content = static_cast<const MessageProximityAlertTriggered *>(message_content);
      dependencies.add_message_sender_dependencies(content->traveler_dialog_id);
      dependencies.add_message_sender_dependencies(content->watcher_dialog_id);
      break;
    }
    case MessageContentType::GroupCall:
      break;
    case MessageContentType::InviteToGroupCall: {
      const auto *content = static_cast<const MessageInviteToGroupCall *>(message_content);
      for (auto &user_id : content->user_ids) {
        dependencies.add(user_id);
      }
      break;
    }
    case MessageContentType::ChatSetTheme:
      break;
    case MessageContentType::WebViewDataSent:
      break;
    case MessageContentType::WebViewDataReceived:
      break;
    default:
      UNREACHABLE();
      break;
  }
  add_formatted_text_dependencies(dependencies, get_message_content_text(message_content));
}

void on_sent_message_content(Td *td, const MessageContent *content) {
  switch (content->get_type()) {
    case MessageContentType::Animation:
      return td->animations_manager_->add_saved_animation_by_id(get_message_content_any_file_id(content));
    case MessageContentType::Sticker:
      return td->stickers_manager_->add_recent_sticker_by_id(false, get_message_content_any_file_id(content));
    default:
      // nothing to do
      return;
  }
}

bool is_unsent_animated_emoji_click(Td *td, DialogId dialog_id, const DialogAction &action) {
  auto emoji = action.get_watching_animations_emoji();
  if (emoji.empty()) {
    // not a WatchingAnimations action
    return false;
  }
  return !td->stickers_manager_->is_sent_animated_emoji_click(dialog_id, remove_emoji_modifiers(emoji));
}

void init_stickers_manager(Td *td) {
  td->stickers_manager_->init();
}

void on_dialog_used(TopDialogCategory category, DialogId dialog_id, int32 date) {
  send_closure(G()->top_dialog_manager(), &TopDialogManager::on_dialog_used, category, dialog_id, date);
}

void update_used_hashtags(Td *td, const MessageContent *content) {
  const FormattedText *text = get_message_content_text(content);
  if (text == nullptr || text->text.empty()) {
    return;
  }

  const unsigned char *ptr = Slice(text->text).ubegin();
  const unsigned char *end = Slice(text->text).uend();
  int32 utf16_pos = 0;
  for (auto &entity : text->entities) {
    if (entity.type != MessageEntity::Type::Hashtag) {
      continue;
    }
    while (utf16_pos < entity.offset && ptr < end) {
      utf16_pos += 1 + (ptr[0] >= 0xf0);
      ptr = next_utf8_unsafe(ptr, nullptr, "update_used_hashtags");
    }
    CHECK(utf16_pos == entity.offset);
    auto from = ptr;

    while (utf16_pos < entity.offset + entity.length && ptr < end) {
      utf16_pos += 1 + (ptr[0] >= 0xf0);
      ptr = next_utf8_unsafe(ptr, nullptr, "update_used_hashtags 2");
    }
    CHECK(utf16_pos == entity.offset + entity.length);
    auto to = ptr;

    send_closure(td->hashtag_hints_, &HashtagHints::hashtag_used, Slice(from + 1, to).str());
  }
}

}  // namespace td
