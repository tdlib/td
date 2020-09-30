//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageEntity.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/misc.h"
#include "td/telegram/SecretChatActor.h"

#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/unicode.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace td {

int MessageEntity::get_type_priority(Type type) {
  static const int types[] = {50, 50, 50, 50, 50, 90, 91, 20, 11, 10, 49, 49, 50, 50, 92, 93, 0, 50};
  static_assert(sizeof(types) / sizeof(types[0]) == static_cast<size_t>(MessageEntity::Type::Size), "");
  return types[static_cast<int32>(type)];
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageEntity::Type &message_entity_type) {
  switch (message_entity_type) {
    case MessageEntity::Type::Mention:
      return string_builder << "Mention";
    case MessageEntity::Type::Hashtag:
      return string_builder << "Hashtag";
    case MessageEntity::Type::BotCommand:
      return string_builder << "BotCommand";
    case MessageEntity::Type::Url:
      return string_builder << "Url";
    case MessageEntity::Type::EmailAddress:
      return string_builder << "EmailAddress";
    case MessageEntity::Type::Bold:
      return string_builder << "Bold";
    case MessageEntity::Type::Italic:
      return string_builder << "Italic";
    case MessageEntity::Type::Underline:
      return string_builder << "Underline";
    case MessageEntity::Type::Strikethrough:
      return string_builder << "Strikethrough";
    case MessageEntity::Type::BlockQuote:
      return string_builder << "BlockQuote";
    case MessageEntity::Type::Code:
      return string_builder << "Code";
    case MessageEntity::Type::Pre:
      return string_builder << "Pre";
    case MessageEntity::Type::PreCode:
      return string_builder << "PreCode";
    case MessageEntity::Type::TextUrl:
      return string_builder << "TextUrl";
    case MessageEntity::Type::MentionName:
      return string_builder << "MentionName";
    case MessageEntity::Type::Cashtag:
      return string_builder << "Cashtag";
    case MessageEntity::Type::PhoneNumber:
      return string_builder << "PhoneNumber";
    case MessageEntity::Type::BankCardNumber:
      return string_builder << "BankCardNumber";
    default:
      UNREACHABLE();
      return string_builder << "Impossible";
  }
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageEntity &message_entity) {
  string_builder << '[' << message_entity.type << ", offset = " << message_entity.offset
                 << ", length = " << message_entity.length;
  if (!message_entity.argument.empty()) {
    string_builder << ", argument = \"" << message_entity.argument << "\"";
  }
  if (message_entity.user_id.is_valid()) {
    string_builder << ", " << message_entity.user_id;
  }
  string_builder << ']';
  return string_builder;
}

tl_object_ptr<td_api::TextEntityType> MessageEntity::get_text_entity_type_object() const {
  switch (type) {
    case MessageEntity::Type::Mention:
      return make_tl_object<td_api::textEntityTypeMention>();
    case MessageEntity::Type::Hashtag:
      return make_tl_object<td_api::textEntityTypeHashtag>();
    case MessageEntity::Type::BotCommand:
      return make_tl_object<td_api::textEntityTypeBotCommand>();
    case MessageEntity::Type::Url:
      return make_tl_object<td_api::textEntityTypeUrl>();
    case MessageEntity::Type::EmailAddress:
      return make_tl_object<td_api::textEntityTypeEmailAddress>();
    case MessageEntity::Type::Bold:
      return make_tl_object<td_api::textEntityTypeBold>();
    case MessageEntity::Type::Italic:
      return make_tl_object<td_api::textEntityTypeItalic>();
    case MessageEntity::Type::Underline:
      return make_tl_object<td_api::textEntityTypeUnderline>();
    case MessageEntity::Type::Strikethrough:
      return make_tl_object<td_api::textEntityTypeStrikethrough>();
    case MessageEntity::Type::BlockQuote:
      return nullptr;
    case MessageEntity::Type::Code:
      return make_tl_object<td_api::textEntityTypeCode>();
    case MessageEntity::Type::Pre:
      return make_tl_object<td_api::textEntityTypePre>();
    case MessageEntity::Type::PreCode:
      return make_tl_object<td_api::textEntityTypePreCode>(argument);
    case MessageEntity::Type::TextUrl:
      return make_tl_object<td_api::textEntityTypeTextUrl>(argument);
    case MessageEntity::Type::MentionName:
      // can't use contacts_manager, because can be called from a static request
      return make_tl_object<td_api::textEntityTypeMentionName>(user_id.get());
    case MessageEntity::Type::Cashtag:
      return make_tl_object<td_api::textEntityTypeCashtag>();
    case MessageEntity::Type::PhoneNumber:
      return make_tl_object<td_api::textEntityTypePhoneNumber>();
    case MessageEntity::Type::BankCardNumber:
      return make_tl_object<td_api::textEntityTypeBankCardNumber>();
    default:
      UNREACHABLE();
      return nullptr;
  }
}

tl_object_ptr<td_api::textEntity> MessageEntity::get_text_entity_object() const {
  return make_tl_object<td_api::textEntity>(offset, length, get_text_entity_type_object());
}

vector<tl_object_ptr<td_api::textEntity>> get_text_entities_object(const vector<MessageEntity> &entities) {
  vector<tl_object_ptr<td_api::textEntity>> result;
  result.reserve(entities.size());

  for (auto &entity : entities) {
    auto entity_object = entity.get_text_entity_object();
    if (entity_object->type_ != nullptr) {
      result.push_back(std::move(entity_object));
    }
  }

  return result;
}

StringBuilder &operator<<(StringBuilder &string_builder, const FormattedText &text) {
  return string_builder << '"' << text.text << "\" with entities " << text.entities;
}

td_api::object_ptr<td_api::formattedText> get_formatted_text_object(const FormattedText &text) {
  return td_api::make_object<td_api::formattedText>(text.text, get_text_entities_object(text.entities));
}

static bool is_word_character(uint32 code) {
  switch (get_unicode_simple_category(code)) {
    case UnicodeSimpleCategory::Letter:
    case UnicodeSimpleCategory::DecimalNumber:
    case UnicodeSimpleCategory::Number:
      return true;
    default:
      return code == '_';
  }
}

/*
static bool is_word_boundary(uint32 a, uint32 b) {
  return is_word_character(a) ^ is_word_character(b);
}
*/

static bool is_alpha_digit(uint32 code) {
  return ('0' <= code && code <= '9') || ('a' <= code && code <= 'z') || ('A' <= code && code <= 'Z');
}

static bool is_alpha_digit_or_underscore(uint32 code) {
  return is_alpha_digit(code) || code == '_';
}

static bool is_alpha_digit_or_underscore_or_minus(uint32 code) {
  return is_alpha_digit_or_underscore(code) || code == '-';
}

// This functions just implements corresponding regexps
// All other fixes will be in other functions
static vector<Slice> match_mentions(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();
  const unsigned char *ptr = begin;

  // '/(?<=\B)@([a-zA-Z0-9_]{2,32})(?=\b)/u'

  while (true) {
    ptr = static_cast<const unsigned char *>(std::memchr(ptr, '@', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev, "match_mentions");

      if (is_word_character(prev)) {
        ptr++;
        continue;
      }
    }
    auto mention_begin = ++ptr;
    while (ptr != end && is_alpha_digit_or_underscore(*ptr)) {
      ptr++;
    }
    auto mention_end = ptr;
    auto mention_size = mention_end - mention_begin;
    if (mention_size < 2 || mention_size > 32) {
      continue;
    }
    uint32 next = 0;
    if (ptr != end) {
      next_utf8_unsafe(ptr, &next, "match_mentions 2");
    }
    if (is_word_character(next)) {
      continue;
    }
    result.emplace_back(mention_begin - 1, mention_end);
  }
  return result;
}

static vector<Slice> match_bot_commands(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();
  const unsigned char *ptr = begin;

  // '/(?<!\b|[\/<>])\/([a-zA-Z0-9_]{1,64})(?:@([a-zA-Z0-9_]{3,32}))?(?!\B|[\/<>])/u'

  while (true) {
    ptr = static_cast<const unsigned char *>(std::memchr(ptr, '/', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev, "match_bot_commands");

      if (is_word_character(prev) || prev == '/' || prev == '<' || prev == '>') {
        ptr++;
        continue;
      }
    }

    auto command_begin = ++ptr;
    while (ptr != end && is_alpha_digit_or_underscore(*ptr)) {
      ptr++;
    }
    auto command_end = ptr;
    auto command_size = command_end - command_begin;
    if (command_size < 1 || command_size > 64) {
      continue;
    }

    if (ptr != end && *ptr == '@') {
      auto mention_begin = ++ptr;
      while (ptr != end && is_alpha_digit_or_underscore(*ptr)) {
        ptr++;
      }
      auto mention_end = ptr;
      auto mention_size = mention_end - mention_begin;
      if (mention_size < 3 || mention_size > 32) {
        continue;
      }
      command_end = ptr;
    }

    uint32 next = 0;
    if (ptr != end) {
      next_utf8_unsafe(ptr, &next, "match_bot_commands 2");
    }
    if (is_word_character(next) || next == '/' || next == '<' || next == '>') {
      continue;
    }
    result.emplace_back(command_begin - 1, command_end);
  }
  return result;
}

static bool is_hashtag_letter(uint32 c, UnicodeSimpleCategory &category) {
  category = get_unicode_simple_category(c);
  if (c == '_' || c == 0x200c) {
    return true;
  }
  switch (category) {
    case UnicodeSimpleCategory::DecimalNumber:
    case UnicodeSimpleCategory::Letter:
      return true;
    default:
      return false;
  }
}

static vector<Slice> match_hashtags(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();
  const unsigned char *ptr = begin;

  // '/(?<=^|[^\d_\pL\x{200c}])#([\d_\pL\x{200c}]{1,256})(?![\d_\pL\x{200c}]*#)/u'
  // and at least one letter

  UnicodeSimpleCategory category;

  while (true) {
    ptr = static_cast<const unsigned char *>(std::memchr(ptr, '#', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev, "match_hashtags");

      if (is_hashtag_letter(prev, category)) {
        ptr++;
        continue;
      }
    }
    auto hashtag_begin = ++ptr;
    size_t hashtag_size = 0;
    const unsigned char *hashtag_end = nullptr;
    bool was_letter = false;
    while (ptr != end) {
      uint32 code;
      auto next_ptr = next_utf8_unsafe(ptr, &code, "match_hashtags 2");
      if (!is_hashtag_letter(code, category)) {
        break;
      }
      ptr = next_ptr;

      if (hashtag_size == 255) {
        hashtag_end = ptr;
      }
      if (hashtag_size != 256) {
        was_letter |= category == UnicodeSimpleCategory::Letter;
        hashtag_size++;
      }
    }
    if (!hashtag_end) {
      hashtag_end = ptr;
    }
    if (hashtag_size < 1) {
      continue;
    }
    if (ptr != end && ptr[0] == '#') {
      continue;
    }
    if (!was_letter) {
      continue;
    }
    result.emplace_back(hashtag_begin - 1, hashtag_end);
  }
  return result;
}

static vector<Slice> match_cashtags(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();
  const unsigned char *ptr = begin;

  // '/(?<=^|[^$\d_\pL\x{200c}])\$([A-Z]{3,8})(?![$\d_\pL\x{200c}])/u'

  UnicodeSimpleCategory category;
  while (true) {
    ptr = static_cast<const unsigned char *>(std::memchr(ptr, '$', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev, "match_cashtags");

      if (is_hashtag_letter(prev, category) || prev == '$') {
        ptr++;
        continue;
      }
    }

    auto cashtag_begin = ++ptr;
    while (ptr != end && 'Z' >= *ptr && *ptr >= 'A') {
      ptr++;
    }
    auto cashtag_end = ptr;
    auto cashtag_size = cashtag_end - cashtag_begin;
    if (cashtag_size < 3 || cashtag_size > 8) {
      continue;
    }

    if (cashtag_end != end) {
      uint32 code;
      next_utf8_unsafe(ptr, &code, "match_cashtags 2");
      if (is_hashtag_letter(code, category) || code == '$') {
        continue;
      }
    }

    result.emplace_back(cashtag_begin - 1, cashtag_end);
  }
  return result;
}

static vector<Slice> match_bank_card_numbers(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();
  const unsigned char *ptr = begin;

  // '/(?<=^|[^+_\pL\d-.,])[\d -]{13,}([^_\pL\d-]|$)/'

  while (true) {
    while (ptr != end && !is_digit(*ptr)) {
      ptr++;
    }
    if (ptr == end) {
      break;
    }
    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev, "match_bank_card_numbers");

      if (prev == '.' || prev == ',' || prev == '+' || prev == '-' || prev == '_' ||
          get_unicode_simple_category(prev) == UnicodeSimpleCategory::Letter) {
        while (ptr != end && (is_digit(*ptr) || *ptr == ' ' || *ptr == '-')) {
          ptr++;
        }
        continue;
      }
    }

    auto card_number_begin = ptr;
    size_t digit_count = 0;
    while (ptr != end && (is_digit(*ptr) || *ptr == ' ' || *ptr == '-')) {
      if (*ptr == ' ' && digit_count >= 16 && digit_count <= 19 &&
          digit_count == static_cast<size_t>(ptr - card_number_begin)) {
        // continuous card number
        break;
      }
      digit_count += static_cast<size_t>(is_digit(*ptr));
      ptr++;
    }
    if (digit_count < 13 || digit_count > 19) {
      continue;
    }

    auto card_number_end = ptr;
    while (!is_digit(card_number_end[-1])) {
      card_number_end--;
    }
    auto card_number_size = static_cast<size_t>(card_number_end - card_number_begin);
    if (card_number_size > 2 * digit_count - 1) {
      continue;
    }
    if (card_number_end != end) {
      uint32 next;
      next_utf8_unsafe(card_number_end, &next, "match_bank_card_numbers 2");
      if (next == '-' || next == '_' || get_unicode_simple_category(next) == UnicodeSimpleCategory::Letter) {
        continue;
      }
    }

    result.emplace_back(card_number_begin, card_number_end);
  }
  return result;
}

static vector<Slice> match_urls(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();

  const auto &is_protocol_symbol = [](uint32 c) {
    if (c < 0x80) {
      // do not allow dots in the protocol
      return is_alpha_digit(c) || c == '+' || c == '-';
    }
    // add unicode letters and digits to later discard protocol as invalid
    return get_unicode_simple_category(c) != UnicodeSimpleCategory::Separator;
  };

  const auto &is_user_data_symbol = [](uint32 c) {
    switch (c) {
      case '\n':
      case '/':
      case '[':
      case ']':
      case '{':
      case '}':
      case '(':
      case ')':
      case '\'':
      case '`':
      case '<':
      case '>':
      case '"':
      case 0xab:  // «
      case 0xbb:  // »
        return false;
      default:
        if (0x2000 <= c && c <= 0x206f) {     // General Punctuation
          return c == 0x200c || c == 0x200d;  // Zero Width Non-Joiner/Joiner
        }
        return get_unicode_simple_category(c) != UnicodeSimpleCategory::Separator;
    }
  };

  const auto &is_domain_symbol = [](uint32 c) {
    if (c < 0xc0) {
      return c == '.' || is_alpha_digit_or_underscore_or_minus(c) || c == '~';
    }
    if (0x2000 <= c && c <= 0x206f) {     // General Punctuation
      return c == 0x200c || c == 0x200d;  // Zero Width Non-Joiner/Joiner
    }
    return get_unicode_simple_category(c) != UnicodeSimpleCategory::Separator;
  };

  const auto &is_path_symbol = [](uint32 c) {
    switch (c) {
      case '\n':
      case '<':
      case '>':
      case '"':
      case 0xab:  // «
      case 0xbb:  // »
        return false;
      default:
        if (0x2000 <= c && c <= 0x206f) {     // General Punctuation
          return c == 0x200c || c == 0x200d;  // Zero Width Non-Joiner/Joiner
        }
        return get_unicode_simple_category(c) != UnicodeSimpleCategory::Separator;
    }
  };

  Slice bad_path_end_chars(".:;,('?!`");

  while (true) {
    auto dot_pos = str.find('.');
    if (dot_pos > str.size() || dot_pos + 1 == str.size()) {
      break;
    }
    if (str[dot_pos + 1] == ' ') {
      // fast path
      str = str.substr(dot_pos + 2);
      begin = str.ubegin();
      continue;
    }

    const unsigned char *last_at_ptr = nullptr;
    const unsigned char *domain_end_ptr = begin + dot_pos;
    while (domain_end_ptr != end) {
      uint32 code = 0;
      auto next_ptr = next_utf8_unsafe(domain_end_ptr, &code, "match_urls");
      if (code == '@') {
        last_at_ptr = domain_end_ptr;
      }
      if (!is_user_data_symbol(code)) {
        break;
      }
      domain_end_ptr = next_ptr;
    }
    domain_end_ptr = last_at_ptr == nullptr ? begin + dot_pos : last_at_ptr + 1;
    while (domain_end_ptr != end) {
      uint32 code = 0;
      auto next_ptr = next_utf8_unsafe(domain_end_ptr, &code, "match_urls 2");
      if (!is_domain_symbol(code)) {
        break;
      }
      domain_end_ptr = next_ptr;
    }

    const unsigned char *domain_begin_ptr = begin + dot_pos;
    while (domain_begin_ptr != begin) {
      domain_begin_ptr = prev_utf8_unsafe(domain_begin_ptr);
      uint32 code = 0;
      auto next_ptr = next_utf8_unsafe(domain_begin_ptr, &code, "match_urls 3");
      if (last_at_ptr == nullptr ? !is_domain_symbol(code) : !is_user_data_symbol(code)) {
        domain_begin_ptr = next_ptr;
        break;
      }
    }
    // LOG(ERROR) << "Domain: " << Slice(domain_begin_ptr, domain_end_ptr);

    const unsigned char *url_end_ptr = domain_end_ptr;
    if (url_end_ptr != end && url_end_ptr[0] == ':') {
      auto port_end_ptr = url_end_ptr + 1;

      while (port_end_ptr != end && is_digit(port_end_ptr[0])) {
        port_end_ptr++;
      }

      auto port_begin_ptr = url_end_ptr + 1;
      while (port_begin_ptr != port_end_ptr && *port_begin_ptr == '0') {
        port_begin_ptr++;
      }
      if (port_begin_ptr != port_end_ptr && narrow_cast<int>(port_end_ptr - port_begin_ptr) <= 5 &&
          to_integer<uint32>(Slice(port_begin_ptr, port_end_ptr)) <= 65535) {
        url_end_ptr = port_end_ptr;
      }
    }
    // LOG(ERROR) << "Domain_port: " << Slice(domain_begin_ptr, url_end_ptr);

    if (url_end_ptr != end && (url_end_ptr[0] == '/' || url_end_ptr[0] == '?' || url_end_ptr[0] == '#')) {
      auto path_end_ptr = url_end_ptr + 1;
      while (path_end_ptr != end) {
        uint32 code = 0;
        auto next_ptr = next_utf8_unsafe(path_end_ptr, &code, "match_urls 4");
        if (!is_path_symbol(code)) {
          break;
        }
        path_end_ptr = next_ptr;
      }
      while (bad_path_end_chars.find(path_end_ptr[-1]) < bad_path_end_chars.size()) {
        path_end_ptr--;
      }
      if (url_end_ptr[0] == '/' || path_end_ptr > url_end_ptr + 1) {
        url_end_ptr = path_end_ptr;
      }
    }
    while (url_end_ptr > begin + dot_pos + 1 && url_end_ptr[-1] == '.') {
      url_end_ptr--;
    }
    // LOG(ERROR) << "Domain_port_path: " << Slice(domain_begin_ptr, url_end_ptr);

    bool is_bad = false;
    const unsigned char *url_begin_ptr = domain_begin_ptr;
    if (url_begin_ptr != begin && url_begin_ptr[-1] == '@') {
      auto user_data_begin_ptr = url_begin_ptr - 1;
      while (user_data_begin_ptr != begin) {
        user_data_begin_ptr = prev_utf8_unsafe(user_data_begin_ptr);
        uint32 code = 0;
        auto next_ptr = next_utf8_unsafe(user_data_begin_ptr, &code, "match_urls 5");
        if (!is_user_data_symbol(code)) {
          user_data_begin_ptr = next_ptr;
          break;
        }
      }
      if (user_data_begin_ptr == url_begin_ptr - 1) {
        is_bad = true;
      }
      url_begin_ptr = user_data_begin_ptr;
    }
    // LOG(ERROR) << "User_data_port_path: " << Slice(url_begin_ptr, url_end_ptr);

    if (url_begin_ptr != begin) {
      Slice prefix(begin, url_begin_ptr);
      if (prefix.size() >= 6 && ends_with(prefix, "://")) {
        auto protocol_begin_ptr = url_begin_ptr - 3;
        while (protocol_begin_ptr != begin) {
          protocol_begin_ptr = prev_utf8_unsafe(protocol_begin_ptr);
          uint32 code = 0;
          auto next_ptr = next_utf8_unsafe(protocol_begin_ptr, &code, "match_urls 6");
          if (!is_protocol_symbol(code)) {
            protocol_begin_ptr = next_ptr;
            break;
          }
        }
        auto protocol = to_lower(Slice(protocol_begin_ptr, url_begin_ptr - 3));
        if (ends_with(protocol, "http") && protocol != "shttp") {
          url_begin_ptr = url_begin_ptr - 7;
        } else if (ends_with(protocol, "https")) {
          url_begin_ptr = url_begin_ptr - 8;
        } else if (ends_with(protocol, "sftp")) {
          url_begin_ptr = url_begin_ptr - 7;
        } else if (ends_with(protocol, "ftp") && protocol != "tftp") {
          url_begin_ptr = url_begin_ptr - 6;
        } else {
          is_bad = true;
        }
      } else {
        auto prefix_end = prefix.uend();
        auto prefix_back = prev_utf8_unsafe(prefix_end);
        uint32 code = 0;
        next_utf8_unsafe(prefix_back, &code, "match_urls 7");
        if (is_word_character(code) || code == '/' || code == '#' || code == '@') {
          is_bad = true;
        }
      }
    }
    // LOG(ERROR) << "Full: " << Slice(url_begin_ptr, url_end_ptr) << " " << is_bad;

    if (!is_bad) {
      if (url_end_ptr > begin + dot_pos + 1) {
        result.emplace_back(url_begin_ptr, url_end_ptr);
      }
      while (url_end_ptr != end && url_end_ptr[0] == '.') {
        url_end_ptr++;
      }
    } else {
      while (url_end_ptr[-1] != '.') {
        url_end_ptr--;
      }
    }

    if (url_end_ptr <= begin + dot_pos) {
      url_end_ptr = begin + dot_pos + 1;
    }
    str = str.substr(url_end_ptr - begin);
    begin = url_end_ptr;
  }

  return result;
}

