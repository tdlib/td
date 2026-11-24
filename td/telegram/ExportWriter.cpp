//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportWriter.h"

#include "td/telegram/CountryInfoManager.h"
#include "td/telegram/ExportHtmlWriter.h"
#include "td/telegram/ExportJsonWriter.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/UserId.h"

#include "td/utils/logging.h"
#include "td/utils/port/Clocks.h"
#include "td/utils/port/config.h"
#include "td/utils/port/path.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"

#include <ctime>

namespace td {

Slice get_export_about_text(ExportAbout about) {
  switch (about) {
    case ExportAbout::Telegram:
      return Slice(
          "Here is the data you requested. Remember: Telegram is ad free, it doesn't use your data for ad targeting "
          "and doesn't sell it to others. Telegram only keeps the information it needs to function as a secure and "
          "feature-rich cloud service.\n\nCheck out Settings > Privacy & Security on Telegram's mobile apps for the "
          "relevant settings.");
    case ExportAbout::Chats:
      return Slice("This page lists all chats from this export.");
    case ExportAbout::LeftChats:
      return Slice(
          "Below are the supergroups and channels from this export that you've left or where you were "
          "banned.\n\nNote that when you leave a channel or supergroup you've created, you have the option to either "
          "delete it, or simply leave (in case you want to rejoin later, or keep the community alive despite not "
          "being a member).");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static int64 floor_div(int64 a, int64 b) {
  return a >= 0 ? a / b : (a - b + 1) / b;
}

static int32 get_system_utc_time_offset(int64 date) {
  auto time = static_cast<std::time_t>(date);
  std::tm local_time{};
  std::tm utc_time{};
#if TD_PORT_WINDOWS
  auto is_converted = localtime_s(&local_time, &time) == 0 && gmtime_s(&utc_time, &time) == 0;
#else
  auto is_converted = localtime_r(&time, &local_time) != nullptr && gmtime_r(&time, &utc_time) != nullptr;
#endif
  if (!is_converted) {
    // dates before 1970 can't be converted on Windows
    return Clocks::tz_offset();
  }
  int32 offset = ((local_time.tm_hour - utc_time.tm_hour) * 60 + local_time.tm_min - utc_time.tm_min) * 60 +
                 local_time.tm_sec - utc_time.tm_sec;
  // the local date differs from the UTC one by at most a day
  auto day_difference = local_time.tm_year != utc_time.tm_year ? local_time.tm_year - utc_time.tm_year
                                                               : local_time.tm_yday - utc_time.tm_yday;
  if (day_difference > 0) {
    offset += 86400;
  } else if (day_difference < 0) {
    offset -= 86400;
  }
  return offset;
}

int32 get_export_utc_time_offset(int64 date, int32 utc_time_offset) {
  // the option "utc_time_offset" is Clocks::tz_offset(), which is calculated once, unless the application changed it
  if (utc_time_offset == Clocks::tz_offset()) {
    return get_system_utc_time_offset(date);
  }
  // the application keeps the option current, so it is compared with the current offset of the system time zone
  auto now = static_cast<int64>(Clocks::system());
  return utc_time_offset + get_system_utc_time_offset(date) - get_system_utc_time_offset(now);
}

int64 get_export_local_time(int64 date, int32 utc_time_offset) {
  return date + get_export_utc_time_offset(date, utc_time_offset);
}

ExportDateTime get_export_date_time(int64 local_time) {
  auto days = floor_div(local_time, 86400);
  auto seconds = local_time - days * 86400;

  days += 719468;
  auto era = floor_div(days, 146097);
  auto day_of_era = days - era * 146097;
  auto year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
  auto year = year_of_era + era * 400;
  auto day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  auto shifted_month = (5 * day_of_year + 2) / 153;
  auto day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
  auto month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  if (month <= 2) {
    year++;
  }
  return {year, month, day, seconds};
}

static void append_export_year(string &result, int64 year) {
  if (0 <= year && year <= 9999) {
    result += static_cast<char>('0' + year / 1000);
    result += static_cast<char>('0' + year / 100 % 10);
    result += static_cast<char>('0' + year / 10 % 10);
    result += static_cast<char>('0' + year % 10);
  } else {
    result += to_string(year);
  }
}

static void append_export_two_digits(string &result, int64 value) {
  result += static_cast<char>('0' + value / 10);
  result += static_cast<char>('0' + value % 10);
}

string get_export_date_time_string(int64 local_time) {
  auto date_time = get_export_date_time(local_time);
  string result;
  append_export_year(result, date_time.year);
  result += '-';
  append_export_two_digits(result, date_time.month);
  result += '-';
  append_export_two_digits(result, date_time.day);
  result += 'T';
  append_export_two_digits(result, date_time.seconds / 3600);
  result += ':';
  append_export_two_digits(result, date_time.seconds / 60 % 60);
  result += ':';
  append_export_two_digits(result, date_time.seconds % 60);
  return result;
}

string get_export_file_date_time_string(int64 local_time) {
  auto date_time = get_export_date_time(local_time);
  string result;
  append_export_two_digits(result, date_time.day);
  result += '-';
  append_export_two_digits(result, date_time.month);
  result += '-';
  append_export_year(result, date_time.year);
  result += '_';
  append_export_two_digits(result, date_time.seconds / 3600);
  result += '-';
  append_export_two_digits(result, date_time.seconds / 60 % 60);
  result += '-';
  append_export_two_digits(result, date_time.seconds % 60);
  return result;
}

int64 get_export_file_size(const td_api::file *file) {
  if (file == nullptr) {
    return 0;
  }
  return file->size_ != 0 ? file->size_ : file->expected_size_;
}

const td_api::photoSize *get_export_photo_size(const vector<td_api::object_ptr<td_api::photoSize>> &sizes) {
  const td_api::photoSize *result = nullptr;
  int64 max_area = 0;
  for (const auto &size : sizes) {
    if (size == nullptr) {
      continue;
    }
    auto area = static_cast<int64>(size->width_) * size->height_;
    if (area > max_area) {
      result = size.get();
      max_area = area;
    }
  }
  return result;
}

const td_api::photoSize *get_export_photo_thumbnail_size(const vector<td_api::object_ptr<td_api::photoSize>> &sizes,
                                                         int32 box_size) {
  const auto *largest = get_export_photo_size(sizes);
  if (largest == nullptr) {
    return nullptr;
  }
  auto needed_size = td::min(td::max(largest->width_, largest->height_), box_size);
  const td_api::photoSize *result = nullptr;
  for (const auto &size : sizes) {
    if (size == nullptr || td::max(size->width_, size->height_) < needed_size) {
      continue;
    }
    if (result == nullptr ||
        static_cast<int64>(size->width_) * size->height_ < static_cast<int64>(result->width_) * result->height_) {
      result = size.get();
    }
  }
  return result == largest ? nullptr : result;
}

const td_api::formattedText *get_export_message_text(const td_api::MessageContent *content) {
  switch (content->get_id()) {
    case td_api::messageText::ID:
      return static_cast<const td_api::messageText *>(content)->text_.get();
    case td_api::messageAnimation::ID:
      return static_cast<const td_api::messageAnimation *>(content)->caption_.get();
    case td_api::messageAudio::ID:
      return static_cast<const td_api::messageAudio *>(content)->caption_.get();
    case td_api::messageDocument::ID:
      return static_cast<const td_api::messageDocument *>(content)->caption_.get();
    case td_api::messagePaidMedia::ID:
      return static_cast<const td_api::messagePaidMedia *>(content)->caption_.get();
    case td_api::messagePhoto::ID:
      return static_cast<const td_api::messagePhoto *>(content)->caption_.get();
    case td_api::messageVideo::ID:
      return static_cast<const td_api::messageVideo *>(content)->caption_.get();
    case td_api::messageVoiceNote::ID:
      return static_cast<const td_api::messageVoiceNote *>(content)->caption_.get();
    case td_api::messageGame::ID: {
      const auto &game = static_cast<const td_api::messageGame *>(content)->game_;
      return game == nullptr ? nullptr : game->text_.get();
    }
    case td_api::messagePoll::ID:
      return static_cast<const td_api::messagePoll *>(content)->description_.get();
    case td_api::messageWidget::ID:
      return static_cast<const td_api::messageWidget *>(content)->caption_.get();
    default:
      return nullptr;
  }
}

ExportContentKind get_export_content_kind(const td_api::MessageContent *content) {
  if (content == nullptr) {
    return ExportContentKind::Unsupported;
  }
  switch (content->get_id()) {
    case td_api::messageText::ID:
    case td_api::messageAnimation::ID:
    case td_api::messageAudio::ID:
    case td_api::messageDocument::ID:
    case td_api::messagePaidMedia::ID:
    case td_api::messagePhoto::ID:
    case td_api::messageSticker::ID:
    case td_api::messageVideo::ID:
    case td_api::messageVideoNote::ID:
    case td_api::messageVoiceNote::ID:
    case td_api::messageExpiredPhoto::ID:
    case td_api::messageExpiredVideo::ID:
    case td_api::messageExpiredVideoNote::ID:
    case td_api::messageExpiredVoiceNote::ID:
    case td_api::messageLiveLocation::ID:
    case td_api::messageLocation::ID:
    case td_api::messageVenue::ID:
    case td_api::messageContact::ID:
    case td_api::messageAnimatedEmoji::ID:
    case td_api::messageDice::ID:
    case td_api::messageGame::ID:
    case td_api::messagePoll::ID:
    case td_api::messageStakeDice::ID:
    case td_api::messageStory::ID:
    case td_api::messageChecklist::ID:
    case td_api::messageInvoice::ID:
    case td_api::messageGiveaway::ID:
    case td_api::messageGiveawayWinners::ID:
    case td_api::messageWidget::ID:
      return ExportContentKind::Ordinary;
    case td_api::messageCall::ID:
    case td_api::messageGroupCall::ID:
    case td_api::messageVideoChatScheduled::ID:
    case td_api::messageVideoChatStarted::ID:
    case td_api::messageVideoChatEnded::ID:
    case td_api::messageInviteVideoChatParticipants::ID:
    case td_api::messagePollOptionAdded::ID:
    case td_api::messagePollOptionDeleted::ID:
    case td_api::messageBasicGroupChatCreate::ID:
    case td_api::messageSupergroupChatCreate::ID:
    case td_api::messageChatChangeTitle::ID:
    case td_api::messageChatChangePhoto::ID:
    case td_api::messageChatDeletePhoto::ID:
    case td_api::messageChatOwnerLeft::ID:
    case td_api::messageChatOwnerChanged::ID:
    case td_api::messageChatHasProtectedContentToggled::ID:
    case td_api::messageChatHasProtectedContentDisableRequested::ID:
    case td_api::messageChatAddMembers::ID:
    case td_api::messageChatJoinByLink::ID:
    case td_api::messageChatJoinByRequest::ID:
    case td_api::messageChatJoinFromCommunity::ID:
    case td_api::messageChatAddedToCommunity::ID:
    case td_api::messageChatRemovedFromCommunity::ID:
    case td_api::messageChatDeleteMember::ID:
    case td_api::messageChatUpgradeTo::ID:
    case td_api::messageChatUpgradeFrom::ID:
    case td_api::messagePinMessage::ID:
    case td_api::messageScreenshotTaken::ID:
    case td_api::messageChatSetBackground::ID:
    case td_api::messageChatSetTheme::ID:
    case td_api::messageChatSetMessageAutoDeleteTime::ID:
    case td_api::messageChatBoost::ID:
    case td_api::messageForumTopicCreated::ID:
    case td_api::messageForumTopicEdited::ID:
    case td_api::messageForumTopicIsClosedToggled::ID:
    case td_api::messageForumTopicIsHiddenToggled::ID:
    case td_api::messageSuggestProfilePhoto::ID:
    case td_api::messageSuggestBirthdate::ID:
    case td_api::messageCustomServiceAction::ID:
    case td_api::messageGameScore::ID:
    case td_api::messageManagedBotCreated::ID:
    case td_api::messagePaymentSuccessful::ID:
    case td_api::messagePaymentRefunded::ID:
    case td_api::messageGiftedPremium::ID:
    case td_api::messagePremiumGiftCode::ID:
    case td_api::messageGiveawayCreated::ID:
    case td_api::messageGiveawayCompleted::ID:
    case td_api::messageGiftedStars::ID:
    case td_api::messageGiftedGrams::ID:
    case td_api::messageGiveawayPrizeStars::ID:
    case td_api::messageTonWalletTransfer::ID:
    case td_api::messageTonConnectRequest::ID:
    case td_api::messageGift::ID:
    case td_api::messageUpgradedGift::ID:
    case td_api::messageRefundedUpgradedGift::ID:
    case td_api::messageUpgradedGiftPurchaseOffer::ID:
    case td_api::messageUpgradedGiftPurchaseOfferRejected::ID:
    case td_api::messageUpgradedGiftOnChainPurchaseOffer::ID:
    case td_api::messagePaidMessagesRefunded::ID:
    case td_api::messagePaidMessagePriceChanged::ID:
    case td_api::messageDirectMessagePriceChanged::ID:
    case td_api::messageChecklistTasksDone::ID:
    case td_api::messageChecklistTasksAdded::ID:
    case td_api::messageSuggestedPostApprovalFailed::ID:
    case td_api::messageSuggestedPostApproved::ID:
    case td_api::messageSuggestedPostDeclined::ID:
    case td_api::messageSuggestedPostPaid::ID:
    case td_api::messageSuggestedPostRefunded::ID:
    case td_api::messageContactRegistered::ID:
    case td_api::messageUsersShared::ID:
    case td_api::messageChatShared::ID:
    case td_api::messageBotWriteAccessAllowed::ID:
    case td_api::messageWebAppDataSent::ID:
    case td_api::messagePassportDataSent::ID:
    case td_api::messageProximityAlertTriggered::ID:
    case td_api::messageUnpaidMessagesAllowedByLink::ID:
    case td_api::messageBotInvite::ID:
      return ExportContentKind::Service;
    default:
      return ExportContentKind::Unsupported;
  }
}

int64 get_export_dialog_bare_id(DialogId dialog_id) {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      return dialog_id.get_user_id().get();
    case DialogType::Chat:
      return dialog_id.get_chat_id().get();
    case DialogType::Channel:
      return dialog_id.get_channel_id().get();
    default:
      return 0;
  }
}

DialogId get_export_message_sender_dialog_id(const td_api::MessageSender *sender) {
  if (sender == nullptr) {
    return DialogId();
  }
  switch (sender->get_id()) {
    case td_api::messageSenderUser::ID:
      return DialogId(UserId(static_cast<const td_api::messageSenderUser *>(sender)->user_id_));
    case td_api::messageSenderChat::ID:
      return DialogId(static_cast<const td_api::messageSenderChat *>(sender)->chat_id_);
    default:
      UNREACHABLE();
      return DialogId();
  }
}

int32 get_export_server_message_id(int64 message_id) {
  MessageId result(message_id);
  return result.is_server() ? result.get_server_message_id().get() : 0;
}

void get_export_giveaway_prize(const td_api::GiveawayPrize *prize, int32 &month_count, int64 &star_count) {
  month_count = 0;
  star_count = 0;
  if (prize == nullptr) {
    return;
  }
  switch (prize->get_id()) {
    case td_api::giveawayPrizePremium::ID:
      month_count = static_cast<const td_api::giveawayPrizePremium *>(prize)->month_count_;
      break;
    case td_api::giveawayPrizeStars::ID:
      star_count = static_cast<const td_api::giveawayPrizeStars *>(prize)->star_count_;
      break;
    default:
      UNREACHABLE();
  }
}

vector<ExportTextPart> get_export_text_parts(Slice text,
                                             const vector<td_api::object_ptr<td_api::textEntity>> *entities) {
  vector<ExportTextPart> result;
  Slice rest = text;
  int32 position = 0;
  if (entities != nullptr) {
    for (const auto &entity : *entities) {
      if (entity == nullptr || entity->type_ == nullptr || entity->offset_ < position || entity->length_ <= 0) {
        continue;
      }
      // the entities are found by TDLib in the text of the message and aren't a part of the message
      if (entity->type_->get_id() == td_api::textEntityTypeMediaTimestamp::ID) {
        continue;
      }
      auto plain_text = utf8_utf16_substr(rest, 0, static_cast<size_t>(entity->offset_ - position));
      if (!plain_text.empty()) {
        result.push_back({plain_text, nullptr});
      }
      rest.remove_prefix(plain_text.size());
      auto entity_text = utf8_utf16_substr(rest, 0, static_cast<size_t>(entity->length_));
      rest.remove_prefix(entity_text.size());
      result.push_back({entity_text, entity->type_.get()});
      position = entity->offset_ + entity->length_;
    }
  }
  if (!rest.empty()) {
    result.push_back({rest, nullptr});
  }
  return result;
}

string get_export_custom_emoji_path(const ExportCustomEmojiFiles *custom_emoji_files, int64 custom_emoji_id) {
  if (custom_emoji_files == nullptr) {
    return to_string(custom_emoji_id);
  }
  auto it = custom_emoji_files->find(CustomEmojiId(custom_emoji_id));
  if (it == custom_emoji_files->end()) {
    return "(unavailable)";
  }
  switch (it->second.skip_reason) {
    case ExportFile::SkipReason::None:
      return it->second.relative_path;
    case ExportFile::SkipReason::Unavailable:
      return "(unavailable)";
    case ExportFile::SkipReason::FileType:
    case ExportFile::SkipReason::FileSize:
      return string();
    default:
      UNREACHABLE();
      return string();
  }
}

struct ExportCurrencyRule {
  const char *international = "";
  char thousands = ',';
  char decimal = '.';
  bool left = true;
  bool space = false;
  int32 exponent = 2;
  bool strip_dot_zero = false;
};

struct ExportCurrencyRuleEntry {
  const char *currency;
  ExportCurrencyRule rule;
};

static ExportCurrencyRule get_currency_rule(Slice currency) {
  static const ExportCurrencyRuleEntry rules[] = {
      {"AED", {"", ',', '.', true, true}},
      {"AFN", {}},
      {"ALL", {"", '.', ',', false}},
      {"AMD", {"", ',', '.', false, true}},
      {"ARS", {"", '.', ',', true, true}},
      {"AUD", {"AU$"}},
      {"AZN", {"", ' ', ',', false, true}},
      {"BAM", {"", '.', ',', false, true}},
      {"BDT", {"", ',', '.', true, true}},
      {"BGN", {"", ' ', ',', false, true}},
      {"BHD", {"", ',', '.', true, true, 3}},
      {"BND", {"", '.', ','}},
      {"BOB", {"", '.', ',', true, true}},
      {"BRL", {"R$", '.', ',', true, true}},
      {"BYN", {"", ' ', ',', false, true}},
      {"CAD", {"CA$"}},
      {"CHF", {"", '\'', '.', false, true}},
      {"CLP", {"", '.', ',', true, true, 0}},
      {"CNY", {"\x43\x4E\xC2\xA5"}},
      {"COP", {"", '.', ',', true, true}},
      {"CRC", {"", '.', ','}},
      {"CZK", {"", ' ', ',', false, true}},
      {"DKK", {"", '\0', ',', false, true}},
      {"DOP", {}},
      {"DZD", {"", ',', '.', true, true}},
      {"EGP", {"", ',', '.', true, true}},
      {"ETB", {}},
      {"EUR", {"\xE2\x82\xAC", ' ', ',', false, true}},
      {"GBP", {"\xC2\xA3"}},
      {"GEL", {"", ' ', ',', false, true}},
      {"GHS", {}},
      {"GTQ", {}},
      {"HKD", {"HK$"}},
      {"HNL", {"", ',', '.', true, true}},
      {"HRK", {"", '.', ',', false, true}},
      {"HUF", {"", ' ', ',', false, true}},
      {"IDR", {"", '.', ','}},
      {"ILS", {"\xE2\x82\xAA", ',', '.', true, true}},
      {"INR", {"\xE2\x82\xB9"}},
      {"IQD", {"", ',', '.', true, true, 3}},
      {"IRR", {"", ',', '/', false, true}},
      {"ISK", {"", '.', ',', false, true, 0}},
      {"JMD", {}},
      {"JOD", {"", ',', '.', true, false, 3}},
      {"JPY", {"\xC2\xA5", ',', '.', true, false, 0}},
      {"KES", {}},
      {"KGS", {"", ' ', '-', false, true}},
      {"KRW", {"\xE2\x82\xA9", ',', '.', true, false, 0}},
      {"KZT", {"", ' ', '-'}},
      {"LBP", {"", ',', '.', true, true}},
      {"LKR", {"", ',', '.', true, true}},
      {"MAD", {"", ',', '.', true, true}},
      {"MDL", {"", ',', '.', false, true}},
      {"MMK", {}},
      {"MNT", {"", ' ', ','}},
      {"MOP", {}},
      {"MUR", {}},
      {"MVR", {"", ',', '.', false, true}},
      {"MXN", {"MX$"}},
      {"MYR", {}},
      {"MZN", {}},
      {"NGN", {}},
      {"NIO", {"", ',', '.', true, true}},
      {"NOK", {"", ' ', ',', true, true}},
      {"NPR", {}},
      {"NZD", {"NZ$"}},
      {"PAB", {"", ',', '.', true, true}},
      {"PEN", {"", ',', '.', true, true}},
      {"PHP", {}},
      {"PKR", {}},
      {"PLN", {"", ' ', ',', false, true}},
      {"PYG", {"", '.', ',', true, true, 0}},
      {"QAR", {"", ',', '.', true, true}},
      {"RON", {"", '.', ',', false, true}},
      {"RSD", {"", '.', ',', false, true}},
      {"RUB", {"", ' ', ',', false, true}},
      {"SAR", {"", ',', '.', true, true}},
      {"SEK", {"", '.', ',', false, true}},
      {"SGD", {}},
      {"SYP", {"", ',', '.', true, true}},
      {"THB", {"\xE0\xB8\xBF"}},
      {"TJS", {"", ' ', ';', false, true}},
      {"TRY", {"", '.', ',', false, true}},
      {"TTD", {}},
      {"TWD", {"NT$"}},
      {"TZS", {}},
      {"UAH", {"", ' ', ',', false}},
      {"UGX", {"", ',', '.', true, false, 0}},
      {"USD", {"$"}},
      {"UYU", {"", '.', ',', true, true}},
      {"UZS", {"", ' ', ',', false, true}},
      {"VEF", {"", '.', ',', true, true}},
      {"VND", {"\xE2\x82\xAB", '.', ',', false, true, 0}},
      {"YER", {"", ',', '.', true, true}},
      {"ZAR", {"", ',', '.', true, true}},
  };
  for (const auto &entry : rules) {
    if (currency == Slice(entry.currency)) {
      return entry.rule;
    }
  }
  return ExportCurrencyRule();
}

static string group_thousands(const string &digits, char thousands) {
  if (thousands == '\0') {
    return digits;
  }
  string result;
  result.reserve(digits.size() + digits.size() / 3);
  for (size_t i = 0; i < digits.size(); i++) {
    if (i != 0 && (digits.size() - i) % 3 == 0) {
      result += thousands;
    }
    result += digits[i];
  }
  return result;
}

string format_export_money_amount(int64 amount, Slice currency) {
  const auto absolute = amount < 0 ? ~static_cast<uint64>(amount) + 1 : static_cast<uint64>(amount);
  if (currency == "XTR") {
    return "\xE2\xAD\x90" + group_thousands(to_string(absolute), ',');
  }

  const auto rule = get_currency_rule(currency);
  uint64 divisor = 1;
  for (int32 i = 0; i < rule.exponent; i++) {
    divisor *= 10;
  }
  const auto integer_part = absolute / divisor;
  const auto fractional_part = absolute % divisor;
  const string name = rule.international[0] != '\0' ? string(rule.international) : currency.str();
  string result = amount < 0 ? "\xE2\x88\x92" : "";
  if (rule.left) {
    result += name;
    if (rule.space) {
      result += ' ';
    }
  }
  result += group_thousands(to_string(integer_part), rule.thousands);
  if (rule.exponent > 0 && (!rule.strip_dot_zero || fractional_part != 0)) {
    auto fraction = to_string(fractional_part);
    result += rule.decimal;
    result.append(static_cast<size_t>(rule.exponent) - fraction.size(), '0');
    result += fraction;
  }
  if (!rule.left) {
    if (rule.space) {
      result += ' ';
    }
    result += name;
  }
  return result;
}

string format_export_phone_number(const string &phone_number) {
  if (phone_number.empty() || phone_number[0] == '0') {
    return phone_number;
  }
  auto info = CountryInfoManager::get_phone_number_info_sync("en", phone_number);
  if (info == nullptr) {
    return phone_number;
  }
  if (info->country_ == nullptr) {
    return info->country_calling_code_ + info->formatted_phone_number_;
  }
  // the unfilled positions of the pattern are padded with '-'
  auto formatted = std::move(info->formatted_phone_number_);
  while (!formatted.empty() && (formatted.back() == '-' || formatted.back() == ' ')) {
    formatted.pop_back();
  }
  return PSTRING() << '+' << info->country_calling_code_ << ' ' << formatted;
}

ExportOutputFile::ExportOutputFile(string path, ExportStats *stats) : path_(std::move(path)), stats_(stats) {
}

Status ExportOutputFile::write(Slice data) {
  if (fd_.empty()) {
    TRY_STATUS(mkpath(path_));
    auto r_fd = FileFd::open(path_, FileFd::Write | FileFd::Create | FileFd::Truncate);
    if (r_fd.is_error()) {
      return Status::Error(PSLICE() << "Failed to create file \"" << path_ << "\": " << r_fd.error().message());
    }
    fd_ = r_fd.move_as_ok();
    stats_->file_count++;
  }
  auto r_size = fd_.write(data);
  if (r_size.is_error() || r_size.ok() != data.size()) {
    fd_.close();
    return Status::Error(PSLICE() << "Failed to write to file \"" << path_ << '"');
  }
  stats_->total_size += static_cast<int64>(data.size());
  return Status::OK();
}

namespace {

class ExportCombinedWriter final : public ExportWriter {
 public:
  ExportCombinedWriter(unique_ptr<ExportWriter> first, unique_ptr<ExportWriter> second)
      : first_(std::move(first)), second_(std::move(second)) {
  }

  Status start(const ExportSettings &settings, ExportStats *stats) final {
    TRY_STATUS(first_->start(settings, stats));
    return second_->start(settings, stats);
  }

  Status write_dialogs_start(int32 dialog_count) final {
    TRY_STATUS(first_->write_dialogs_start(dialog_count));
    return second_->write_dialogs_start(dialog_count);
  }

  Status write_dialog_start(const ExportDialogInfo &dialog) final {
    TRY_STATUS(first_->write_dialog_start(dialog));
    return second_->write_dialog_start(dialog);
  }

  Status write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated,
                               const ExportCustomEmojiFiles &custom_emoji_files) final {
    TRY_STATUS(first_->write_dialog_messages(messages, is_migrated, custom_emoji_files));
    return second_->write_dialog_messages(messages, is_migrated, custom_emoji_files);
  }

  Status write_dialog_end(bool only_my_messages) final {
    TRY_STATUS(first_->write_dialog_end(only_my_messages));
    return second_->write_dialog_end(only_my_messages);
  }

  Status write_dialogs_end() final {
    TRY_STATUS(first_->write_dialogs_end());
    return second_->write_dialogs_end();
  }

  Status finish() final {
    TRY_STATUS(first_->finish());
    return second_->finish();
  }

 private:
  unique_ptr<ExportWriter> first_;
  unique_ptr<ExportWriter> second_;
};

}  // namespace

unique_ptr<ExportWriter> create_export_writer(Td *td, ExportFormat format) {
  switch (format) {
    case ExportFormat::Json:
      return make_unique<ExportJsonWriter>(td);
    case ExportFormat::Html:
      return make_unique<ExportHtmlWriter>(td);
    case ExportFormat::HtmlAndJson:
      return make_unique<ExportCombinedWriter>(make_unique<ExportHtmlWriter>(td), make_unique<ExportJsonWriter>(td));
    default:
      UNREACHABLE();
      return nullptr;
  }
}

}  // namespace td