static bool is_valid_bank_card(Slice str) {
  const size_t MIN_CARD_LENGTH = 13;
  const size_t MAX_CARD_LENGTH = 19;
  char digits[MAX_CARD_LENGTH];
  size_t digit_count = 0;
  for (auto c : str) {
    if (is_digit(c)) {
      CHECK(digit_count < MAX_CARD_LENGTH);
      digits[digit_count++] = c;
    }
  }
  CHECK(digit_count >= MIN_CARD_LENGTH);

  // Luhn algorithm
  int32 sum = 0;
  for (size_t i = digit_count; i > 0; i--) {
    int32 digit = digits[i - 1] - '0';
    if ((digit_count - i) % 2 == 0) {
      sum += digit;
    } else {
      sum += (digit < 5 ? 2 * digit : 2 * digit - 9);
    }
  }
  if (sum % 10 != 0) {
    return false;
  }

  int32 prefix1 = (digits[0] - '0');
  int32 prefix2 = prefix1 * 10 + (digits[1] - '0');
  int32 prefix3 = prefix2 * 10 + (digits[2] - '0');
  int32 prefix4 = prefix3 * 10 + (digits[3] - '0');
  if (prefix1 == 4) {
    // Visa
    return digit_count == 13 || digit_count == 16 || digit_count == 18 || digit_count == 19;
  }
  if ((51 <= prefix2 && prefix2 <= 55) || (2221 <= prefix4 && prefix4 <= 2720)) {
    // mastercard
    return digit_count == 16;
  }
  if (prefix2 == 34 || prefix2 == 37) {
    // American Express
    return digit_count == 15;
  }
  if (prefix2 == 62 || prefix2 == 81) {
    // UnionPay
    return digit_count >= 16;
  }
  if (2200 <= prefix4 && prefix4 <= 2204) {
    // MIR
    return digit_count == 16;
  }
  return true;  // skip length check
}

bool is_email_address(Slice str) {
  // /^([a-z0-9_-]{0,26}[.+]){0,10}[a-z0-9_-]{1,35}@(([a-z0-9][a-z0-9_-]{0,28})?[a-z0-9][.]){1,6}[a-z]{2,6}$/i
  Slice userdata;
  Slice domain;
  std::tie(userdata, domain) = split(str, '@');
  vector<Slice> userdata_parts;
  size_t prev = 0;
  for (size_t i = 0; i < userdata.size(); i++) {
    if (userdata[i] == '.' || userdata[i] == '+') {
      userdata_parts.push_back(userdata.substr(prev, i - prev));
      prev = i + 1;
    }
  }
  userdata_parts.push_back(userdata.substr(prev));
  if (userdata_parts.size() >= 12) {
    return false;
  }
  for (auto &part : userdata_parts) {
    for (auto c : part) {
      if (!is_alpha_digit_or_underscore_or_minus(c)) {
        return false;
      }
    }
  }
  if (userdata_parts.back().empty() || userdata_parts.back().size() >= 36) {
    return false;
  }
  userdata_parts.pop_back();
  for (auto &part : userdata_parts) {
    if (part.size() >= 27) {
      return false;
    }
  }

  vector<Slice> domain_parts = full_split(domain, '.');
  if (domain_parts.size() <= 1 || domain_parts.size() > 7) {
    return false;
  }
  if (domain_parts.back().size() <= 1 || domain_parts.back().size() >= 7) {
    return false;
  }
  for (auto c : domain_parts.back()) {
    if (!is_alpha(c)) {
      return false;
    }
  }
  domain_parts.pop_back();
  for (auto &part : domain_parts) {
    if (part.empty() || part.size() >= 31) {
      return false;
    }
    for (auto c : part) {
      if (!is_alpha_digit_or_underscore_or_minus(c)) {
        return false;
      }
    }
    if (!is_alpha_digit(part[0])) {
      return false;
    }
    if (!is_alpha_digit(part.back())) {
      return false;
    }
  }

  return true;
}

static bool is_common_tld(Slice str) {
  static const std::unordered_set<Slice, SliceHash> tlds(
      {"aaa", "aarp", "abarth", "abb", "abbott", "abbvie", "abc", "able", "abogado", "abudhabi", "ac", "academy",
       "accenture", "accountant", "accountants", "aco", "active", "actor", "ad", "adac", "ads", "adult", "ae", "aeg",
       "aero", "aetna", "af", "afamilycompany", "afl", "africa", "ag", "agakhan", "agency", "ai", "aig", "aigo",
       "airbus", "airforce", "airtel", "akdn", "al", "alfaromeo", "alibaba", "alipay", "allfinanz", "allstate", "ally",
       "alsace", "alstom", "am", "americanexpress", "americanfamily", "amex", "amfam", "amica", "amsterdam",
       "analytics", "android", "anquan", "anz", "ao", "aol", "apartments", "app", "apple", "aq", "aquarelle", "ar",
       "arab", "aramco", "archi", "army", "arpa", "art", "arte", "as", "asda", "asia", "associates", "at", "athleta",
       "attorney", "au", "auction", "audi", "audible", "audio", "auspost", "author", "auto", "autos", "avianca", "aw",
       "aws", "ax", "axa", "az", "azure", "ba", "baby", "baidu", "banamex", "bananarepublic", "band", "bank", "bar",
       "barcelona", "barclaycard", "barclays", "barefoot", "bargains", "baseball", "basketball", "bauhaus", "bayern",
       "bb", "bbc", "bbt", "bbva", "bcg", "bcn", "bd", "be", "beats", "beauty", "beer", "bentley", "berlin", "best",
       "bestbuy", "bet", "bf", "bg", "bh", "bharti", "bi", "bible", "bid", "bike", "bing", "bingo", "bio", "biz", "bj",
       "black", "blackfriday", "blanco", "blockbuster", "blog", "bloomberg", "blue", "bm", "bms", "bmw", "bn", "bnl",
       "bnpparibas", "bo", "boats", "boehringer", "bofa", "bom", "bond", "boo", "book", "booking", "boots", "bosch",
       "bostik", "boston", "bot", "boutique", "box", "br", "bradesco", "bridgestone", "broadway", "broker", "brother",
       "brussels", "bs", "bt", "budapest", "bugatti", "build", "builders", "business", "buy", "buzz", "bv", "bw", "by",
       "bz", "bzh", "ca", "cab", "cafe", "cal", "call", "calvinklein", "cam", "camera", "camp", "cancerresearch",
       "canon", "capetown", "capital", "capitalone", "car", "caravan", "cards", "care", "career", "careers", "cars",
       "cartier", "casa", "case", "caseih", "cash", "casino", "cat", "catering", "catholic", "cba", "cbn", "cbre",
       "cbs", "cc", "cd", "ceb", "center", "ceo", "cern", "cf", "cfa", "cfd", "cg", "ch", "chanel", "channel", "chase",
       "chat", "cheap", "chintai", "christmas", "chrome", "chrysler", "church", "ci", "cipriani", "circle", "cisco",
       "citadel", "citi", "citic", "city", "cityeats", "ck", "cl", "claims", "cleaning", "click", "clinic", "clinique",
       "clothing", "cloud", "club", "clubmed", "cm", "cn", "co", "coach", "codes", "coffee", "college", "cologne",
       "com", "comcast", "commbank", "community", "company", "compare", "computer", "comsec", "condos", "construction",
       "consulting", "contact", "contractors", "cooking", "cookingchannel", "cool", "coop", "corsica", "country",
       "coupon", "coupons", "courses", "cr", "credit", "creditcard", "creditunion", "cricket", "crown", "crs", "cruise",
       "cruises", "csc", "cu", "cuisinella", "cv", "cw", "cx", "cy", "cymru", "cyou", "cz", "dabur", "dad", "dance",
       "data", "date", "dating", "datsun", "day", "dclk", "dds", "de", "deal", "dealer", "deals", "degree", "delivery",
       "dell", "deloitte", "delta", "democrat", "dental", "dentist", "desi", "design", "dev", "dhl", "diamonds", "diet",
       "digital", "direct", "directory", "discount", "discover", "dish", "diy", "dj", "dk", "dm", "dnp", "do", "docs",
       "doctor", "dodge", "dog", "doha", "domains", "dot", "download", "drive", "dtv", "dubai", "duck", "dunlop",
       "duns", "dupont", "durban", "dvag", "dvr", "dz", "earth", "eat", "ec", "eco", "edeka", "edu", "education", "ee",
       "eg", "email", "emerck", "energy", "engineer", "engineering", "enterprises", "epost", "epson", "equipment", "er",
       "ericsson", "erni", "es", "esq", "estate", "esurance", "et", "etisalat", "eu", "eurovision", "eus", "events",
       "everbank", "exchange", "expert", "exposed", "express", "extraspace", "fage", "fail", "fairwinds", "faith",
       "family", "fan", "fans", "farm", "farmers", "fashion", "fast", "fedex", "feedback", "ferrari", "ferrero", "fi",
       "fiat", "fidelity", "fido", "film", "final", "finance", "financial", "fire", "firestone", "firmdale", "fish",
       "fishing", "fit", "fitness", "fj", "fk", "flickr", "flights", "flir", "florist", "flowers", "fly", "fm", "fo",
       "foo", "food", "foodnetwork", "football", "ford", "forex", "forsale", "forum", "foundation", "fox", "fr", "free",
       "fresenius", "frl", "frogans", "frontdoor", "frontier", "ftr", "fujitsu", "fujixerox", "fun", "fund",
       "furniture", "futbol", "fyi", "ga", "gal", "gallery", "gallo", "gallup", "game", "games", "gap", "garden", "gb",
       "gbiz", "gd", "gdn", "ge", "gea", "gent", "genting", "george", "gf", "gg", "ggee", "gh", "gi", "gift", "gifts",
       "gives", "giving", "gl", "glade", "glass", "gle", "global", "globo", "gm", "gmail", "gmbh", "gmo", "gmx", "gn",
       "godaddy", "gold", "goldpoint", "golf", "goo", "goodhands", "goodyear", "goog", "google", "gop", "got", "gov",
       "gp", "gq", "gr", "grainger", "graphics", "gratis", "green", "gripe", "grocery", "group", "gs", "gt", "gu",
       "guardian", "gucci", "guge", "guide", "guitars", "guru", "gw", "gy", "hair", "hamburg", "hangout", "haus", "hbo",
       "hdfc", "hdfcbank", "health", "healthcare", "help", "helsinki", "here", "hermes", "hgtv", "hiphop", "hisamitsu",
       "hitachi", "hiv", "hk", "hkt", "hm", "hn", "hockey", "holdings", "holiday", "homedepot", "homegoods", "homes",
       "homesense", "honda", "honeywell", "horse", "hospital", "host", "hosting", "hot", "hoteles", "hotels", "hotmail",
       "house", "how", "hr", "hsbc", "ht", "hu", "hughes", "hyatt", "hyundai", "ibm", "icbc", "ice", "icu", "id", "ie",
       "ieee", "ifm", "ikano", "il", "im", "imamat", "imdb", "immo", "immobilien", "in", "industries", "infiniti",
       "info", "ing", "ink", "institute", "insurance", "insure", "int", "intel", "international", "intuit",
       "investments", "io", "ipiranga", "iq", "ir", "irish", "is", "iselect", "ismaili", "ist", "istanbul", "it",
       "itau", "itv", "iveco", "iwc", "jaguar", "java", "jcb", "jcp", "je", "jeep", "jetzt", "jewelry", "jio", "jlc",
       "jll", "jm", "jmp", "jnj", "jo", "jobs", "joburg", "jot", "joy", "jp", "jpmorgan", "jprs", "juegos", "juniper",
       "kaufen", "kddi", "ke", "kerryhotels", "kerrylogistics", "kerryproperties", "kfh", "kg", "kh", "ki", "kia",
       "kim", "kinder", "kindle", "kitchen", "kiwi", "km", "kn", "koeln", "komatsu", "kosher", "kp", "kpmg", "kpn",
       "kr", "krd", "kred", "kuokgroup", "kw", "ky", "kyoto", "kz", "la", "lacaixa", "ladbrokes", "lamborghini",
       "lamer", "lancaster", "lancia", "lancome", "land", "landrover", "lanxess", "lasalle", "lat", "latino", "latrobe",
       "law", "lawyer", "lb", "lc", "lds", "lease", "leclerc", "lefrak", "legal", "lego", "lexus", "lgbt", "li",
       "liaison", "lidl", "life", "lifeinsurance", "lifestyle", "lighting", "like", "lilly", "limited", "limo",
       "lincoln", "linde", "link", "lipsy", "live", "living", "lixil", "lk", "loan", "loans", "locker", "locus", "loft",
       "lol", "london", "lotte", "lotto", "love", "lpl", "lplfinancial", "lr", "ls", "lt", "ltd", "ltda", "lu",
       "lundbeck", "lupin", "luxe", "luxury", "lv", "ly", "ma", "macys", "madrid", "maif", "maison", "makeup", "man",
       "management", "mango", "map", "market", "marketing", "markets", "marriott", "marshalls", "maserati", "mattel",
       "mba", "mc", "mckinsey", "md", "me", "med", "media", "meet", "melbourne", "meme", "memorial", "men", "menu",
       "meo", "merckmsd", "metlife", "mg", "mh", "miami", "microsoft", "mil", "mini", "mint", "mit", "mitsubishi", "mk",
       "ml", "mlb", "mls", "mm", "mma", "mn", "mo", "mobi", "mobile", "mobily", "moda", "moe", "moi", "mom", "monash",
       "money", "monster", "mopar", "mormon", "mortgage", "moscow", "moto", "motorcycles", "mov", "movie", "movistar",
       "mp", "mq", "mr", "ms", "msd", "mt", "mtn", "mtr", "mu", "museum", "mutual", "mv", "mw", "mx", "my", "mz", "na",
       "nab", "nadex", "nagoya", "name", "nationwide", "natura", "navy", "nba", "nc", "ne", "nec", "net", "netbank",
       "netflix", "network", "neustar", "new", "newholland", "news", "next", "nextdirect", "nexus", "nf", "nfl", "ng",
       "ngo", "nhk", "ni", "nico", "nike", "nikon", "ninja", "nissan", "nissay", "nl", "no", "nokia",
       "northwesternmutual", "norton", "now", "nowruz", "nowtv", "np", "nr", "nra", "nrw", "ntt", "nu", "nyc", "nz",
       "obi", "observer", "off", "office", "okinawa", "olayan", "olayangroup", "oldnavy", "ollo", "om", "omega", "one",
       "ong", "onion", "onl", "online", "onyourside", "ooo", "open", "oracle", "orange", "org", "organic", "origins",
       "osaka", "otsuka", "ott", "ovh", "pa", "page", "panasonic", "panerai", "paris", "pars", "partners", "parts",
       "party", "passagens", "pay", "pccw", "pe", "pet", "pf", "pfizer", "pg", "ph", "pharmacy", "phd", "philips",
       "phone", "photo", "photography", "photos", "physio", "piaget", "pics", "pictet", "pictures", "pid", "pin",
       "ping", "pink", "pioneer", "pizza", "pk", "pl", "place", "play", "playstation", "plumbing", "plus", "pm", "pn",
       "pnc", "pohl", "poker", "politie", "porn", "post", "pr", "pramerica", "praxi", "press", "prime", "pro", "prod",
       "productions", "prof", "progressive", "promo", "properties", "property", "protection", "pru", "prudential", "ps",
       "pt", "pub", "pw", "pwc", "py", "qa", "qpon", "quebec", "quest", "qvc", "racing", "radio", "raid", "re", "read",
       "realestate", "realtor", "realty", "recipes", "red", "redstone", "redumbrella", "rehab", "reise", "reisen",
       "reit", "reliance", "ren", "rent", "rentals", "repair", "report", "republican", "rest", "restaurant", "review",
       "reviews", "rexroth", "rich", "richardli", "ricoh", "rightathome", "ril", "rio", "rip", "rmit", "ro", "rocher",
       "rocks", "rodeo", "rogers", "room", "rs", "rsvp", "ru", "rugby", "ruhr", "run", "rw", "rwe", "ryukyu", "sa",
       "saarland", "safe", "safety", "sakura", "sale", "salon", "samsclub", "samsung", "sandvik", "sandvikcoromant",
       "sanofi", "sap", "sapo", "sarl", "sas", "save", "saxo", "sb", "sbi", "sbs", "sc", "sca", "scb", "schaeffler",
       "schmidt", "scholarships", "school", "schule", "schwarz", "science", "scjohnson", "scor", "scot", "sd", "se",
       "search", "seat", "secure", "security", "seek", "select", "sener", "services", "ses", "seven", "sew", "sex",
       "sexy", "sfr", "sg", "sh", "shangrila", "sharp", "shaw", "shell", "shia", "shiksha", "shoes", "shop", "shopping",
       "shouji", "show", "showtime", "shriram", "si", "silk", "sina", "singles", "site", "sj", "sk", "ski", "skin",
       "sky", "skype", "sl", "sling", "sm", "smart", "smile", "sn", "sncf", "so", "soccer", "social", "softbank",
       "software", "sohu", "solar", "solutions", "song", "sony", "soy", "space", "spiegel", "sport", "spot",
       "spreadbetting", "sr", "srl", "srt", "st", "stada", "staples", "star", "starhub", "statebank", "statefarm",
       "statoil", "stc", "stcgroup", "stockholm", "storage", "store", "stream", "studio", "study", "style", "su",
       "sucks", "supplies", "supply", "support", "surf", "surgery", "suzuki", "sv", "swatch", "swiftcover", "swiss",
       "sx", "sy", "sydney", "symantec", "systems", "sz", "tab", "taipei", "talk", "taobao", "target", "tatamotors",
       "tatar", "tattoo", "tax", "taxi", "tc", "tci", "td", "tdk", "team", "tech", "technology", "tel", "telecity",
       "telefonica", "temasek", "tennis", "teva", "tf", "tg", "th", "thd", "theater", "theatre", "tiaa", "tickets",
       "tienda", "tiffany", "tips", "tires", "tirol", "tj", "tjmaxx", "tjx", "tk", "tkmaxx", "tl", "tm", "tmall", "tn",
       "to", "today", "tokyo", "tools", "top", "toray", "toshiba", "total", "tours", "town", "toyota", "toys", "tr",
       "trade", "trading", "training", "travel", "travelchannel", "travelers", "travelersinsurance", "trust", "trv",
       "tt", "tube", "tui", "tunes", "tushu", "tv", "tvs", "tw", "tz", "ua", "ubank", "ubs", "uconnect", "ug", "uk",
       "unicom", "university", "uno", "uol", "ups", "us", "uy", "uz", "va", "vacations", "vana", "vanguard", "vc", "ve",
       "vegas", "ventures", "verisign", "versicherung", "vet", "vg", "vi", "viajes", "video", "vig", "viking", "villas",
       "vin", "vip", "virgin", "visa", "vision", "vista", "vistaprint", "viva", "vivo", "vlaanderen", "vn", "vodka",
       "volkswagen", "volvo", "vote", "voting", "voto", "voyage", "vu", "vuelos", "wales", "walmart", "walter", "wang",
       "wanggou", "warman", "watch", "watches", "weather", "weatherchannel", "webcam", "weber", "website", "wed",
       "wedding", "weibo", "weir", "wf", "whoswho", "wien", "wiki", "williamhill", "win", "windows", "wine", "winners",
       "wme", "wolterskluwer", "woodside", "work", "works", "world", "wow", "ws", "wtc", "wtf", "xbox", "xerox",
       "xfinity", "xihuan", "xin", "कॉम", "セール", "佛山", "ಭಾರತ", "慈善", "集团", "在线", "한국", "ଭାରତ", "大众汽车",
       "点看", "คอม", "ভাৰত", "ভারত", "八卦", "موقع", "বাংলা", "公益", "公司", "香格里拉", "网站", "移动", "我爱你",
       "москва", "қаз", "католик", "онлайн", "сайт", "联通", "срб", "бг", "бел", "קום", "时尚", "微博", "淡马锡",
       "ファッション", "орг", "नेट", "ストア", "삼성", "சிங்கப்பூர்", "商标", "商店", "商城", "дети", "мкд", "ею",
       "ポイント", "新闻", "工行", "家電", "كوم", "中文网", "中信", "中国", "中國", "娱乐", "谷歌", "భారత్", "ලංකා",
       "電訊盈科", "购物", "クラウド", "ભારત", "通販", "भारतम्", "भारत", "भारोत", "网店", "संगठन", "餐厅", "网络", "ком",
       "укр", "香港", "诺基亚", "食品", "飞利浦", "台湾", "台灣", "手表", "手机", "мон", "الجزائر", "عمان", "ارامكو",
       "ایران", "العليان", "اتصالات", "امارات", "بازار", "پاکستان", "الاردن", "موبايلي", "بارت", "بھارت", "المغرب",
       "ابوظبي", "السعودية", "ڀارت", "كاثوليك", "سودان", "همراه", "عراق", "مليسيا", "澳門", "닷컴", "政府", "شبكة",
       "بيتك", "عرب", "გე", "机构", "组织机构", "健康", "ไทย", "سورية", "招聘", "рус", "рф", "珠宝", "تونس", "大拿",
       "みんな", "グーグル", "ελ", "世界", "書籍", "ഭാരതം", "ਭਾਰਤ", "网址", "닷넷", "コム", "天主教", "游戏",
       "vermögensberater", "vermögensberatung", "企业", "信息", "嘉里大酒店", "嘉里", "مصر", "قطر", "广东", "இலங்கை",
       "இந்தியா", "հայ", "新加坡", "فلسطين", "政务", "xperia", "xxx", "xyz", "yachts", "yahoo", "yamaxun", "yandex",
       "ye", "yodobashi", "yoga", "yokohama", "you", "youtube", "yt", "yun", "za", "zappos", "zara", "zero", "zip",
       "zippo", "zm", "zone", "zuerich",
       // comment for clang-format to prevent him from placing all strings on separate lines
       "zw"});
  string str_lower = utf8_to_lower(str);
  if (str_lower != str && utf8_substr(Slice(str_lower), 1) == utf8_substr(str, 1)) {
    return false;
  }
  return tlds.count(str_lower) > 0;
}

Slice fix_url(Slice str) {
  auto full_url = str;

  bool has_protocol = false;
  auto str_begin = to_lower(str.substr(0, 8));
  if (begins_with(str_begin, "http://") || begins_with(str_begin, "https://") || begins_with(str_begin, "sftp://") ||
      begins_with(str_begin, "ftp://")) {
    auto pos = str.find(':');
    str = str.substr(pos + 3);
    has_protocol = true;
  }
  auto domain_end = std::min({str.size(), str.find('/'), str.find('?'), str.find('#')});  // TODO server: str.find('#')
  auto domain = str.substr(0, domain_end);
  auto path = str.substr(domain_end);

  auto at_pos = domain.find('@');
  if (at_pos < domain.size()) {
    domain.remove_prefix(at_pos + 1);
  }
  domain.truncate(domain.rfind(':'));

  string domain_lower = domain.str();
  to_lower_inplace(domain_lower);
  if (domain_lower == "teiegram.org") {
    return Slice();
  }

  int32 balance[3] = {0, 0, 0};
  size_t path_pos;
  for (path_pos = 0; path_pos < path.size(); path_pos++) {
    switch (path[path_pos]) {
      case '(':
        balance[0]++;
        break;
      case '[':
        balance[1]++;
        break;
      case '{':
        balance[2]++;
        break;
      case ')':
        balance[0]--;
        break;
      case ']':
        balance[1]--;
        break;
      case '}':
        balance[2]--;
        break;
    }
    if (balance[0] < 0 || balance[1] < 0 || balance[2] < 0) {
      break;
    }
  }
  Slice bad_path_end_chars(".:;,('?!`");
  while (path_pos > 0 && bad_path_end_chars.find(path[path_pos - 1]) < bad_path_end_chars.size()) {
    path_pos--;
  }
  full_url.remove_suffix(path.size() - path_pos);

  vector<Slice> domain_parts = full_split(domain, '.');
  if (domain_parts.size() <= 1) {
    return Slice();
  }

  bool is_ipv4 = domain_parts.size() == 4;
  bool has_non_digit = false;
  for (auto &part : domain_parts) {
    if (part.empty() || part.size() >= 64) {
      return Slice();
    }
    if (part.back() == '-') {
      return Slice();
    }

    if (!has_non_digit) {
      if (part.size() > 3) {
        is_ipv4 = false;
      }
      for (auto c : part) {
        if (!is_digit(c)) {
          is_ipv4 = false;
          has_non_digit = true;
        }
      }
      if (part.size() == 3 &&
          (part[0] >= '3' || (part[0] == '2' && (part[1] >= '6' || (part[1] == '5' && part[2] >= '6'))))) {
        is_ipv4 = false;
      }
      if (part[0] == '0' && part.size() >= 2) {
        is_ipv4 = false;
      }
    }
  }

  if (is_ipv4) {
    return full_url;
  }

  if (!has_non_digit) {
    return Slice();
  }

  auto tld = domain_parts.back();
  if (utf8_length(tld) <= 1) {
    return Slice();
  }

  if (begins_with(tld, "xn--")) {
    if (tld.size() <= 5) {
      return Slice();
    }
    for (auto c : tld.substr(4)) {
      if (!is_alpha_digit(c)) {
        return Slice();
      }
    }
  } else {
    if (tld.find('_') < tld.size()) {
      return Slice();
    }
    if (tld.find('-') < tld.size()) {
      return Slice();
    }

    if (!has_protocol && !is_common_tld(tld)) {
      return Slice();
    }
  }

  domain_parts.pop_back();
  if (domain_parts.back().find('_') < domain_parts.back().size()) {
    return Slice();
  }

  return full_url;
}

const std::unordered_set<Slice, SliceHash> &get_valid_short_usernames() {
  static const std::unordered_set<Slice, SliceHash> valid_usernames{"gif",  "wiki", "vid",  "bing", "pic",
                                                                    "bold", "imdb", "coub", "like", "vote"};
  return valid_usernames;
}

vector<Slice> find_mentions(Slice str) {
  auto mentions = match_mentions(str);
  td::remove_if(mentions, [](Slice mention) {
    mention.remove_prefix(1);
    if (mention.size() >= 5) {
      return false;
    }
    return get_valid_short_usernames().count(mention) == 0;
  });
  return mentions;
}

vector<Slice> find_bot_commands(Slice str) {
  return match_bot_commands(str);
}

vector<Slice> find_hashtags(Slice str) {
  return match_hashtags(str);
}

vector<Slice> find_cashtags(Slice str) {
  return match_cashtags(str);
}

vector<Slice> find_bank_card_numbers(Slice str) {
  vector<Slice> result;
  for (auto bank_card : match_bank_card_numbers(str)) {
    if (is_valid_bank_card(bank_card)) {
      result.emplace_back(bank_card);
    }
  }
  return result;
}

vector<std::pair<Slice, bool>> find_urls(Slice str) {
  vector<std::pair<Slice, bool>> result;
  for (auto url : match_urls(str)) {
    if (is_email_address(url)) {
      result.emplace_back(url, true);
    } else if (begins_with(url, "mailto:") && is_email_address(url.substr(7))) {
      result.emplace_back(url.substr(7), true);
    } else {
      url = fix_url(url);
      if (!url.empty()) {
        result.emplace_back(url, false);
      }
    }
  }
  return result;
}

static int32 text_length(Slice text) {
  return narrow_cast<int32>(utf8_utf16_length(text));
}

static void sort_entities(vector<MessageEntity> &entities) {
  if (std::is_sorted(entities.begin(), entities.end())) {
    return;
  }

  std::sort(entities.begin(), entities.end());
}

#define check_is_sorted(entities) check_is_sorted_impl(entities, __LINE__)
static void check_is_sorted_impl(const vector<MessageEntity> &entities, int line) {
  LOG_CHECK(std::is_sorted(entities.begin(), entities.end())) << line << " " << entities;
}

#define check_non_intersecting(entities) check_non_intersecting_impl(entities, __LINE__)
static void check_non_intersecting_impl(const vector<MessageEntity> &entities, int line) {
  for (size_t i = 0; i + 1 < entities.size(); i++) {
    LOG_CHECK(entities[i].offset + entities[i].length <= entities[i + 1].offset) << line << " " << entities;
  }
}

static constexpr int32 get_entity_type_mask(MessageEntity::Type type) {
  return 1 << static_cast<int32>(type);
}

static constexpr int32 get_splittable_entities_mask() {
  return get_entity_type_mask(MessageEntity::Type::Bold) | get_entity_type_mask(MessageEntity::Type::Italic) |
         get_entity_type_mask(MessageEntity::Type::Underline) |
         get_entity_type_mask(MessageEntity::Type::Strikethrough);
}

static constexpr int32 get_blockquote_entities_mask() {
  return get_entity_type_mask(MessageEntity::Type::BlockQuote);
}

static constexpr int32 get_continuous_entities_mask() {
  return get_entity_type_mask(MessageEntity::Type::Mention) | get_entity_type_mask(MessageEntity::Type::Hashtag) |
         get_entity_type_mask(MessageEntity::Type::BotCommand) | get_entity_type_mask(MessageEntity::Type::Url) |
         get_entity_type_mask(MessageEntity::Type::EmailAddress) | get_entity_type_mask(MessageEntity::Type::TextUrl) |
         get_entity_type_mask(MessageEntity::Type::MentionName) | get_entity_type_mask(MessageEntity::Type::Cashtag) |
         get_entity_type_mask(MessageEntity::Type::PhoneNumber) |
         get_entity_type_mask(MessageEntity::Type::BankCardNumber);
}

static constexpr int32 get_pre_entities_mask() {
  return get_entity_type_mask(MessageEntity::Type::Pre) | get_entity_type_mask(MessageEntity::Type::Code) |
         get_entity_type_mask(MessageEntity::Type::PreCode);
}

static constexpr int32 get_user_entities_mask() {
  return get_splittable_entities_mask() | get_blockquote_entities_mask() |
         get_entity_type_mask(MessageEntity::Type::TextUrl) | get_entity_type_mask(MessageEntity::Type::MentionName) |
         get_pre_entities_mask();
}

static int32 is_splittable_entity(MessageEntity::Type type) {
  return (get_entity_type_mask(type) & get_splittable_entities_mask()) != 0;
}

static int32 is_blockquote_entity(MessageEntity::Type type) {
  return type == MessageEntity::Type::BlockQuote;
}

static int32 is_continuous_entity(MessageEntity::Type type) {
  return (get_entity_type_mask(type) & get_continuous_entities_mask()) != 0;
}

static int32 is_pre_entity(MessageEntity::Type type) {
  return (get_entity_type_mask(type) & get_pre_entities_mask()) != 0;
}

static int32 is_user_entity(MessageEntity::Type type) {
  return (get_entity_type_mask(type) & get_user_entities_mask()) != 0;
}

static constexpr size_t SPLITTABLE_ENTITY_TYPE_COUNT = 4;

static size_t get_splittable_entity_type_index(MessageEntity::Type type) {
  if (static_cast<int32>(type) <= static_cast<int32>(MessageEntity::Type::Bold) + 1) {
    // Bold or Italic
    return static_cast<int32>(type) - static_cast<int32>(MessageEntity::Type::Bold);
  } else {
    // Underline or Strikethrough
    return static_cast<int32>(type) - static_cast<int32>(MessageEntity::Type::Underline) + 2;
  }
}

static bool are_entities_valid(const vector<MessageEntity> &entities) {
  if (entities.empty()) {
    return true;
  }
  check_is_sorted(entities);

  int32 end_pos[SPLITTABLE_ENTITY_TYPE_COUNT];
  std::fill_n(end_pos, SPLITTABLE_ENTITY_TYPE_COUNT, -1);
  vector<const MessageEntity *> nested_entities_stack;
  int32 nested_entity_type_mask = 0;
  for (auto &entity : entities) {
    while (!nested_entities_stack.empty() &&
           entity.offset >= nested_entities_stack.back()->offset + nested_entities_stack.back()->length) {
      // remove non-intersecting entities from the stack
      nested_entity_type_mask -= get_entity_type_mask(nested_entities_stack.back()->type);
      nested_entities_stack.pop_back();
    }

    if (!nested_entities_stack.empty()) {
      if (entity.offset + entity.length > nested_entities_stack.back()->offset + nested_entities_stack.back()->length) {
        // entity intersects some previous entity
        return false;
      }
      if ((nested_entity_type_mask & get_entity_type_mask(entity.type)) != 0) {
        // entity has the same type as one of the previous nested
        return false;
      }
      auto parent_type = nested_entities_stack.back()->type;
      if (is_pre_entity(parent_type)) {
        // Pre and Code can't contain nested entities
        return false;
      }
      // parents are not pre after this point
      if (is_pre_entity(entity.type) && (nested_entity_type_mask & ~get_blockquote_entities_mask()) != 0) {
        // Pre and Code can't be contained in other entities, except blockquote
        return false;
      }
      if ((is_continuous_entity(entity.type) || is_blockquote_entity(entity.type)) &&
          (nested_entity_type_mask & get_continuous_entities_mask()) != 0) {
        // continuous and blockquote can't be contained in continuous
        return false;
      }
      if ((nested_entity_type_mask & get_splittable_entities_mask()) != 0) {
        // the previous nested entity may be needed to splitted for consistency
        // alternatively, better entity merging needs to be implemented
        return false;
      }
    }

    if (is_splittable_entity(entity.type)) {
      auto index = get_splittable_entity_type_index(entity.type);
      if (end_pos[index] >= entity.offset) {
        // the entities can be merged
        return false;
      }
      end_pos[index] = entity.offset + entity.length;
    }
    nested_entities_stack.push_back(&entity);
    nested_entity_type_mask += get_entity_type_mask(entity.type);
  }
  return true;
}

// removes all intersecting entities, including nested
static void remove_intersecting_entities(vector<MessageEntity> &entities) {
  check_is_sorted(entities);
  int32 last_entity_end = 0;
  size_t left_entities = 0;
  for (size_t i = 0; i < entities.size(); i++) {
    CHECK(entities[i].length > 0);
    if (entities[i].offset >= last_entity_end) {
      last_entity_end = entities[i].offset + entities[i].length;
      if (i != left_entities) {
        entities[left_entities] = std::move(entities[i]);
      }
      left_entities++;
    }
  }
  entities.erase(entities.begin() + left_entities, entities.end());
}

// continuous_entities and blockquote_entities must be pre-sorted and non-overlapping
static void remove_entities_intersecting_blockquote(vector<MessageEntity> &entities,
                                                    const vector<MessageEntity> &blockquote_entities) {
  check_non_intersecting(entities);
  check_non_intersecting(blockquote_entities);
  if (blockquote_entities.empty()) {
    // fast path
    return;
  }

  auto blockquote_it = blockquote_entities.begin();
  size_t left_entities = 0;
  for (size_t i = 0; i < entities.size(); i++) {
    while (blockquote_it != blockquote_entities.end() &&
           (blockquote_it->type != MessageEntity::Type::BlockQuote ||
            blockquote_it->offset + blockquote_it->length <= entities[i].offset)) {
      blockquote_it++;
    }
    if (blockquote_it != blockquote_entities.end() &&
        (blockquote_it->offset + blockquote_it->length < entities[i].offset + entities[i].length ||
         (entities[i].offset < blockquote_it->offset &&
          blockquote_it->offset < entities[i].offset + entities[i].length))) {
      continue;
    }
    if (i != left_entities) {
      entities[left_entities] = std::move(entities[i]);
    }
    left_entities++;
  }
  entities.erase(entities.begin() + left_entities, entities.end());
}

vector<MessageEntity> find_entities(Slice text, bool skip_bot_commands, bool only_urls) {
  vector<MessageEntity> entities;

  if (!only_urls) {
    auto add_entities = [&entities, &text](MessageEntity::Type type, vector<Slice> (*find_entities_f)(Slice)) mutable {
      auto new_entities = find_entities_f(text);
      for (auto &entity : new_entities) {
        auto offset = narrow_cast<int32>(entity.begin() - text.begin());
        auto length = narrow_cast<int32>(entity.size());
        entities.emplace_back(type, offset, length);
      }
    };
    add_entities(MessageEntity::Type::Mention, find_mentions);
    if (!skip_bot_commands) {
      add_entities(MessageEntity::Type::BotCommand, find_bot_commands);
    }
    add_entities(MessageEntity::Type::Hashtag, find_hashtags);
    add_entities(MessageEntity::Type::Cashtag, find_cashtags);
    // TODO find_phone_numbers
    add_entities(MessageEntity::Type::BankCardNumber, find_bank_card_numbers);
  }

  auto urls = find_urls(text);
  for (auto &url : urls) {
    auto type = url.second ? MessageEntity::Type::EmailAddress : MessageEntity::Type::Url;
    if (only_urls && type != MessageEntity::Type::Url) {
      continue;
    }
    auto offset = narrow_cast<int32>(url.first.begin() - text.begin());
    auto length = narrow_cast<int32>(url.first.size());
    entities.emplace_back(type, offset, length);
  }

  if (entities.empty()) {
    return entities;
  }

  sort_entities(entities);

  remove_intersecting_entities(entities);

  // fix offsets to UTF-16 offsets
  const unsigned char *begin = text.ubegin();
  const unsigned char *ptr = begin;
  const unsigned char *end = text.uend();

  int32 utf16_pos = 0;
  for (auto &entity : entities) {
    int cnt = 2;
    auto entity_begin = entity.offset;
    auto entity_end = entity.offset + entity.length;

    int32 pos = static_cast<int32>(ptr - begin);
    if (entity_begin == pos) {
      cnt--;
      entity.offset = utf16_pos;
    }

    while (ptr != end && cnt > 0) {
      unsigned char c = ptr[0];
      utf16_pos += 1 + (c >= 0xf0);
      ptr = next_utf8_unsafe(ptr, nullptr, "find_entities");

      pos = static_cast<int32>(ptr - begin);
      if (entity_begin == pos) {
        cnt--;
        entity.offset = utf16_pos;
      } else if (entity_end == pos) {
        cnt--;
        entity.length = utf16_pos - entity.offset;
      }
    }
    CHECK(cnt == 0);
  }

  return entities;
}

static vector<MessageEntity> merge_entities(vector<MessageEntity> old_entities, vector<MessageEntity> new_entities) {
  if (new_entities.empty()) {
    return old_entities;
  }
  if (old_entities.empty()) {
    return new_entities;
  }

  vector<MessageEntity> result;
  result.reserve(old_entities.size() + new_entities.size());

  auto new_it = new_entities.begin();
  auto new_end = new_entities.end();
  for (auto &old_entity : old_entities) {
    while (new_it != new_end && new_it->offset + new_it->length <= old_entity.offset) {
      result.push_back(std::move(*new_it));
      new_it++;
    }
    auto old_entity_end = old_entity.offset + old_entity.length;
    result.push_back(std::move(old_entity));
    while (new_it != new_end && new_it->offset < old_entity_end) {
      new_it++;
    }
  }
  while (new_it != new_end) {
    result.push_back(std::move(*new_it));
    new_it++;
  }

  return result;
}

static bool is_plain_domain(Slice url) {
  return url.find('/') >= url.size() && url.find('?') >= url.size() && url.find('#') >= url.size();
}

string get_first_url(Slice text, const vector<MessageEntity> &entities) {
  for (auto &entity : entities) {
    switch (entity.type) {
      case MessageEntity::Type::Mention:
        break;
      case MessageEntity::Type::Hashtag:
        break;
      case MessageEntity::Type::BotCommand:
        break;
      case MessageEntity::Type::Url: {
        Slice url = utf8_utf16_substr(text, entity.offset, entity.length);
        if (begins_with(url, "ton:") || begins_with(url, "tg:") || is_plain_domain(url)) {
          continue;
        }
        return url.str();
      }
      case MessageEntity::Type::EmailAddress:
        break;
      case MessageEntity::Type::Bold:
        break;
      case MessageEntity::Type::Italic:
        break;
      case MessageEntity::Type::Underline:
        break;
      case MessageEntity::Type::Strikethrough:
        break;
      case MessageEntity::Type::BlockQuote:
        break;
      case MessageEntity::Type::Code:
        break;
      case MessageEntity::Type::Pre:
        break;
      case MessageEntity::Type::PreCode:
        break;
      case MessageEntity::Type::TextUrl:
        if (begins_with(entity.argument, "ton:") || begins_with(entity.argument, "tg:")) {
          continue;
        }
        return entity.argument;
      case MessageEntity::Type::MentionName:
        break;
      case MessageEntity::Type::Cashtag:
        break;
      case MessageEntity::Type::PhoneNumber:
        break;
      case MessageEntity::Type::BankCardNumber:
        break;
      default:
        UNREACHABLE();
    }
  }

  return string();
}

static UserId get_link_user_id(Slice url) {
  string lower_cased_url = to_lower(url);
  url = lower_cased_url;

  Slice link_scheme("tg:");
  if (!begins_with(url, link_scheme)) {
    return UserId();
  }
  url.remove_prefix(link_scheme.size());
  if (begins_with(url, "//")) {
    url.remove_prefix(2);
  }

  Slice host("user");
  if (!begins_with(url, host)) {
    return UserId();
  }
  url.remove_prefix(host.size());
  if (begins_with(url, "/")) {
    url.remove_prefix(1);
  }
  if (!begins_with(url, "?")) {
    return UserId();
  }
  url.remove_prefix(1);
  url.truncate(url.find('#'));

  for (auto parameter : full_split(url, '&')) {
    Slice key;
    Slice value;
    std::tie(key, value) = split(parameter, '=');
    if (key == Slice("id")) {
      auto r_user_id = to_integer_safe<int32>(value);
      if (r_user_id.is_error()) {
        return UserId();
      }
      return UserId(r_user_id.ok());
    }
  }
  return UserId();
}

Result<vector<MessageEntity>> parse_markdown(string &text) {
  string result;
  vector<MessageEntity> entities;
  size_t size = text.size();
  int32 utf16_offset = 0;
  for (size_t i = 0; i < size; i++) {
    auto c = static_cast<unsigned char>(text[i]);
    if (c == '\\' && (text[i + 1] == '_' || text[i + 1] == '*' || text[i + 1] == '`' || text[i + 1] == '[')) {
      i++;
      result.push_back(text[i]);
      utf16_offset++;
      continue;
    }
    if (c != '_' && c != '*' && c != '`' && c != '[') {
      if (is_utf8_character_first_code_unit(c)) {
        utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
      }
      result.push_back(text[i]);
      continue;
    }

    // we are at begin of the entity
    size_t begin_pos = i;
    char end_character = text[i];
    bool is_pre = false;
    if (c == '[') {
      end_character = ']';
    }

    i++;

    string language;
    if (c == '`' && text[i] == '`' && text[i + 1] == '`') {
      i += 2;
      is_pre = true;
      size_t language_end = i;
      while (!is_space(text[language_end]) && text[language_end] != '`') {
        language_end++;
      }
      if (i != language_end && language_end < size && text[language_end] != '`') {
        language.assign(text, i, language_end - i);
        i = language_end;
      }
      // skip one new line in the beginning of the text
      if (text[i] == '\n' || text[i] == '\r') {
        if ((text[i + 1] == '\n' || text[i + 1] == '\r') && text[i] != text[i + 1]) {
          i += 2;
        } else {
          i++;
        }
      }
    }

    int32 entity_offset = utf16_offset;
    while (i < size && (text[i] != end_character || (is_pre && !(text[i + 1] == '`' && text[i + 2] == '`')))) {
      auto cur_ch = static_cast<unsigned char>(text[i]);
      if (is_utf8_character_first_code_unit(cur_ch)) {
        utf16_offset += 1 + (cur_ch >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
      }
      result.push_back(text[i++]);
    }
    if (i == size) {
      return Status::Error(400, PSLICE() << "Can't find end of the entity starting at byte offset " << begin_pos);
    }

    if (entity_offset != utf16_offset) {
      auto entity_length = utf16_offset - entity_offset;
      switch (c) {
        case '_':
          entities.emplace_back(MessageEntity::Type::Italic, entity_offset, entity_length);
          break;
        case '*':
          entities.emplace_back(MessageEntity::Type::Bold, entity_offset, entity_length);
          break;
        case '[': {
          string url;
          if (text[i + 1] != '(') {
            // use text as a url
            url.assign(text, begin_pos + 1, i - begin_pos - 1);
          } else {
            i += 2;
            while (i < size && text[i] != ')') {
              url.push_back(text[i++]);
            }
          }
          auto user_id = get_link_user_id(url);
          if (user_id.is_valid()) {
            entities.emplace_back(entity_offset, entity_length, user_id);
          } else {
            auto r_url = check_url(url);
            if (r_url.is_ok()) {
              entities.emplace_back(MessageEntity::Type::TextUrl, entity_offset, entity_length, r_url.move_as_ok());
            }
          }
          break;
        }
        case '`':
          if (is_pre) {
            if (language.empty()) {
              entities.emplace_back(MessageEntity::Type::Pre, entity_offset, entity_length);
            } else {
              entities.emplace_back(MessageEntity::Type::PreCode, entity_offset, entity_length, language);
            }
          } else {
            entities.emplace_back(MessageEntity::Type::Code, entity_offset, entity_length);
          }
          break;
        default:
          UNREACHABLE();
      }
    }
    if (is_pre) {
      i += 2;
    }
  }
  text = result;
  return entities;
}

static Result<vector<MessageEntity>> do_parse_markdown_v2(CSlice text, string &result) {
  vector<MessageEntity> entities;
  int32 utf16_offset = 0;

  struct EntityInfo {
    MessageEntity::Type type;
    string argument;
    int32 entity_offset;
    size_t entity_byte_offset;
    size_t entity_begin_pos;

    EntityInfo(MessageEntity::Type type, string argument, int32 entity_offset, size_t entity_byte_offset,
               size_t entity_begin_pos)
        : type(type)
        , argument(std::move(argument))
        , entity_offset(entity_offset)
        , entity_byte_offset(entity_byte_offset)
        , entity_begin_pos(entity_begin_pos) {
    }
  };
  std::vector<EntityInfo> nested_entities;

  for (size_t i = 0; i < text.size(); i++) {
    auto c = static_cast<unsigned char>(text[i]);
    if (c == '\\' && text[i + 1] > 0 && text[i + 1] <= 126) {
      i++;
      utf16_offset += 1;
      result += text[i];
      continue;
    }

    Slice reserved_characters("_*[]()~`>#+-=|{}.!");
    if (!nested_entities.empty()) {
      switch (nested_entities.back().type) {
        case MessageEntity::Type::Code:
        case MessageEntity::Type::Pre:
        case MessageEntity::Type::PreCode:
          reserved_characters = Slice("`");
          break;
        default:
          break;
      }
    }

    if (reserved_characters.find(text[i]) == Slice::npos) {
      if (is_utf8_character_first_code_unit(c)) {
        utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
      }
      result.push_back(text[i]);
      continue;
    }

    bool is_end_of_an_entity = false;
    if (!nested_entities.empty()) {
      is_end_of_an_entity = [&] {
        switch (nested_entities.back().type) {
          case MessageEntity::Type::Bold:
            return c == '*';
          case MessageEntity::Type::Italic:
            return c == '_' && text[i + 1] != '_';
          case MessageEntity::Type::Code:
            return c == '`';
          case MessageEntity::Type::Pre:
          case MessageEntity::Type::PreCode:
            return c == '`' && text[i + 1] == '`' && text[i + 2] == '`';
          case MessageEntity::Type::TextUrl:
            return c == ']';
          case MessageEntity::Type::Underline:
            return c == '_' && text[i + 1] == '_';
          case MessageEntity::Type::Strikethrough:
            return c == '~';
          default:
            UNREACHABLE();
            return false;
        }
      }();
    }

    if (!is_end_of_an_entity) {
      // begin of an entity
      MessageEntity::Type type;
      string argument;
      auto entity_byte_offset = i;
      switch (c) {
        case '_':
          if (text[i + 1] == '_') {
            type = MessageEntity::Type::Underline;
            i++;
          } else {
            type = MessageEntity::Type::Italic;
          }
          break;
        case '*':
          type = MessageEntity::Type::Bold;
          break;
        case '~':
          type = MessageEntity::Type::Strikethrough;
          break;
        case '[':
          type = MessageEntity::Type::TextUrl;
          break;
        case '`':
          if (text[i + 1] == '`' && text[i + 2] == '`') {
            i += 3;
            type = MessageEntity::Type::Pre;
            size_t language_end = i;
            while (!is_space(text[language_end]) && text[language_end] != '`') {
              language_end++;
            }
            if (i != language_end && language_end < text.size() && text[language_end] != '`') {
              type = MessageEntity::Type::PreCode;
              argument = text.substr(i, language_end - i).str();
              i = language_end;
            }
            // skip one new line in the beginning of the text
            if (text[i] == '\n' || text[i] == '\r') {
              if ((text[i + 1] == '\n' || text[i + 1] == '\r') && text[i] != text[i + 1]) {
                i += 2;
              } else {
                i++;
              }
            }

            i--;
          } else {
            type = MessageEntity::Type::Code;
          }
          break;
        default:
          return Status::Error(
              400, PSLICE() << "Character '" << text[i] << "' is reserved and must be escaped with the preceding '\\'");
      }
      nested_entities.emplace_back(type, std::move(argument), utf16_offset, entity_byte_offset, result.size());
    } else {
      // end of an entity
      auto type = nested_entities.back().type;
      auto argument = std::move(nested_entities.back().argument);
      UserId user_id;
      bool skip_entity = utf16_offset == nested_entities.back().entity_offset;
      switch (type) {
        case MessageEntity::Type::Bold:
        case MessageEntity::Type::Italic:
        case MessageEntity::Type::Code:
        case MessageEntity::Type::Strikethrough:
          break;
        case MessageEntity::Type::Underline:
          i++;
          break;
        case MessageEntity::Type::Pre:
        case MessageEntity::Type::PreCode:
          i += 2;
          break;
        case MessageEntity::Type::TextUrl: {
          string url;
          if (text[i + 1] != '(') {
            // use text as a url
            url = result.substr(nested_entities.back().entity_begin_pos);
          } else {
            i += 2;
            auto url_begin_pos = i;
            while (i < text.size() && text[i] != ')') {
              if (text[i] == '\\' && text[i + 1] > 0 && text[i + 1] <= 126) {
                url += text[i + 1];
                i += 2;
                continue;
              }
              url += text[i++];
            }
            if (text[i] != ')') {
              return Status::Error(400, PSLICE() << "Can't find end of a URL at byte offset " << url_begin_pos);
            }
          }
          user_id = get_link_user_id(url);
          if (!user_id.is_valid()) {
            auto r_url = check_url(url);
            if (r_url.is_error()) {
              skip_entity = true;
            } else {
              argument = r_url.move_as_ok();
            }
          }
          break;
        }
        default:
          UNREACHABLE();
          return false;
      }

      if (!skip_entity) {
        auto entity_offset = nested_entities.back().entity_offset;
        auto entity_length = utf16_offset - entity_offset;
        if (user_id.is_valid()) {
          entities.emplace_back(entity_offset, entity_length, user_id);
        } else {
          entities.emplace_back(type, entity_offset, entity_length, std::move(argument));
        }
      }
      nested_entities.pop_back();
    }
  }
  if (!nested_entities.empty()) {
    return Status::Error(400, PSLICE() << "Can't find end of " << nested_entities.back().type
                                       << " entity at byte offset " << nested_entities.back().entity_byte_offset);
  }

  sort_entities(entities);

  return entities;
}

Result<vector<MessageEntity>> parse_markdown_v2(string &text) {
  string result;
  TRY_RESULT(entities, do_parse_markdown_v2(text, result));
  text = result;
  return entities;
}

static vector<Slice> find_text_url_entities_v3(Slice text) {
  vector<Slice> result;
  size_t size = text.size();
  for (size_t i = 0; i < size; i++) {
    if (text[i] != '[') {
      continue;
    }

    auto text_begin = i;
    auto text_end = text_begin + 1;
    while (text_end < size && text[text_end] != ']') {
      text_end++;
    }

    i = text_end;  // prevent quadratic asymptotic

    if (text_end == size || text_end == text_begin + 1) {
      continue;
    }

    auto url_begin = text_end + 1;
    if (url_begin == size || text[url_begin] != '(') {
      continue;
    }

    size_t url_end = url_begin + 1;
    while (url_end < size && text[url_end] != ')') {
      url_end++;
    }

    i = url_end;  // prevent quadratic asymptotic, disallows [a](b[c](t.me)

    if (url_end < size) {
      Slice url = text.substr(url_begin + 1, url_end - url_begin - 1);
      if (check_url(url).is_ok()) {
        result.push_back(text.substr(text_begin, text_end - text_begin + 1));
        result.push_back(text.substr(url_begin, url_end - url_begin + 1));
      }
    }
  }
  return result;
}

// entities must be valid for the text
static FormattedText parse_text_url_entities_v3(Slice text, const vector<MessageEntity> &entities) {
  // continuous entities can't intersect TextUrl entities,
  // so try to find new TextUrl entities only between the predetermined continuous entities

  Slice debug_initial_text = text;

  FormattedText result;
  int32 result_text_utf16_length = 0;
  vector<MessageEntity> part_entities;
  vector<MessageEntity> part_splittable_entities[SPLITTABLE_ENTITY_TYPE_COUNT];
  int32 part_begin = 0;
  int32 max_end = 0;
  int32 skipped_length = 0;
  auto add_part = [&](int32 part_end) {
    // we have [part_begin, max_end) kept part and [max_end, part_end) part to parse text_url entities

    if (max_end != part_begin) {
      // add all entities from the kept part
      auto kept_part_text = utf8_utf16_substr(text, 0, max_end - part_begin);
      text = text.substr(kept_part_text.size());

      result.text.append(kept_part_text.begin(), kept_part_text.size());
      append(result.entities, std::move(part_entities));
      part_entities.clear();
      result_text_utf16_length += max_end - part_begin;
    }

    size_t splittable_entity_pos[SPLITTABLE_ENTITY_TYPE_COUNT] = {};
    for (size_t index = 0; index < SPLITTABLE_ENTITY_TYPE_COUNT; index++) {
      check_non_intersecting(part_splittable_entities[index]);
    }
    if (part_end != max_end) {
      // try to find text_url entities in the left part
      auto parsed_part_text = utf8_utf16_substr(text, 0, part_end - max_end);
      text = text.substr(parsed_part_text.size());

      vector<Slice> text_urls = find_text_url_entities_v3(parsed_part_text);

      int32 text_utf16_offset = max_end;
      size_t prev_pos = 0;
      for (size_t i = 0; i < text_urls.size(); i += 2) {
        auto text_begin_pos = static_cast<size_t>(text_urls[i].begin() - parsed_part_text.begin());
        auto text_end_pos = text_begin_pos + text_urls[i].size() - 1;
        auto url_begin_pos = static_cast<size_t>(text_urls[i + 1].begin() - parsed_part_text.begin());
        auto url_end_pos = url_begin_pos + text_urls[i + 1].size() - 1;
        CHECK(parsed_part_text[text_begin_pos] == '[');
        CHECK(parsed_part_text[text_end_pos] == ']');
        CHECK(url_begin_pos == text_end_pos + 1);
        CHECK(parsed_part_text[url_begin_pos] == '(');
        CHECK(parsed_part_text[url_end_pos] == ')');

        Slice before_text_url = parsed_part_text.substr(prev_pos, text_begin_pos - prev_pos);
        auto before_text_url_utf16_length = text_length(before_text_url);
        result_text_utf16_length += before_text_url_utf16_length;
        result.text.append(before_text_url.begin(), before_text_url.size());
        text_utf16_offset += before_text_url_utf16_length;

        Slice text_url = parsed_part_text.substr(text_begin_pos + 1, text_end_pos - text_begin_pos - 1);
        auto text_url_utf16_length = text_length(text_url);
        Slice url = parsed_part_text.substr(url_begin_pos + 1, url_end_pos - url_begin_pos - 1);
        auto url_utf16_length = text_length(url);
        result.entities.emplace_back(MessageEntity::Type::TextUrl, result_text_utf16_length, text_url_utf16_length,
                                     check_url(url).move_as_ok());
        result.text.append(text_url.begin(), text_url.size());
        result_text_utf16_length += text_url_utf16_length;

        auto initial_utf16_length = 1 + text_url_utf16_length + 1 + 1 + url_utf16_length + 1;

        // adjust splittable entities, removing deleted parts from them
        // in the segment [text_utf16_offset, text_utf16_offset + initial_utf16_length)
        // the first character and the last (url_utf16_length + 3) characters are deleted
        for (size_t index = 0; index < SPLITTABLE_ENTITY_TYPE_COUNT; index++) {
          auto &pos = splittable_entity_pos[index];
          auto &splittable_entities = part_splittable_entities[index];
          while (pos < splittable_entities.size() &&
                 splittable_entities[pos].offset < text_utf16_offset + initial_utf16_length) {
            auto offset = splittable_entities[pos].offset;
            auto length = splittable_entities[pos].length;
            if (offset + length > text_utf16_offset + 1 + text_url_utf16_length) {
              // ends after last removed part; truncate length
              length = text_utf16_offset + 1 + text_url_utf16_length - offset;
            }
            if (offset >= text_utf16_offset + 1) {
              offset--;
            } else if (offset + length >= text_utf16_offset + 1) {
              length--;
            }
            if (length > 0) {
              CHECK(offset >= skipped_length);
              CHECK(offset - skipped_length + length <= result_text_utf16_length);
              if (offset < text_utf16_offset && offset + length > text_utf16_offset) {
                // entity intersects start on the new text_url entity; split it
                result.entities.emplace_back(splittable_entities[pos].type, offset - skipped_length,
                                             text_utf16_offset - offset);
                length -= text_utf16_offset - offset;
                offset = text_utf16_offset;
              }
              result.entities.emplace_back(splittable_entities[pos].type, offset - skipped_length, length);
            }
            if (splittable_entities[pos].offset + splittable_entities[pos].length >
                text_utf16_offset + initial_utf16_length) {
              // begins before end of the segment, but ends after it
              // need to keep the entity for future segments, so split the entity
              splittable_entities[pos].length = splittable_entities[pos].offset + splittable_entities[pos].length -
                                                (text_utf16_offset + initial_utf16_length);
              splittable_entities[pos].offset = text_utf16_offset + initial_utf16_length;
            } else {
              pos++;
            }
          }
        }
        text_utf16_offset += initial_utf16_length;

        skipped_length += 2 + 2 + url_utf16_length;
        prev_pos = url_end_pos + 1;
      }

      result.text.append(parsed_part_text.begin() + prev_pos, parsed_part_text.size() - prev_pos);
      result_text_utf16_length += part_end - text_utf16_offset;
    }

    // now add all left splittable entities from [part_begin, part_end)
    for (size_t index = 0; index < SPLITTABLE_ENTITY_TYPE_COUNT; index++) {
      auto &pos = splittable_entity_pos[index];
      auto &splittable_entities = part_splittable_entities[index];
      while (pos < splittable_entities.size() && splittable_entities[pos].offset < part_end) {
        if (splittable_entities[pos].offset + splittable_entities[pos].length > part_end) {
          // begins before end of the segment, but ends after it
          // need to keep the entity for future segments, so split the entity
          // entities don't intersect each other, so there can be at most one such entity
          result.entities.emplace_back(splittable_entities[pos].type, splittable_entities[pos].offset - skipped_length,
                                       part_end - splittable_entities[pos].offset);

          splittable_entities[pos].length =
              splittable_entities[pos].offset + splittable_entities[pos].length - part_end;
          splittable_entities[pos].offset = part_end;
        } else {
          result.entities.emplace_back(splittable_entities[pos].type, splittable_entities[pos].offset - skipped_length,
                                       splittable_entities[pos].length);
          pos++;
        }
      }
      if (pos == splittable_entities.size()) {
        splittable_entities.clear();
      } else {
        CHECK(pos == splittable_entities.size() - 1);
        LOG_CHECK(!text.empty()) << '"' << debug_initial_text << "\" " << entities;
        splittable_entities[0] = std::move(splittable_entities.back());
        splittable_entities.resize(1);
      }
    }

    part_begin = part_end;
  };

  for (const auto &entity : entities) {
    if (is_splittable_entity(entity.type)) {
      auto index = get_splittable_entity_type_index(entity.type);
      part_splittable_entities[index].push_back(entity);
      continue;
    }
    CHECK(is_continuous_entity(entity.type));

    if (entity.offset > max_end) {
      // found a gap from max_end to entity.offset between predetermined entities
      add_part(entity.offset);
    } else {
      CHECK(entity.offset == max_end);
    }

    max_end = entity.offset + entity.length;
    part_entities.push_back(entity);
    part_entities.back().offset -= skipped_length;
  }
  add_part(part_begin + text_length(text));

  return result;
}

static vector<MessageEntity> find_splittable_entities_v3(Slice text, const vector<MessageEntity> &entities) {
  std::unordered_set<size_t> unallowed_boundaries;
  for (auto &entity : entities) {
    unallowed_boundaries.insert(entity.offset);
    unallowed_boundaries.insert(entity.offset + entity.length);
    if (entity.type == MessageEntity::Type::Mention || entity.type == MessageEntity::Type::Hashtag ||
        entity.type == MessageEntity::Type::BotCommand || entity.type == MessageEntity::Type::Cashtag ||
        entity.type == MessageEntity::Type::PhoneNumber || entity.type == MessageEntity::Type::BankCardNumber) {
      for (int32 i = 1; i < entity.length; i++) {
        unallowed_boundaries.insert(entity.offset + i);
      }
    }
  }

  auto found_entities = find_entities(text, false, false);
  td::remove_if(found_entities, [](const auto &entity) {
    return entity.type == MessageEntity::Type::EmailAddress || entity.type == MessageEntity::Type::Url;
  });
  for (auto &entity : found_entities) {
    for (int32 i = 0; i <= entity.length; i++) {
      unallowed_boundaries.insert(entity.offset + i);
    }
  }

  vector<MessageEntity> result;
  int32 splittable_entity_offset[SPLITTABLE_ENTITY_TYPE_COUNT] = {};
  int32 utf16_offset = 0;
  for (size_t i = 0; i + 1 < text.size(); i++) {
    auto c = static_cast<unsigned char>(text[i]);
    if (is_utf8_character_first_code_unit(c)) {
      utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
    }
    if ((c == '_' || c == '*' || c == '~') && text[i] == text[i + 1] && unallowed_boundaries.count(utf16_offset) == 0) {
      auto j = i + 2;
      while (j != text.size() && text[j] == text[i] && unallowed_boundaries.count(utf16_offset + j - i - 1) == 0) {
        j++;
      }
      if (j == i + 2) {
        auto type = c == '_' ? MessageEntity::Type::Italic
                             : (c == '*' ? MessageEntity::Type::Bold : MessageEntity::Type::Strikethrough);
        auto index = get_splittable_entity_type_index(type);
        if (splittable_entity_offset[index] != 0) {
          auto length = utf16_offset - splittable_entity_offset[index] - 1;
          if (length > 0) {
            result.emplace_back(type, splittable_entity_offset[index], length);
          }
          splittable_entity_offset[index] = 0;
        } else {
          splittable_entity_offset[index] = utf16_offset + 1;
        }
      }
      utf16_offset += narrow_cast<int32>(j - i - 1);
      i = j - 1;
    }
  }
  return result;
}

// entities must be valid and can contain only splittable and continuous entities
// __italic__ ~~strikethrough~~ **bold** and [text_url](telegram.org) entities are left to be parsed
static FormattedText parse_markdown_v3_without_pre(Slice text, vector<MessageEntity> entities) {
  check_is_sorted(entities);

  FormattedText parsed_text_url_text;
  if (text.find('[') != string::npos) {
    parsed_text_url_text = parse_text_url_entities_v3(text, entities);
    text = parsed_text_url_text.text;
    entities = std::move(parsed_text_url_text.entities);
  }
  // splittable entities are sorted only within a fixed type now

  bool have_splittable_entities = false;
  for (size_t i = 0; i + 1 < text.size(); i++) {
    if ((text[i] == '_' || text[i] == '*' || text[i] == '~') && text[i] == text[i + 1]) {
      have_splittable_entities = true;
      break;
    }
  }
  if (!have_splittable_entities) {
    // fast path
    sort_entities(entities);
    return {text.str(), std::move(entities)};
  }

  auto found_splittable_entities = find_splittable_entities_v3(text, entities);
  vector<int32> removed_pos;
  for (auto &entity : found_splittable_entities) {
    removed_pos.push_back(entity.offset - 1);
    removed_pos.push_back(entity.offset + entity.length + 1);
  }
  std::sort(removed_pos.begin(), removed_pos.end());

  string new_text;
  CHECK(text.size() >= 2 * removed_pos.size());
  new_text.reserve(text.size() - 2 * removed_pos.size());
  size_t j = 0;
  int32 utf16_offset = 0;
  for (size_t i = 0; i < text.size(); i++) {
    auto c = static_cast<unsigned char>(text[i]);
    if (is_utf8_character_first_code_unit(c)) {
      utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
    }
    if (j < removed_pos.size() && utf16_offset == removed_pos[j]) {
      i++;
      utf16_offset++;
      CHECK(j + 1 == removed_pos.size() || removed_pos[j + 1] >= removed_pos[j] + 2);
      j++;
    } else {
      new_text += text[i];
    }
  }
  CHECK(j == removed_pos.size());
  combine(entities, std::move(found_splittable_entities));
  for (auto &entity : entities) {
    auto removed_before_begin = narrow_cast<int32>(
        std::upper_bound(removed_pos.begin(), removed_pos.end(), entity.offset) - removed_pos.begin());
    auto removed_before_end = narrow_cast<int32>(
        std::upper_bound(removed_pos.begin(), removed_pos.end(), entity.offset + entity.length) - removed_pos.begin());
    entity.length -= 2 * (removed_before_end - removed_before_begin);
    entity.offset -= 2 * removed_before_begin;
    CHECK(entity.offset >= 0);
    CHECK(entity.length >= 0);
    CHECK(entity.offset + entity.length <= utf16_offset);
  }

  td::remove_if(entities, [](const auto &entity) { return entity.length == 0; });

  sort_entities(entities);
  return {std::move(new_text), std::move(entities)};
}

static FormattedText parse_pre_entities_v3(Slice text) {
  string result;
  vector<MessageEntity> entities;
  size_t size = text.size();
  int32 utf16_offset = 0;
  for (size_t i = 0; i < size; i++) {
    auto c = static_cast<unsigned char>(text[i]);
    if (c != '`') {
      if (is_utf8_character_first_code_unit(c)) {
        utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
      }
      result.push_back(text[i]);
      continue;
    }

    size_t j = i + 1;
    while (j < size && text[j] == '`') {
      j++;
    }

    if (j - i == 1 || j - i == 3) {
      // trying to find end of the entity
      int32 entity_length = 0;
      bool is_found = false;
      for (size_t end_tag_begin = j; end_tag_begin < size; end_tag_begin++) {
        auto cur_c = static_cast<unsigned char>(text[end_tag_begin]);
        if (cur_c == '`') {
          // possible end tag
          size_t end_tag_end = end_tag_begin + 1;
          while (end_tag_end < size && text[end_tag_end] == '`') {
            end_tag_end++;
          }
          if (end_tag_end - end_tag_begin == j - i) {
            // end tag found
            CHECK(entity_length > 0);
            entities.emplace_back(j - i == 3 ? MessageEntity::Type::Pre : MessageEntity::Type::Code, utf16_offset,
                                  entity_length);
            result.append(text.begin() + j, end_tag_begin - j);
            utf16_offset += entity_length;
            i = end_tag_end - 1;
            is_found = true;
            break;
          } else {
            // not an end tag, skip
            entity_length += narrow_cast<int32>(end_tag_end - end_tag_begin);
            end_tag_begin = end_tag_end - 1;
          }
        } else if (is_utf8_character_first_code_unit(cur_c)) {
          entity_length += 1 + (cur_c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
        }
      }
      if (is_found) {
        continue;
      }
    }

    result.append(text.begin() + i, j - i);
    utf16_offset += narrow_cast<int32>(j - i);
    i = j - 1;
  }
  return {std::move(result), std::move(entities)};
}

// entities must be valid for the text
static FormattedText parse_pre_entities_v3(Slice text, vector<MessageEntity> entities) {
  // nothing can intersect pre entities, so ignore all '`' inside the predetermined entities
  // and try to find new pre entities only between the predetermined entities

  FormattedText result;
  int32 result_text_utf16_length = 0;
  int32 part_begin = 0;
  int32 max_end = 0;
  int32 skipped_length = 0;

  auto add_part = [&](int32 part_end) {
    // we have [part_begin, max_end) kept part and [max_end, part_end) part to parse pre entities
    CHECK(part_begin == result_text_utf16_length + skipped_length);

    if (max_end != part_begin) {
      // add the kept part
      auto kept_part_text = utf8_utf16_substr(text, 0, max_end - part_begin);
      text = text.substr(kept_part_text.size());

      result.text.append(kept_part_text.begin(), kept_part_text.size());
      result_text_utf16_length += max_end - part_begin;
    }

    if (part_end != max_end) {
      // try to find pre entities in the left part
      auto parsed_part_text = utf8_utf16_substr(text, 0, part_end - max_end);
      text = text.substr(parsed_part_text.size());

      if (parsed_part_text.find('`') == string::npos) {
        // fast path, no pre entities; just append the text
        result.text.append(parsed_part_text.begin(), parsed_part_text.size());
        result_text_utf16_length += part_end - max_end;
      } else {
        FormattedText parsed_text = parse_pre_entities_v3(parsed_part_text);
        int32 new_skipped_length = 0;
        for (auto &entity : parsed_text.entities) {
          new_skipped_length += (entity.type == MessageEntity::Type::Pre ? 6 : 2);
        }
        CHECK(new_skipped_length < part_end - max_end);
        result.text += parsed_text.text;
        for (auto &entity : parsed_text.entities) {
          entity.offset += result_text_utf16_length;
        }
        append(result.entities, std::move(parsed_text.entities));
        result_text_utf16_length += part_end - max_end - new_skipped_length;
        skipped_length += new_skipped_length;
      }
    }

    part_begin = part_end;
  };

  for (auto &entity : entities) {
    if (entity.offset > max_end) {
      // found a gap from max_end to entity.offset between predetermined entities
      add_part(entity.offset);
    }

    max_end = td::max(max_end, entity.offset + entity.length);
    result.entities.push_back(std::move(entity));
    result.entities.back().offset -= skipped_length;
  }
  add_part(part_begin + text_length(text));

  return result;
}

// text entities must be valid
// returned entities must be resplitted and fixed
FormattedText parse_markdown_v3(FormattedText text) {
  if (text.text.find('`') != string::npos) {
    text = parse_pre_entities_v3(text.text, std::move(text.entities));
    check_is_sorted(text.entities);
  }

  bool have_pre = false;
  for (auto &entity : text.entities) {
    if (is_pre_entity(entity.type)) {
      have_pre = true;
      break;
    }
  }
  if (!have_pre) {
    // fast path
    return parse_markdown_v3_without_pre(text.text, std::move(text.entities));
  }

  FormattedText result;
  int32 result_text_utf16_length = 0;
  vector<MessageEntity> part_entities;
  int32 part_begin = 0;
  int32 max_end = 0;
  Slice left_text = text.text;

  auto add_part = [&](int32 part_end) {
    auto part_text = utf8_utf16_substr(left_text, 0, part_end - part_begin);
    left_text = left_text.substr(part_text.size());

    FormattedText part = parse_markdown_v3_without_pre(part_text, std::move(part_entities));
    part_entities.clear();

    result.text += part.text;
    for (auto &entity : part.entities) {
      entity.offset += result_text_utf16_length;
    }
    append(result.entities, std::move(part.entities));
    result_text_utf16_length += text_length(part.text);
    part_begin = part_end;
  };

  for (size_t i = 0; i < text.entities.size(); i++) {
    auto &entity = text.entities[i];
    CHECK(is_splittable_entity(entity.type) || is_pre_entity(entity.type) || is_continuous_entity(entity.type));
    if (is_pre_entity(entity.type)) {
      CHECK(entity.offset >= max_end);
      CHECK(i + 1 == text.entities.size() || text.entities[i + 1].offset >= entity.offset + entity.length);

      add_part(entity.offset);

      auto part_text = utf8_utf16_substr(left_text, 0, entity.length);
      left_text = left_text.substr(part_text.size());

      result.text.append(part_text.begin(), part_text.size());
      result.entities.push_back(entity);
      result.entities.back().offset = result_text_utf16_length;
      result_text_utf16_length += entity.length;
      part_begin = entity.offset + entity.length;
    } else {
      part_entities.push_back(entity);
      part_entities.back().offset -= part_begin;
    }

    max_end = td::max(max_end, entity.offset + entity.length);
  }
  add_part(part_begin + text_length(left_text));

  return result;
}

// text entities must be valid
FormattedText get_markdown_v3(FormattedText text) {
  if (text.entities.empty()) {
    return text;
  }

  check_is_sorted(text.entities);
  for (auto &entity : text.entities) {
    if (!is_user_entity(entity.type)) {
      return text;
    }
  }

  FormattedText result;
  struct EntityInfo {
    const MessageEntity *entity;
    int32 utf16_added_before;

    EntityInfo(MessageEntity *entity, int32 utf16_added_before)
        : entity(entity), utf16_added_before(utf16_added_before) {
    }
  };
  vector<EntityInfo> nested_entities_stack;
  size_t current_entity = 0;

  int32 utf16_offset = 0;
  int32 utf16_added = 0;

  for (size_t pos = 0; pos <= text.text.size(); pos++) {
    auto c = static_cast<unsigned char>(text.text[pos]);
    if (is_utf8_character_first_code_unit(c)) {
      while (!nested_entities_stack.empty()) {
        const auto *entity = nested_entities_stack.back().entity;
        auto entity_end = entity->offset + entity->length;
        if (utf16_offset < entity_end) {
          break;
        }

        CHECK(utf16_offset == entity_end);

        switch (entity->type) {
          case MessageEntity::Type::Italic:
            result.text += "__";
            utf16_added += 2;
            break;
          case MessageEntity::Type::Bold:
            result.text += "**";
            utf16_added += 2;
            break;
          case MessageEntity::Type::Strikethrough:
            result.text += "~~";
            utf16_added += 2;
            break;
          case MessageEntity::Type::TextUrl:
            result.text += "](";
            result.text += entity->argument;
            result.text += ')';
            utf16_added += narrow_cast<int32>(3 + entity->argument.size());
            break;
          case MessageEntity::Type::Code:
            result.text += '`';
            utf16_added++;
            break;
          case MessageEntity::Type::Pre:
            result.text += "```";
            utf16_added += 3;
            break;
          default:
            result.entities.push_back(*entity);
            result.entities.back().offset += nested_entities_stack.back().utf16_added_before;
            result.entities.back().length += utf16_added - nested_entities_stack.back().utf16_added_before;
            break;
        }
        nested_entities_stack.pop_back();
      }

      while (current_entity < text.entities.size() && utf16_offset >= text.entities[current_entity].offset) {
        CHECK(utf16_offset == text.entities[current_entity].offset);
        switch (text.entities[current_entity].type) {
          case MessageEntity::Type::Italic:
            result.text += "__";
            utf16_added += 2;
            break;
          case MessageEntity::Type::Bold:
            result.text += "**";
            utf16_added += 2;
            break;
          case MessageEntity::Type::Strikethrough:
            result.text += "~~";
            utf16_added += 2;
            break;
          case MessageEntity::Type::TextUrl:
            result.text += '[';
            utf16_added++;
            break;
          case MessageEntity::Type::Code:
            result.text += '`';
            utf16_added++;
            break;
          case MessageEntity::Type::Pre:
            result.text += "```";
            utf16_added += 3;
            break;
          default:
            // keep as is
            break;
        }
        nested_entities_stack.emplace_back(&text.entities[current_entity++], utf16_added);
      }
      utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
    }
    if (pos == text.text.size()) {
      break;
    }

    result.text.push_back(text.text[pos]);
  }

  sort_entities(result.entities);
  if (parse_markdown_v3(result) != text) {
    return text;
  }
  return result;
}

static uint32 decode_html_entity(CSlice text, size_t &pos) {
  auto c = static_cast<unsigned char>(text[pos]);
  if (c != '&') {
    return 0;
  }

  size_t end_pos = pos + 1;
  uint32 res = 0;
  if (text[pos + 1] == '#') {
    // numeric character reference
    end_pos++;
    if (text[pos + 2] == 'x') {
      // hexadecimal numeric character reference
      end_pos++;
      while (is_hex_digit(text[end_pos])) {
        res = res * 16 + hex_to_int(text[end_pos++]);
      }
    } else {
      // decimal numeric character reference
      while (is_digit(text[end_pos])) {
        res = res * 10 + text[end_pos++] - '0';
      }
    }
    if (res == 0 || res >= 0x10ffff || end_pos - pos >= 10) {
      return 0;
    }
  } else {
    while (is_alpha(text[end_pos])) {
      end_pos++;
    }
    Slice entity = text.substr(pos + 1, end_pos - pos - 1);
    if (entity == Slice("lt")) {
      res = static_cast<uint32>('<');
    } else if (entity == Slice("gt")) {
      res = static_cast<uint32>('>');
    } else if (entity == Slice("amp")) {
      res = static_cast<uint32>('&');
    } else if (entity == Slice("quot")) {
      res = static_cast<uint32>('"');
    } else {
      // unsupported literal entity
      return 0;
    }
  }

  if (text[end_pos] == ';') {
    pos = end_pos + 1;
  } else {
    pos = end_pos;
  }
  return res;
}

static Result<vector<MessageEntity>> do_parse_html(CSlice text, string &result) {
  vector<MessageEntity> entities;
  int32 utf16_offset = 0;

  struct EntityInfo {
    string tag_name;
    string argument;
    int32 entity_offset;
    size_t entity_begin_pos;

    EntityInfo(string tag_name, string argument, int32 entity_offset, size_t entity_begin_pos)
        : tag_name(std::move(tag_name))
        , argument(std::move(argument))
        , entity_offset(entity_offset)
        , entity_begin_pos(entity_begin_pos) {
    }
  };
  std::vector<EntityInfo> nested_entities;

  for (size_t i = 0; i < text.size(); i++) {
    auto c = static_cast<unsigned char>(text[i]);
    if (c == '&') {
      auto ch = decode_html_entity(text, i);
      if (ch != 0) {
        i--;  // i will be incremented in for
        utf16_offset += 1 + (ch > 0xffff);
        append_utf8_character(result, ch);
        continue;
      }
    }
    if (c != '<') {
      if (is_utf8_character_first_code_unit(c)) {
        utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
      }
      result.push_back(text[i]);
      continue;
    }

    auto begin_pos = i++;
    if (text[i] != '/') {
      // begin of an entity
      while (!is_space(text[i]) && text[i] != '>') {
        i++;
      }
      if (text[i] == 0) {
        return Status::Error(400, PSLICE() << "Unclosed start tag at byte offset " << begin_pos);
      }

      string tag_name = to_lower(text.substr(begin_pos + 1, i - begin_pos - 1));
      if (tag_name != "a" && tag_name != "b" && tag_name != "strong" && tag_name != "i" && tag_name != "em" &&
          tag_name != "s" && tag_name != "strike" && tag_name != "del" && tag_name != "u" && tag_name != "ins" &&
          tag_name != "pre" && tag_name != "code") {
        return Status::Error(400, PSLICE()
                                      << "Unsupported start tag \"" << tag_name << "\" at byte offset " << begin_pos);
      }

      string argument;
      while (text[i] != '>') {
        while (text[i] != 0 && is_space(text[i])) {
          i++;
        }
        if (text[i] == '>') {
          break;
        }
        auto attribute_begin_pos = i;
        while (!is_space(text[i]) && text[i] != '=') {
          i++;
        }
        Slice attribute_name = text.substr(attribute_begin_pos, i - attribute_begin_pos);
        if (attribute_name.empty()) {
          return Status::Error(
              400, PSLICE() << "Empty attribute name in the tag \"" << tag_name << "\" at byte offset " << begin_pos);
        }
        while (text[i] != 0 && is_space(text[i])) {
          i++;
        }
        if (text[i] != '=') {
          return Status::Error(400, PSLICE() << "Expected equal sign in declaration of an attribute of the tag \""
                                             << tag_name << "\" at byte offset " << begin_pos);
        }
        i++;
        while (text[i] != 0 && is_space(text[i])) {
          i++;
        }
        if (text[i] == 0) {
          return Status::Error(400, PSLICE()
                                        << "Unclosed start tag \"" << tag_name << "\" at byte offset " << begin_pos);
        }

        string attribute_value;
        if (text[i] != '\'' && text[i] != '"') {
          // A name token (a sequence of letters, digits, periods, or hyphens). Name tokens are not case sensitive.
          auto token_begin_pos = i;
          while (is_alnum(text[i]) || text[i] == '.' || text[i] == '-') {
            i++;
          }
          attribute_value = to_lower(text.substr(token_begin_pos, i - token_begin_pos));

          if (!is_space(text[i]) && text[i] != '>') {
            return Status::Error(400, PSLICE() << "Unexpected end of name token at byte offset " << token_begin_pos);
          }
        } else {
          // A string literal
          char end_character = text[i++];
          while (text[i] != end_character && text[i] != 0) {
            if (text[i] == '&') {
              auto ch = decode_html_entity(text, i);
              if (ch != 0) {
                append_utf8_character(attribute_value, ch);
                continue;
              }
            }
            attribute_value.push_back(text[i++]);
          }
          if (text[i] == end_character) {
            i++;
          }
        }
        if (text[i] == 0) {
          return Status::Error(400, PSLICE() << "Unclosed start tag at byte offset " << begin_pos);
        }

        if (tag_name == "a" && attribute_name == Slice("href")) {
          argument = std::move(attribute_value);
        }
        if (tag_name == "code" && attribute_name == Slice("class") && begins_with(attribute_value, "language-")) {
          argument = attribute_value.substr(9);
        }
      }

      nested_entities.emplace_back(std::move(tag_name), std::move(argument), utf16_offset, result.size());
    } else {
      // end of an entity
      if (nested_entities.empty()) {
        return Status::Error(400, PSLICE() << "Unexpected end tag at byte offset " << begin_pos);
      }

      while (!is_space(text[i]) && text[i] != '>') {
        i++;
      }
      Slice end_tag_name = text.substr(begin_pos + 2, i - begin_pos - 2);
      while (is_space(text[i]) && text[i] != 0) {
        i++;
      }
      if (text[i] != '>') {
        return Status::Error(400, PSLICE() << "Unclosed end tag at byte offset " << begin_pos);
      }

      string tag_name = std::move(nested_entities.back().tag_name);
      if (!end_tag_name.empty() && end_tag_name != tag_name) {
        return Status::Error(400, PSLICE() << "Unmatched end tag at byte offset " << begin_pos << ", expected \"</"
                                           << tag_name << ">\", found \"</" << end_tag_name << ">\"");
      }

      if (utf16_offset > nested_entities.back().entity_offset) {
        auto entity_offset = nested_entities.back().entity_offset;
        auto entity_length = utf16_offset - entity_offset;
        if (tag_name == "i" || tag_name == "em") {
          entities.emplace_back(MessageEntity::Type::Italic, entity_offset, entity_length);
        } else if (tag_name == "b" || tag_name == "strong") {
          entities.emplace_back(MessageEntity::Type::Bold, entity_offset, entity_length);
        } else if (tag_name == "s" || tag_name == "strike" || tag_name == "del") {
          entities.emplace_back(MessageEntity::Type::Strikethrough, entity_offset, entity_length);
        } else if (tag_name == "u" || tag_name == "ins") {
          entities.emplace_back(MessageEntity::Type::Underline, entity_offset, entity_length);
        } else if (tag_name == "a") {
          auto url = std::move(nested_entities.back().argument);
          if (url.empty()) {
            url = result.substr(nested_entities.back().entity_begin_pos);
          }
          auto user_id = get_link_user_id(url);
          if (user_id.is_valid()) {
            entities.emplace_back(entity_offset, entity_length, user_id);
          } else {
            auto r_url = check_url(url);
            if (r_url.is_ok()) {
              entities.emplace_back(MessageEntity::Type::TextUrl, entity_offset, entity_length, r_url.move_as_ok());
            }
          }
        } else if (tag_name == "pre") {
          if (!entities.empty() && entities.back().type == MessageEntity::Type::Code &&
              entities.back().offset == entity_offset && entities.back().length == entity_length &&
              !entities.back().argument.empty()) {
            entities.back().type = MessageEntity::Type::PreCode;
          } else {
            entities.emplace_back(MessageEntity::Type::Pre, entity_offset, entity_length);
          }
        } else if (tag_name == "code") {
          if (!entities.empty() && entities.back().type == MessageEntity::Type::Pre &&
              entities.back().offset == entity_offset && entities.back().length == entity_length &&
              !nested_entities.back().argument.empty()) {
            entities.back().type = MessageEntity::Type::PreCode;
            entities.back().argument = std::move(nested_entities.back().argument);
          } else {
            entities.emplace_back(MessageEntity::Type::Code, entity_offset, entity_length,
                                  nested_entities.back().argument);
          }
        } else {
          UNREACHABLE();
        }
      }
      nested_entities.pop_back();
    }
  }
  if (!nested_entities.empty()) {
    return Status::Error(
        400, PSLICE() << "Can't find end tag corresponding to start tag " << nested_entities.back().tag_name);
  }

  for (auto &entity : entities) {
    if (entity.type == MessageEntity::Type::Code && !entity.argument.empty()) {
      entity.argument.clear();
    }
  }

  sort_entities(entities);

  return entities;
}

Result<vector<MessageEntity>> parse_html(string &text) {
  string result;
  TRY_RESULT(entities, do_parse_html(text, result));
  if (!check_utf8(result)) {
    return Status::Error(400,
                         "Text contains invalid Unicode characters after decoding HTML entities, check for unmatched "
                         "surrogate code units");
  }
  text = result;
  return entities;
}

vector<tl_object_ptr<telegram_api::MessageEntity>> get_input_message_entities(const ContactsManager *contacts_manager,
                                                                              const vector<MessageEntity> &entities,
                                                                              const char *source) {
  vector<tl_object_ptr<telegram_api::MessageEntity>> result;
  for (auto &entity : entities) {
    if (!is_user_entity(entity.type)) {
      continue;
    }
    switch (entity.type) {
      case MessageEntity::Type::Bold:
        result.push_back(make_tl_object<telegram_api::messageEntityBold>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Italic:
        result.push_back(make_tl_object<telegram_api::messageEntityItalic>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Underline:
        result.push_back(make_tl_object<telegram_api::messageEntityUnderline>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Strikethrough:
        result.push_back(make_tl_object<telegram_api::messageEntityStrike>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::BlockQuote:
        result.push_back(make_tl_object<telegram_api::messageEntityBlockquote>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Code:
        result.push_back(make_tl_object<telegram_api::messageEntityCode>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Pre:
        result.push_back(make_tl_object<telegram_api::messageEntityPre>(entity.offset, entity.length, string()));
        break;
      case MessageEntity::Type::PreCode:
        result.push_back(make_tl_object<telegram_api::messageEntityPre>(entity.offset, entity.length, entity.argument));
        break;
      case MessageEntity::Type::TextUrl:
        result.push_back(
            make_tl_object<telegram_api::messageEntityTextUrl>(entity.offset, entity.length, entity.argument));
        break;
      case MessageEntity::Type::MentionName: {
        auto input_user = contacts_manager->get_input_user(entity.user_id);
        LOG_CHECK(input_user != nullptr) << source;
        result.push_back(make_tl_object<telegram_api::inputMessageEntityMentionName>(entity.offset, entity.length,
                                                                                     std::move(input_user)));
        break;
      }
      case MessageEntity::Type::Mention:
      case MessageEntity::Type::Hashtag:
      case MessageEntity::Type::BotCommand:
      case MessageEntity::Type::Url:
      case MessageEntity::Type::EmailAddress:
      case MessageEntity::Type::Cashtag:
      case MessageEntity::Type::PhoneNumber:
      case MessageEntity::Type::BankCardNumber:
      default:
        UNREACHABLE();
    }
  }

  return result;
}

vector<tl_object_ptr<telegram_api::MessageEntity>> get_input_message_entities(const ContactsManager *contacts_manager,
                                                                              const FormattedText *text,
                                                                              const char *source) {
  if (text != nullptr && !text->entities.empty()) {
    return get_input_message_entities(contacts_manager, text->entities, source);
  }
  return {};
}

vector<tl_object_ptr<secret_api::MessageEntity>> get_input_secret_message_entities(
    const vector<MessageEntity> &entities, int32 layer) {
  vector<tl_object_ptr<secret_api::MessageEntity>> result;
  for (auto &entity : entities) {
    switch (entity.type) {
      case MessageEntity::Type::Mention:
        result.push_back(make_tl_object<secret_api::messageEntityMention>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Hashtag:
        result.push_back(make_tl_object<secret_api::messageEntityHashtag>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Cashtag:
        break;
      case MessageEntity::Type::BotCommand:
        break;
      case MessageEntity::Type::PhoneNumber:
        break;
      case MessageEntity::Type::BankCardNumber:
        break;
      case MessageEntity::Type::Url:
        result.push_back(make_tl_object<secret_api::messageEntityUrl>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::EmailAddress:
        result.push_back(make_tl_object<secret_api::messageEntityEmail>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Bold:
        result.push_back(make_tl_object<secret_api::messageEntityBold>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Italic:
        result.push_back(make_tl_object<secret_api::messageEntityItalic>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Underline:
        if (layer >= SecretChatActor::NEW_ENTITIES_LAYER) {
          result.push_back(make_tl_object<secret_api::messageEntityUnderline>(entity.offset, entity.length));
        }
        break;
      case MessageEntity::Type::Strikethrough:
        if (layer >= SecretChatActor::NEW_ENTITIES_LAYER) {
          result.push_back(make_tl_object<secret_api::messageEntityStrike>(entity.offset, entity.length));
        }
        break;
      case MessageEntity::Type::BlockQuote:
        if (layer >= SecretChatActor::NEW_ENTITIES_LAYER) {
          result.push_back(make_tl_object<secret_api::messageEntityBlockquote>(entity.offset, entity.length));
        }
        break;
      case MessageEntity::Type::Code:
        result.push_back(make_tl_object<secret_api::messageEntityCode>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Pre:
        result.push_back(make_tl_object<secret_api::messageEntityPre>(entity.offset, entity.length, string()));
        break;
      case MessageEntity::Type::PreCode:
        result.push_back(make_tl_object<secret_api::messageEntityPre>(entity.offset, entity.length, entity.argument));
        break;
      case MessageEntity::Type::TextUrl:
        result.push_back(
            make_tl_object<secret_api::messageEntityTextUrl>(entity.offset, entity.length, entity.argument));
        break;
      case MessageEntity::Type::MentionName:
        break;
      default:
        UNREACHABLE();
    }
  }

  return result;
}

Result<vector<MessageEntity>> get_message_entities(const ContactsManager *contacts_manager,
                                                   vector<tl_object_ptr<td_api::textEntity>> &&input_entities,
                                                   bool allow_all) {
  vector<MessageEntity> entities;
  for (auto &entity : input_entities) {
    if (entity == nullptr || entity->type_ == nullptr) {
      continue;
    }

    switch (entity->type_->get_id()) {
      case td_api::textEntityTypeMention::ID:
        entities.emplace_back(MessageEntity::Type::Mention, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeHashtag::ID:
        entities.emplace_back(MessageEntity::Type::Hashtag, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeBotCommand::ID:
        entities.emplace_back(MessageEntity::Type::BotCommand, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeUrl::ID:
        entities.emplace_back(MessageEntity::Type::Url, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeEmailAddress::ID:
        entities.emplace_back(MessageEntity::Type::EmailAddress, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeCashtag::ID:
        entities.emplace_back(MessageEntity::Type::Cashtag, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypePhoneNumber::ID:
        entities.emplace_back(MessageEntity::Type::PhoneNumber, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeBankCardNumber::ID:
        entities.emplace_back(MessageEntity::Type::BankCardNumber, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeBold::ID:
        entities.emplace_back(MessageEntity::Type::Bold, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeItalic::ID:
        entities.emplace_back(MessageEntity::Type::Italic, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeUnderline::ID:
        entities.emplace_back(MessageEntity::Type::Underline, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeStrikethrough::ID:
        entities.emplace_back(MessageEntity::Type::Strikethrough, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeCode::ID:
        entities.emplace_back(MessageEntity::Type::Code, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypePre::ID:
        entities.emplace_back(MessageEntity::Type::Pre, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypePreCode::ID: {
        auto entity_pre_code = static_cast<td_api::textEntityTypePreCode *>(entity->type_.get());
        if (!clean_input_string(entity_pre_code->language_)) {
          return Status::Error(400, "MessageEntityPreCode.language must be encoded in UTF-8");
        }
        entities.emplace_back(MessageEntity::Type::PreCode, entity->offset_, entity->length_,
                              entity_pre_code->language_);
        break;
      }
      case td_api::textEntityTypeTextUrl::ID: {
        auto entity_text_url = static_cast<td_api::textEntityTypeTextUrl *>(entity->type_.get());
        if (!clean_input_string(entity_text_url->url_)) {
          return Status::Error(400, "MessageEntityTextUrl.url must be encoded in UTF-8");
        }
        auto r_url = check_url(entity_text_url->url_);
        if (r_url.is_error()) {
          return Status::Error(400, PSTRING() << "Wrong message entity: " << r_url.error().message());
        }
        entities.emplace_back(MessageEntity::Type::TextUrl, entity->offset_, entity->length_, r_url.move_as_ok());
        break;
      }
      case td_api::textEntityTypeMentionName::ID: {
        auto entity_mention_name = static_cast<td_api::textEntityTypeMentionName *>(entity->type_.get());
        UserId user_id(entity_mention_name->user_id_);
        if (contacts_manager != nullptr && !contacts_manager->have_input_user(user_id)) {
          return Status::Error(7, "Have no access to the user");
        }
        entities.emplace_back(entity->offset_, entity->length_, user_id);
        break;
      }
      default:
        UNREACHABLE();
    }
    CHECK(!entities.empty());
    if (!allow_all && !is_user_entity(entities.back().type)) {
      entities.pop_back();
    }
  }
  return entities;
}

vector<MessageEntity> get_message_entities(const ContactsManager *contacts_manager,
                                           vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities,
                                           const char *source) {
  vector<MessageEntity> entities;
  entities.reserve(server_entities.size());
  for (auto &entity : server_entities) {
    switch (entity->get_id()) {
      case telegram_api::messageEntityUnknown::ID:
        break;
      case telegram_api::messageEntityMention::ID: {
        auto entity_mention = static_cast<const telegram_api::messageEntityMention *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Mention, entity_mention->offset_, entity_mention->length_);
        break;
      }
      case telegram_api::messageEntityHashtag::ID: {
        auto entity_hashtag = static_cast<const telegram_api::messageEntityHashtag *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Hashtag, entity_hashtag->offset_, entity_hashtag->length_);
        break;
      }
      case telegram_api::messageEntityCashtag::ID: {
        auto entity_cashtag = static_cast<const telegram_api::messageEntityCashtag *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Cashtag, entity_cashtag->offset_, entity_cashtag->length_);
        break;
      }
      case telegram_api::messageEntityPhone::ID: {
        auto entity_phone = static_cast<const telegram_api::messageEntityPhone *>(entity.get());
        entities.emplace_back(MessageEntity::Type::PhoneNumber, entity_phone->offset_, entity_phone->length_);
        break;
      }
      case telegram_api::messageEntityBotCommand::ID: {
        auto entity_bot_command = static_cast<const telegram_api::messageEntityBotCommand *>(entity.get());
        entities.emplace_back(MessageEntity::Type::BotCommand, entity_bot_command->offset_,
                              entity_bot_command->length_);
        break;
      }
      case telegram_api::messageEntityBankCard::ID: {
        auto entity_bank_card = static_cast<const telegram_api::messageEntityBankCard *>(entity.get());
        entities.emplace_back(MessageEntity::Type::BankCardNumber, entity_bank_card->offset_,
                              entity_bank_card->length_);
        break;
      }
      case telegram_api::messageEntityUrl::ID: {
        auto entity_url = static_cast<const telegram_api::messageEntityUrl *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Url, entity_url->offset_, entity_url->length_);
        break;
      }
      case telegram_api::messageEntityEmail::ID: {
        auto entity_email = static_cast<const telegram_api::messageEntityEmail *>(entity.get());
        entities.emplace_back(MessageEntity::Type::EmailAddress, entity_email->offset_, entity_email->length_);
        break;
      }
      case telegram_api::messageEntityBold::ID: {
        auto entity_bold = static_cast<const telegram_api::messageEntityBold *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Bold, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case telegram_api::messageEntityItalic::ID: {
        auto entity_italic = static_cast<const telegram_api::messageEntityItalic *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Italic, entity_italic->offset_, entity_italic->length_);
        break;
      }
      case telegram_api::messageEntityUnderline::ID: {
        auto entity_bold = static_cast<const telegram_api::messageEntityUnderline *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Underline, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case telegram_api::messageEntityStrike::ID: {
        auto entity_bold = static_cast<const telegram_api::messageEntityStrike *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Strikethrough, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case telegram_api::messageEntityBlockquote::ID: {
        auto entity_bold = static_cast<const telegram_api::messageEntityBlockquote *>(entity.get());
        entities.emplace_back(MessageEntity::Type::BlockQuote, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case telegram_api::messageEntityCode::ID: {
        auto entity_code = static_cast<const telegram_api::messageEntityCode *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Code, entity_code->offset_, entity_code->length_);
        break;
      }
      case telegram_api::messageEntityPre::ID: {
        auto entity_pre = static_cast<telegram_api::messageEntityPre *>(entity.get());
        if (entity_pre->language_.empty()) {
          entities.emplace_back(MessageEntity::Type::Pre, entity_pre->offset_, entity_pre->length_);
        } else {
          entities.emplace_back(MessageEntity::Type::PreCode, entity_pre->offset_, entity_pre->length_,
                                std::move(entity_pre->language_));
        }
        break;
      }
      case telegram_api::messageEntityTextUrl::ID: {
        auto entity_text_url = static_cast<const telegram_api::messageEntityTextUrl *>(entity.get());
        auto r_url = check_url(entity_text_url->url_);
        if (r_url.is_error()) {
          LOG(ERROR) << "Wrong URL entity: \"" << entity_text_url->url_ << "\": " << r_url.error().message() << " from "
                     << source;
          continue;
        }
        entities.emplace_back(MessageEntity::Type::TextUrl, entity_text_url->offset_, entity_text_url->length_,
                              r_url.move_as_ok());
        break;
      }
      case telegram_api::messageEntityMentionName::ID: {
        auto entity_mention_name = static_cast<const telegram_api::messageEntityMentionName *>(entity.get());
        UserId user_id(entity_mention_name->user_id_);
        if (!user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << user_id << " in MentionName from " << source;
          continue;
        }
        if (contacts_manager == nullptr || !contacts_manager->have_user(user_id)) {
          LOG(ERROR) << "Receive unknown " << user_id << " in MentionName from " << source;
          continue;
        }
        if (!contacts_manager->have_input_user(user_id)) {
          LOG(ERROR) << "Receive inaccessible " << user_id << " in MentionName from " << source;
          continue;
        }
        entities.emplace_back(entity_mention_name->offset_, entity_mention_name->length_, user_id);
        break;
      }
      default:
        UNREACHABLE();
    }
  }
  return entities;
}

vector<MessageEntity> get_message_entities(vector<tl_object_ptr<secret_api::MessageEntity>> &&secret_entities) {
  vector<MessageEntity> entities;
  entities.reserve(secret_entities.size());
  for (auto &entity : secret_entities) {
    switch (entity->get_id()) {
      case secret_api::messageEntityUnknown::ID:
        break;
      case secret_api::messageEntityMention::ID:
        // skip, will find it ourselves
        break;
      case secret_api::messageEntityHashtag::ID:
        // skip, will find it ourselves
        break;
      case secret_api::messageEntityCashtag::ID:
        // skip, will find it ourselves
        break;
      case secret_api::messageEntityPhone::ID:
        // skip, will find it ourselves
        break;
      case secret_api::messageEntityBotCommand::ID:
        // skip all bot commands in secret chats
        break;
      case secret_api::messageEntityBankCard::ID:
        // skip, will find it ourselves
        break;
      case secret_api::messageEntityUrl::ID: {
        auto entity_url = static_cast<const secret_api::messageEntityUrl *>(entity.get());
        // TODO skip URL when find_urls will be better
        entities.emplace_back(MessageEntity::Type::Url, entity_url->offset_, entity_url->length_);
        break;
      }
      case secret_api::messageEntityEmail::ID: {
        auto entity_email = static_cast<const secret_api::messageEntityEmail *>(entity.get());
        // TODO skip emails when find_urls will be better
        entities.emplace_back(MessageEntity::Type::EmailAddress, entity_email->offset_, entity_email->length_);
        break;
      }
      case secret_api::messageEntityBold::ID: {
        auto entity_bold = static_cast<const secret_api::messageEntityBold *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Bold, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case secret_api::messageEntityItalic::ID: {
        auto entity_italic = static_cast<const secret_api::messageEntityItalic *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Italic, entity_italic->offset_, entity_italic->length_);
        break;
      }
      case secret_api::messageEntityUnderline::ID: {
        auto entity_bold = static_cast<const secret_api::messageEntityUnderline *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Underline, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case secret_api::messageEntityStrike::ID: {
        auto entity_bold = static_cast<const secret_api::messageEntityStrike *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Strikethrough, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case secret_api::messageEntityBlockquote::ID: {
        auto entity_bold = static_cast<const secret_api::messageEntityBlockquote *>(entity.get());
        entities.emplace_back(MessageEntity::Type::BlockQuote, entity_bold->offset_, entity_bold->length_);
        break;
      }
      case secret_api::messageEntityCode::ID: {
        auto entity_code = static_cast<const secret_api::messageEntityCode *>(entity.get());
        entities.emplace_back(MessageEntity::Type::Code, entity_code->offset_, entity_code->length_);
        break;
      }
      case secret_api::messageEntityPre::ID: {
        auto entity_pre = static_cast<secret_api::messageEntityPre *>(entity.get());
        if (!clean_input_string(entity_pre->language_)) {
          LOG(WARNING) << "Wrong language in entity: \"" << entity_pre->language_ << '"';
          entity_pre->language_.clear();
        }
        if (entity_pre->language_.empty()) {
          entities.emplace_back(MessageEntity::Type::Pre, entity_pre->offset_, entity_pre->length_);
        } else {
          entities.emplace_back(MessageEntity::Type::PreCode, entity_pre->offset_, entity_pre->length_,
                                std::move(entity_pre->language_));
        }
        break;
      }
      case secret_api::messageEntityTextUrl::ID: {
        auto entity_text_url = static_cast<secret_api::messageEntityTextUrl *>(entity.get());
        if (!clean_input_string(entity_text_url->url_)) {
          LOG(WARNING) << "Wrong URL entity: \"" << entity_text_url->url_ << '"';
          continue;
        }
        auto r_url = check_url(entity_text_url->url_);
        if (r_url.is_error()) {
          LOG(WARNING) << "Wrong URL entity: \"" << entity_text_url->url_ << "\": " << r_url.error().message();
          continue;
        }
        entities.emplace_back(MessageEntity::Type::TextUrl, entity_text_url->offset_, entity_text_url->length_,
                              r_url.move_as_ok());
        break;
      }
      case secret_api::messageEntityMentionName::ID:
        // skip all name mentions in secret chats
        break;
      default:
        UNREACHABLE();
    }
  }
  return entities;
}

// like clean_input_string but also fixes entities
// entities must be sorted, can be nested, but must not intersect each other
static Result<string> clean_input_string_with_entities(const string &text, vector<MessageEntity> &entities) {
  check_is_sorted(entities);

  struct EntityInfo {
    MessageEntity *entity;
    int32 utf16_skipped_before;

    EntityInfo(MessageEntity *entity, int32 utf16_skipped_before)
        : entity(entity), utf16_skipped_before(utf16_skipped_before) {
    }
  };
  vector<EntityInfo> nested_entities_stack;
  size_t current_entity = 0;

  int32 utf16_offset = 0;
  int32 utf16_skipped = 0;

  size_t text_size = text.size();

  string result;
  result.reserve(text_size);
  for (size_t pos = 0; pos <= text_size; pos++) {
    auto c = static_cast<unsigned char>(text[pos]);
    bool is_utf8_character_begin = is_utf8_character_first_code_unit(c);
    if (is_utf8_character_begin) {
      while (!nested_entities_stack.empty()) {
        auto *entity = nested_entities_stack.back().entity;
        auto entity_end = entity->offset + entity->length;
        if (utf16_offset < entity_end) {
          break;
        }

        if (utf16_offset != entity_end) {
          CHECK(utf16_offset == entity_end + 1);
          return Status::Error(400, PSLICE() << "Entity beginning at UTF-16 offset " << entity->offset
                                             << " ends in a middle of a UTF-16 symbol at byte offset " << pos);
        }

        auto skipped_before_current_entity = nested_entities_stack.back().utf16_skipped_before;
        entity->offset -= skipped_before_current_entity;
        entity->length -= utf16_skipped - skipped_before_current_entity;
        nested_entities_stack.pop_back();
      }
      while (current_entity < entities.size() && utf16_offset >= entities[current_entity].offset) {
        if (utf16_offset != entities[current_entity].offset) {
          CHECK(utf16_offset == entities[current_entity].offset + 1);
          return Status::Error(400, PSLICE() << "Entity begins in a middle of a UTF-16 symbol at byte offset " << pos);
        }
        nested_entities_stack.emplace_back(&entities[current_entity++], utf16_skipped);
      }
    }
    if (pos == text_size) {
      break;
    }

    switch (c) {
      // remove control characters
      case 0:
      case 1:
      case 2:
      case 3:
      case 4:
      case 5:
      case 6:
      case 7:
      case 8:
      case 9:
      // allow '\n'
      case 11:
      case 12:
      // ignore '\r'
      case 14:
      case 15:
      case 16:
      case 17:
      case 18:
      case 19:
      case 20:
      case 21:
      case 22:
      case 23:
      case 24:
      case 25:
      case 26:
      case 27:
      case 28:
      case 29:
      case 30:
      case 31:
      case 32:
        result.push_back(' ');
        utf16_offset++;
        break;
      case '\r':
        // skip
        utf16_offset++;
        utf16_skipped++;
        break;
      default:
        if (is_utf8_character_begin) {
          utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
        }
        if (c == 0xe2 && pos + 2 < text_size) {
          unsigned char next = static_cast<unsigned char>(text[pos + 1]);
          if (next == 0x80) {
            next = static_cast<unsigned char>(text[pos + 2]);
            if (0xa8 <= next && next <= 0xae) {
              pos += 2;
              utf16_skipped++;
              break;
            }
          }
        }
        if (c == 0xcc && pos + 1 < text_size) {
          unsigned char next = static_cast<unsigned char>(text[pos + 1]);
          // remove vertical lines
          if (next == 0xb3 || next == 0xbf || next == 0x8a) {
            pos++;
            utf16_skipped++;
            break;
          }
        }

        result.push_back(text[pos]);
        break;
    }
  }

  if (current_entity != entities.size()) {
    return Status::Error(400, PSLICE() << "Entity begins after the end of the text at UTF-16 offset "
                                       << entities[current_entity].offset);
  }
  if (!nested_entities_stack.empty()) {
    auto *entity = nested_entities_stack.back().entity;
    return Status::Error(400, PSLICE() << "Entity beginning at UTF-16 offset " << entity->offset
                                       << " ends after the end of the text at UTF-16 offset "
                                       << entity->offset + entity->length);
  }

  replace_offending_characters(result);

  return result;
}

// removes entities containing whitespaces only
// entities must be sorted by offset and length, but not necessary by type
// returns {last_non_whitespace_pos, last_non_whitespace_utf16_offset}
static std::pair<size_t, int32> remove_invalid_entities(const string &text, vector<MessageEntity> &entities) {
  // check_is_sorted(entities);
  vector<MessageEntity *> nested_entities_stack;
  size_t current_entity = 0;

  size_t last_non_whitespace_pos = text.size();

  int32 utf16_offset = 0;
  int32 last_space_utf16_offset = -1;
  int32 last_non_whitespace_utf16_offset = -1;

  td::remove_if(entities, [](const auto &entity) { return entity.length == 0; });

  for (size_t pos = 0; pos <= text.size(); pos++) {
    while (!nested_entities_stack.empty()) {
      auto *entity = nested_entities_stack.back();
      auto entity_end = entity->offset + entity->length;
      if (utf16_offset < entity_end) {
        break;
      }

      auto have_hidden_data = entity->type == MessageEntity::Type::TextUrl ||
                              entity->type == MessageEntity::Type::MentionName || is_pre_entity(entity->type);
      if (last_non_whitespace_utf16_offset >= entity->offset ||
          (last_space_utf16_offset >= entity->offset && have_hidden_data)) {
        // TODO check entity for validness, for example, that mentions, hashtags, cashtags and URLs are valid
        // keep entity
      } else {
        entity->length = 0;
      }

      nested_entities_stack.pop_back();
    }
    while (current_entity < entities.size() && utf16_offset >= entities[current_entity].offset) {
      nested_entities_stack.push_back(&entities[current_entity++]);
    }

    if (pos == text.size()) {
      break;
    }

    if (!nested_entities_stack.empty() && nested_entities_stack.back()->offset == utf16_offset &&
        (text[pos] == '\n' || text[pos] == ' ')) {
      // entities was fixed, so there can't be more than one splittable entity of each type, one blockquote and
      // one continuous entity for the given offset
      for (size_t i = nested_entities_stack.size(); i > 0; i--) {
        auto *entity = nested_entities_stack[i - 1];
        if (entity->offset != utf16_offset || entity->type == MessageEntity::Type::TextUrl ||
            entity->type == MessageEntity::Type::MentionName || is_pre_entity(entity->type)) {
          break;
        }
        entity->offset++;
        entity->length--;
        if (entity->length == 0) {
          CHECK(i == nested_entities_stack.size());
          nested_entities_stack.pop_back();
        }
      }
    }

    auto c = static_cast<unsigned char>(text[pos]);
    switch (c) {
      case '\n':
        break;
      case 32:
        last_space_utf16_offset = utf16_offset;
        break;
      default:
        while (!is_utf8_character_first_code_unit(static_cast<unsigned char>(text[pos + 1]))) {
          pos++;
        }
        utf16_offset += (c >= 0xf0);  // >= 4 bytes in symbol => surrogate pair
        last_non_whitespace_pos = pos;
        last_non_whitespace_utf16_offset = utf16_offset;
        break;
    }

    utf16_offset++;
  }
  CHECK(nested_entities_stack.empty());
  CHECK(current_entity == entities.size());

  td::remove_if(entities, [](const auto &entity) { return entity.length == 0; });

  return {last_non_whitespace_pos, last_non_whitespace_utf16_offset};
}

// enitities must contain only splittable entities
void split_entities(vector<MessageEntity> &entities, const vector<MessageEntity> &other_entities) {
  check_is_sorted(entities);
  check_is_sorted(other_entities);

  int32 begin_pos[SPLITTABLE_ENTITY_TYPE_COUNT] = {};
  int32 end_pos[SPLITTABLE_ENTITY_TYPE_COUNT] = {};
  auto it = entities.begin();
  vector<MessageEntity> result;
  auto add_entities = [&](int32 end_offset) {
    auto flush_entities = [&](int32 offset) {
      for (auto type : {MessageEntity::Type::Bold, MessageEntity::Type::Italic, MessageEntity::Type::Underline,
                        MessageEntity::Type::Strikethrough}) {
        auto index = get_splittable_entity_type_index(type);
        if (end_pos[index] != 0 && begin_pos[index] < offset) {
          if (end_pos[index] <= offset) {
            result.emplace_back(type, begin_pos[index], end_pos[index] - begin_pos[index]);
            begin_pos[index] = 0;
            end_pos[index] = 0;
          } else {
            result.emplace_back(type, begin_pos[index], offset - begin_pos[index]);
            begin_pos[index] = offset;
          }
        }
      }
    };

    while (it != entities.end()) {
      if (it->offset >= end_offset) {
        break;
      }
      CHECK(is_splittable_entity(it->type));
      auto index = get_splittable_entity_type_index(it->type);
      if (it->offset <= end_pos[index] && end_pos[index] != 0) {
        if (it->offset + it->length > end_pos[index]) {
          end_pos[index] = it->offset + it->length;
        }
      } else {
        flush_entities(it->offset);
        begin_pos[index] = it->offset;
        end_pos[index] = it->offset + it->length;
      }
      ++it;
    }
    flush_entities(end_offset);
  };

  vector<const MessageEntity *> nested_entities_stack;
  auto add_offset = [&](int32 offset) {
    while (!nested_entities_stack.empty() &&
           offset >= nested_entities_stack.back()->offset + nested_entities_stack.back()->length) {
      // remove non-intersecting entities from the stack
      auto old_size = result.size();
      add_entities(nested_entities_stack.back()->offset + nested_entities_stack.back()->length);
      if (is_pre_entity(nested_entities_stack.back()->type)) {
        result.resize(old_size);
      }
      nested_entities_stack.pop_back();
    }

    add_entities(offset);
  };
  for (auto &other_entity : other_entities) {
    add_offset(other_entity.offset);
    nested_entities_stack.push_back(&other_entity);
  }
  add_offset(std::numeric_limits<int32>::max());

  entities = std::move(result);

  // entities are sorted only by offset now, re-sort if needed
  sort_entities(entities);
}

static vector<MessageEntity> resplit_entities(vector<MessageEntity> &&splittable_entities,
                                              vector<MessageEntity> &&entities) {
  if (!splittable_entities.empty()) {
    split_entities(splittable_entities, entities);  // can merge some entities

    if (entities.empty()) {
      return std::move(splittable_entities);
    }

    combine(entities, std::move(splittable_entities));
    sort_entities(entities);
  }
  return std::move(entities);
}

static void fix_entities(vector<MessageEntity> &entities) {
  sort_entities(entities);

  if (are_entities_valid(entities)) {
    // fast path
    return;
  }

  vector<MessageEntity> continuous_entities;
  vector<MessageEntity> blockquote_entities;
  vector<MessageEntity> splittable_entities;
  for (auto &entity : entities) {
    if (is_splittable_entity(entity.type)) {
      splittable_entities.push_back(std::move(entity));
    } else if (is_blockquote_entity(entity.type)) {
      blockquote_entities.push_back(std::move(entity));
    } else {
      continuous_entities.push_back(std::move(entity));
    }
  }
  remove_intersecting_entities(continuous_entities);  // continuous entities can't intersect each other

  if (!blockquote_entities.empty()) {
    remove_intersecting_entities(blockquote_entities);  // blockquote entities can't intersect each other

    // blockquote entities can contain continuous entities, but can't intersect them in the other ways
    remove_entities_intersecting_blockquote(continuous_entities, blockquote_entities);

    combine(continuous_entities, std::move(blockquote_entities));
    sort_entities(continuous_entities);
  }

  // must be called once to not merge some adjacent entities
  entities = resplit_entities(std::move(splittable_entities), std::move(continuous_entities));
  check_is_sorted(entities);
}

static void merge_new_entities(vector<MessageEntity> &entities, vector<MessageEntity> new_entities) {
  check_is_sorted(entities);
  if (new_entities.empty()) {
    // fast path
    return;
  }

  check_non_intersecting(new_entities);

  vector<MessageEntity> continuous_entities;
  vector<MessageEntity> blockquote_entities;
  vector<MessageEntity> splittable_entities;
  for (auto &entity : entities) {
    if (is_splittable_entity(entity.type)) {
      splittable_entities.push_back(std::move(entity));
    } else if (is_blockquote_entity(entity.type)) {
      blockquote_entities.push_back(std::move(entity));
    } else {
      continuous_entities.push_back(std::move(entity));
    }
  }

  remove_entities_intersecting_blockquote(new_entities, blockquote_entities);

  // merge before combining with blockquote entities
  continuous_entities = merge_entities(std::move(continuous_entities), std::move(new_entities));

  if (!blockquote_entities.empty()) {
    combine(continuous_entities, std::move(blockquote_entities));
    sort_entities(continuous_entities);
  }

  // must be called once to not merge some adjacent entities
  entities = resplit_entities(std::move(splittable_entities), std::move(continuous_entities));
  check_is_sorted(entities);
}

Status fix_formatted_text(string &text, vector<MessageEntity> &entities, bool allow_empty, bool skip_new_entities,
                          bool skip_bot_commands, bool for_draft) {
  if (!check_utf8(text)) {
    return Status::Error(400, "Strings must be encoded in UTF-8");
  }

  for (auto &entity : entities) {
    if (entity.offset < 0 || entity.offset > 1000000) {
      return Status::Error(400, PSLICE() << "Receive an entity with incorrect offset " << entity.offset);
    }
    if (entity.length < 0 || entity.length > 1000000) {
      return Status::Error(400, PSLICE() << "Receive an entity with incorrect length " << entity.length);
    }
  }
  td::remove_if(entities, [](const MessageEntity &entity) { return entity.length == 0; });

  fix_entities(entities);

  TRY_RESULT(result, clean_input_string_with_entities(text, entities));

  // now entities are still sorted by offset and length, but not type,
  // because some characters could be deleted and after that some entities begin to share a common end

  size_t last_non_whitespace_pos;
  int32 last_non_whitespace_utf16_offset;
  std::tie(last_non_whitespace_pos, last_non_whitespace_utf16_offset) = remove_invalid_entities(result, entities);
  if (last_non_whitespace_utf16_offset == -1) {
    if (allow_empty) {
      text.clear();
      entities.clear();
      return Status::OK();
    }
    return Status::Error(3, "Message must be non-empty");
  }

  // re-fix entities if needed after removal of some characters
  // the sort order can be incorrect by type
  // some splittable entities may be needed to be concatenated
  fix_entities(entities);

  if (for_draft) {
    text = std::move(result);
  } else {
    // rtrim
    CHECK(last_non_whitespace_pos < result.size());
    result.resize(last_non_whitespace_pos + 1);
    while (!entities.empty() && entities.back().offset > last_non_whitespace_utf16_offset) {
      CHECK(entities.back().type == MessageEntity::Type::TextUrl ||
            entities.back().type == MessageEntity::Type::MentionName || is_pre_entity(entities.back().type));
      entities.pop_back();
    }
    bool need_sort = false;
    for (auto &entity : entities) {
      if (entity.offset + entity.length > last_non_whitespace_utf16_offset + 1) {
        entity.length = last_non_whitespace_utf16_offset + 1 - entity.offset;
        need_sort = true;
        CHECK(entity.length > 0);
      }
    }
    if (need_sort) {
      sort_entities(entities);
    }

    // ltrim
    size_t first_non_whitespaces_pos = 0;
    size_t first_entity_begin_pos = entities.empty() ? result.size() : entities[0].offset;
    while (first_non_whitespaces_pos < first_entity_begin_pos &&
           (result[first_non_whitespaces_pos] == ' ' || result[first_non_whitespaces_pos] == '\n')) {
      first_non_whitespaces_pos++;
    }
    if (first_non_whitespaces_pos > 0) {
      int32 offset = narrow_cast<int32>(first_non_whitespaces_pos);
      text = result.substr(first_non_whitespaces_pos);
      for (auto &entity : entities) {
        entity.offset -= offset;
        CHECK(entity.offset >= 0);
      }
    } else {
      text = std::move(result);
    }
  }
  LOG_CHECK(check_utf8(text)) << text;

  if (!allow_empty && is_empty_string(text)) {
    return Status::Error(3, "Message must be non-empty");
  }

  constexpr size_t LENGTH_LIMIT = 35000;  // server side limit
  if (text.size() > LENGTH_LIMIT) {
    size_t new_size = LENGTH_LIMIT;
    while (!is_utf8_character_first_code_unit(text[new_size])) {
      new_size--;
    }
    text.resize(new_size);

    td::remove_if(entities, [text_utf16_length = text_length(text)](const auto &entity) {
      return entity.offset + entity.length > text_utf16_length;
    });
  }

  if (!skip_new_entities) {
    merge_new_entities(entities, find_entities(text, skip_bot_commands));
  }

  // new whitespace-only entities could be added after splitting of entities
  remove_invalid_entities(text, entities);

  // TODO MAX_MESSAGE_LENGTH and MAX_CAPTION_LENGTH

  return Status::OK();
}

FormattedText get_message_text(const ContactsManager *contacts_manager, string message_text,
                               vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities,
                               bool skip_new_entities, int32 send_date, bool from_album, const char *source) {
  auto entities = get_message_entities(contacts_manager, std::move(server_entities), source);
  auto debug_message_text = message_text;
  auto debug_entities = entities;
  auto status = fix_formatted_text(message_text, entities, true, skip_new_entities, true, false);
  if (status.is_error()) {
    if (!from_album && (send_date == 0 || send_date > 1600340000)) {  // approximate fix date
      LOG(ERROR) << "Receive error " << status << " while parsing message text from " << source << " sent at "
                 << send_date << " with content \"" << debug_message_text << "\" -> \"" << message_text
                 << "\" with entities " << format::as_array(debug_entities) << " -> " << format::as_array(entities);
    }
    if (!clean_input_string(message_text)) {
      message_text.clear();
    }
    entities = find_entities(message_text, false);
  }
  return FormattedText{std::move(message_text), std::move(entities)};
}

td_api::object_ptr<td_api::formattedText> extract_input_caption(
    tl_object_ptr<td_api::InputMessageContent> &input_message_content) {
  switch (input_message_content->get_id()) {
    case td_api::inputMessageAnimation::ID: {
      auto input_animation = static_cast<td_api::inputMessageAnimation *>(input_message_content.get());
      return std::move(input_animation->caption_);
    }
    case td_api::inputMessageAudio::ID: {
      auto input_audio = static_cast<td_api::inputMessageAudio *>(input_message_content.get());
      return std::move(input_audio->caption_);
    }
    case td_api::inputMessageDocument::ID: {
      auto input_document = static_cast<td_api::inputMessageDocument *>(input_message_content.get());
      return std::move(input_document->caption_);
    }
    case td_api::inputMessagePhoto::ID: {
      auto input_photo = static_cast<td_api::inputMessagePhoto *>(input_message_content.get());
      return std::move(input_photo->caption_);
    }
    case td_api::inputMessageVideo::ID: {
      auto input_video = static_cast<td_api::inputMessageVideo *>(input_message_content.get());
      return std::move(input_video->caption_);
    }
    case td_api::inputMessageVoiceNote::ID: {
      auto input_voice_note = static_cast<td_api::inputMessageVoiceNote *>(input_message_content.get());
      return std::move(input_voice_note->caption_);
    }
    default:
      return nullptr;
  }
}

Result<FormattedText> process_input_caption(const ContactsManager *contacts_manager, DialogId dialog_id,
                                            tl_object_ptr<td_api::formattedText> &&caption, bool is_bot) {
  if (caption == nullptr) {
    return FormattedText();
  }
  TRY_RESULT(entities, get_message_entities(contacts_manager, std::move(caption->entities_)));
  TRY_STATUS(fix_formatted_text(caption->text_, entities, true, false,
                                need_skip_bot_commands(contacts_manager, dialog_id, is_bot), false));
  return FormattedText{std::move(caption->text_), std::move(entities)};
}

void add_formatted_text_dependencies(Dependencies &dependencies, const FormattedText *text) {
  if (text == nullptr) {
    return;
  }
  for (auto &entity : text->entities) {
    if (entity.user_id.is_valid()) {
      dependencies.user_ids.insert(entity.user_id);
    }
  }
}

bool need_skip_bot_commands(const ContactsManager *contacts_manager, DialogId dialog_id, bool is_bot) {
  if (!dialog_id.is_valid()) {
    return true;
  }
  if (is_bot) {
    return false;
  }

  switch (dialog_id.get_type()) {
    case DialogType::User: {
      auto user_id = dialog_id.get_user_id();
      return user_id == ContactsManager::get_replies_bot_user_id() || !contacts_manager->is_user_bot(user_id);
    }
    case DialogType::SecretChat: {
      auto user_id = contacts_manager->get_secret_chat_user_id(dialog_id.get_secret_chat_id());
      return !user_id.is_valid() || !contacts_manager->is_user_bot(user_id);
    }
    case DialogType::Chat:
    case DialogType::Channel:
    case DialogType::None:
      return false;
    default:
      UNREACHABLE();
      return false;
  }
}

}  // namespace td
