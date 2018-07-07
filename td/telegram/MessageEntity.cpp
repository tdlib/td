//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageEntity.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/misc.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/unicode.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <cstring>
#include <tuple>
#include <unordered_set>

namespace td {

StringBuilder &operator<<(StringBuilder &string_builder, const MessageEntity &message_entity) {
  bool has_argument = false;
  string_builder << '[';
  switch (message_entity.type) {
    case MessageEntity::Type::Mention:
      string_builder << "Mention";
      break;
    case MessageEntity::Type::Hashtag:
      string_builder << "Hashtag";
      break;
    case MessageEntity::Type::BotCommand:
      string_builder << "BotCommand";
      break;
    case MessageEntity::Type::Url:
      string_builder << "Url";
      break;
    case MessageEntity::Type::EmailAddress:
      string_builder << "EmailAddress";
      break;
    case MessageEntity::Type::Bold:
      string_builder << "Bold";
      break;
    case MessageEntity::Type::Italic:
      string_builder << "Italic";
      break;
    case MessageEntity::Type::Code:
      string_builder << "Code";
      break;
    case MessageEntity::Type::Pre:
      string_builder << "Pre";
      break;
    case MessageEntity::Type::PreCode:
      string_builder << "PreCode";
      has_argument = true;
      break;
    case MessageEntity::Type::TextUrl:
      string_builder << "TextUrl";
      has_argument = true;
      break;
    case MessageEntity::Type::MentionName:
      string_builder << "MentionName";
      break;
    case MessageEntity::Type::Cashtag:
      string_builder << "Cashtag";
      break;
    case MessageEntity::Type::PhoneNumber:
      string_builder << "PhoneNumber";
      break;
    default:
      UNREACHABLE();
      string_builder << "Impossible";
      break;
  }

  string_builder << ", offset = " << message_entity.offset << ", length = " << message_entity.length;
  if (has_argument) {
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
    case MessageEntity::Type::Code:
      return make_tl_object<td_api::textEntityTypeCode>();
    case MessageEntity::Type::Pre:
      return make_tl_object<td_api::textEntityTypePre>();
    case MessageEntity::Type::PreCode:
      return make_tl_object<td_api::textEntityTypePreCode>(argument);
    case MessageEntity::Type::TextUrl:
      return make_tl_object<td_api::textEntityTypeTextUrl>(argument);
    case MessageEntity::Type::MentionName:
      return make_tl_object<td_api::textEntityTypeMentionName>(user_id.get());
    case MessageEntity::Type::Cashtag:
      return make_tl_object<td_api::textEntityTypeCashtag>();
    case MessageEntity::Type::PhoneNumber:
      return make_tl_object<td_api::textEntityTypePhoneNumber>();
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
    result.push_back(entity.get_text_entity_object());
  }

  return result;
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

td_api::object_ptr<td_api::formattedText> get_formatted_text_object(const FormattedText &text) {
  return td_api::make_object<td_api::formattedText>(text.text, get_text_entities_object(text.entities));
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
    ptr = reinterpret_cast<const unsigned char *>(std::memchr(ptr, '@', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);

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
      next_utf8_unsafe(ptr, &next);
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
    ptr = reinterpret_cast<const unsigned char *>(std::memchr(ptr, '/', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);

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
      next_utf8_unsafe(ptr, &next);
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
    ptr = reinterpret_cast<const unsigned char *>(std::memchr(ptr, '#', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);

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
      auto next_ptr = next_utf8_unsafe(ptr, &code);
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
    ptr = reinterpret_cast<const unsigned char *>(std::memchr(ptr, '$', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    if (ptr != begin) {
      uint32 prev;
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);

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
      next_utf8_unsafe(ptr, &code);
      if (is_hashtag_letter(code, category) || code == '$') {
        continue;
      }
    }

    result.emplace_back(cashtag_begin - 1, cashtag_end);
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
    if (dot_pos > str.size()) {
      break;
    }

    const unsigned char *last_at_ptr = nullptr;
    const unsigned char *domain_end_ptr = begin + dot_pos;
    while (domain_end_ptr != end) {
      uint32 code = 0;
      auto next_ptr = next_utf8_unsafe(domain_end_ptr, &code);
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
      auto next_ptr = next_utf8_unsafe(domain_end_ptr, &code);
      if (!is_domain_symbol(code)) {
        break;
      }
      domain_end_ptr = next_ptr;
    }

    const unsigned char *domain_begin_ptr = begin + dot_pos;
    while (domain_begin_ptr != begin) {
      domain_begin_ptr = prev_utf8_unsafe(domain_begin_ptr);
      uint32 code = 0;
      auto next_ptr = next_utf8_unsafe(domain_begin_ptr, &code);
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
        auto next_ptr = next_utf8_unsafe(path_end_ptr, &code);
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
        auto next_ptr = next_utf8_unsafe(user_data_begin_ptr, &code);
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
          auto next_ptr = next_utf8_unsafe(protocol_begin_ptr, &code);
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
        next_utf8_unsafe(prefix_back, &code);
        if (is_word_character(code) || code == '/' || code == '#' || code == '@') {
          is_bad = true;
        }
      }
    }
    // LOG(ERROR) << "full: " << Slice(url_begin_ptr, url_end_ptr) << " " << is_bad;

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
  static const std::unordered_set<Slice, SliceHash> valid_usernames{"ya",   "gif",  "wiki", "vid",  "bing", "pic",
                                                                    "bold", "imdb", "coub", "like", "vote"};
  return valid_usernames;
}

vector<Slice> find_mentions(Slice str) {
  auto mentions = match_mentions(str);
  mentions.erase(std::remove_if(mentions.begin(), mentions.end(),
                                [](Slice mention) {
                                  mention.remove_prefix(1);
                                  if (mention.size() >= 5) {
                                    return false;
                                  }
                                  return get_valid_short_usernames().count(mention) == 0;
                                }),
                 mentions.end());
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

// sorts entities, removes intersecting and empty entities
static void fix_entities(vector<MessageEntity> &entities) {
  if (entities.empty()) {
    return;
  }

  std::sort(entities.begin(), entities.end());

  int32 last_entity_end = 0;
  size_t left_entities = 0;
  for (size_t i = 0; i < entities.size(); i++) {
    if (entities[i].length > 0 && entities[i].offset >= last_entity_end) {
      last_entity_end = entities[i].offset + entities[i].length;
      if (i != left_entities) {
        entities[left_entities] = std::move(entities[i]);
      }
      left_entities++;
    }
  }
  entities.erase(entities.begin() + left_entities, entities.end());
}

vector<MessageEntity> find_entities(Slice text, bool skip_bot_commands, bool only_urls) {
  vector<MessageEntity> entities;

  if (!only_urls) {
    auto mentions = find_mentions(text);
    for (auto &mention : mentions) {
      entities.emplace_back(MessageEntity::Type::Mention, narrow_cast<int32>(mention.begin() - text.begin()),
                            narrow_cast<int32>(mention.size()));
    }

    if (!skip_bot_commands) {
      auto bot_commands = find_bot_commands(text);
      for (auto &bot_command : bot_commands) {
        entities.emplace_back(MessageEntity::Type::BotCommand, narrow_cast<int32>(bot_command.begin() - text.begin()),
                              narrow_cast<int32>(bot_command.size()));
      }
    }

    auto hashtags = find_hashtags(text);
    for (auto &hashtag : hashtags) {
      entities.emplace_back(MessageEntity::Type::Hashtag, narrow_cast<int32>(hashtag.begin() - text.begin()),
                            narrow_cast<int32>(hashtag.size()));
    }

    auto cashtags = find_cashtags(text);
    for (auto &cashtag : cashtags) {
      entities.emplace_back(MessageEntity::Type::Cashtag, narrow_cast<int32>(cashtag.begin() - text.begin()),
                            narrow_cast<int32>(cashtag.size()));
    }

    // TODO find_phone_numbers
  }

  auto urls = find_urls(text);
  for (auto &url : urls) {
    // TODO better find messageEntityUrl
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

  fix_entities(entities);

  // fix offsets to utf16 offsets
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
      ptr = next_utf8_unsafe(ptr, nullptr);

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
        if (begins_with(url, "tg:") || is_plain_domain(url)) {
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
      case MessageEntity::Type::Code:
        break;
      case MessageEntity::Type::Pre:
        break;
      case MessageEntity::Type::PreCode:
        break;
      case MessageEntity::Type::TextUrl:
        if (begins_with(entity.argument, "tg:")) {
          continue;
        }
        return entity.argument;
      case MessageEntity::Type::MentionName:
        break;
      case MessageEntity::Type::Cashtag:
        break;
      case MessageEntity::Type::PhoneNumber:
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
        utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogaite pair
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
      while (language_end < size && !is_space(text[language_end]) && text[language_end] != '`') {
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

    int32 utf16_entity_length = 0;
    while (i < size && (text[i] != end_character || (is_pre && !(text[i + 1] == '`' && text[i + 2] == '`')))) {
      auto cur_ch = static_cast<unsigned char>(text[i]);
      if (is_utf8_character_first_code_unit(cur_ch)) {
        utf16_entity_length += 1 + (cur_ch >= 0xf0);  // >= 4 bytes in symbol => surrogaite pair
      }
      result.push_back(text[i++]);
    }
    if (i == size) {
      return Status::Error(400, PSLICE() << "Can't find end of the entity starting at byte offset " << begin_pos);
    }

    if (utf16_entity_length > 0) {
      switch (c) {
        case '_':
          entities.emplace_back(MessageEntity::Type::Italic, utf16_offset, utf16_entity_length);
          break;
        case '*':
          entities.emplace_back(MessageEntity::Type::Bold, utf16_offset, utf16_entity_length);
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
            entities.emplace_back(utf16_offset, utf16_entity_length, user_id);
          } else {
            auto r_url = check_url(url);
            if (r_url.is_ok()) {
              entities.emplace_back(MessageEntity::Type::TextUrl, utf16_offset, utf16_entity_length,
                                    r_url.move_as_ok());
            }
          }
          break;
        }
        case '`':
          if (is_pre) {
            if (language.empty()) {
              entities.emplace_back(MessageEntity::Type::Pre, utf16_offset, utf16_entity_length);
            } else {
              entities.emplace_back(MessageEntity::Type::PreCode, utf16_offset, utf16_entity_length, language);
            }
          } else {
            entities.emplace_back(MessageEntity::Type::Code, utf16_offset, utf16_entity_length);
          }
          break;
        default:
          UNREACHABLE();
      }
      utf16_offset += utf16_entity_length;
    }
    if (is_pre) {
      i += 2;
    }
  }
  text = result;
  return entities;
}

static uint32 decode_html_entity(const string &text, size_t &pos) {
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
    string entity(text, pos + 1, end_pos - pos - 1);
    if (entity == "lt") {
      res = static_cast<uint32>('<');
    } else if (entity == "gt") {
      res = static_cast<uint32>('>');
    } else if (entity == "amp") {
      res = static_cast<uint32>('&');
    } else if (entity == "quot") {
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

Result<vector<MessageEntity>> parse_html(string &text) {
  string result;
  vector<MessageEntity> entities;
  size_t size = text.size();
  int32 utf16_offset = 0;
  for (size_t i = 0; i < size; i++) {
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
        utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogaite pair
      }
      result.push_back(text[i]);
      continue;
    }

    // we are at begin of the entity
    size_t begin_pos = i++;
    if (text[i] == '/') {
      return Status::Error(400, PSLICE() << "Unexpected end tag at byte offset " << begin_pos);
    }
    while (!is_space(text[i]) && text[i] != '>') {
      i++;
    }
    if (text[i] == 0) {
      return Status::Error(400, PSLICE() << "Unclosed start tag at byte offset " << begin_pos);
    }

    string tag_name(text, begin_pos + 1, i - begin_pos - 1);
    to_lower_inplace(tag_name);
    if (tag_name != "em" && tag_name != "strong" && tag_name != "a" && tag_name != "b" && tag_name != "i" &&
        tag_name != "pre" && tag_name != "code") {
      return Status::Error(400,
                           PSLICE() << "Unsupported start tag \"" << tag_name << "\" at byte offset " << begin_pos);
    }

    string url;
    // string language; TODO PreCode support
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
      string attribute_name(text, attribute_begin_pos, i - attribute_begin_pos);
      if (attribute_name.empty()) {
        return Status::Error(400, PSLICE() << "Expected equal sign in declaration of attribute of the tag \""
                                           << tag_name << "\" at byte offset " << begin_pos);
      }
      while (text[i] != 0 && is_space(text[i])) {
        i++;
      }
      if (text[i] != '=') {
        return Status::Error(400, PSLICE() << "Expected equal sign in declaration of attribute of the tag \""
                                           << tag_name << "\" at byte offset " << begin_pos);
      }
      i++;
      while (text[i] != 0 && is_space(text[i])) {
        i++;
      }
      if (text[i] == 0) {
        return Status::Error(400, PSLICE() << "Unclosed start tag at byte offset " << begin_pos);
      }

      string attribute_value;
      if (text[i] != '\'' && text[i] != '"') {
        // A name token (a sequence of letters, digits, periods, or hyphens). Name tokens are not case sensitive.
        auto token_begin_pos = i;
        while (is_alnum(text[i]) || text[i] == '.' || text[i] == '-') {
          i++;
        }
        attribute_value.assign(text, token_begin_pos, i - token_begin_pos);
        to_lower_inplace(attribute_value);

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

      if (tag_name == "a" && attribute_name == "href") {
        url = attribute_value;
      }
    }
    i++;

    int32 utf16_entity_length = 0;
    size_t entity_begin_pos = result.size();
    while (text[i] != 0 && text[i] != '<') {
      auto cur_ch = static_cast<unsigned char>(text[i]);
      if (cur_ch == '&') {
        auto ch = decode_html_entity(text, i);
        if (ch != 0) {
          utf16_entity_length += 1 + (ch > 0xffff);
          append_utf8_character(result, ch);
          continue;
        }
      }
      if (is_utf8_character_first_code_unit(cur_ch)) {
        utf16_entity_length += 1 + (cur_ch >= 0xf0);  // >= 4 bytes in symbol => surrogaite pair
      }
      result.push_back(text[i++]);
    }
    if (text[i] == 0) {
      return Status::Error(400,
                           PSLICE() << "Can't found end tag corresponding to start tag at byte offset " << begin_pos);
    }

    auto end_tag_begin_pos = i++;
    if (text[i] != '/') {
      return Status::Error(400, PSLICE() << "Expected end tag at byte offset " << end_tag_begin_pos);
    }
    while (!is_space(text[i]) && text[i] != '>') {
      i++;
    }
    string end_tag_name(text, end_tag_begin_pos + 2, i - end_tag_begin_pos - 2);
    while (is_space(text[i]) && text[i] != 0) {
      i++;
    }
    if (text[i] != '>') {
      return Status::Error(400, PSLICE() << "Unclosed end tag at byte offset " << end_tag_begin_pos);
    }
    if (!end_tag_name.empty() && end_tag_name != tag_name) {
      return Status::Error(400, PSLICE() << "Unmatched end tag at byte offset " << end_tag_begin_pos
                                         << ", expected \"</" << tag_name << ">\", found\"</" << end_tag_name << ">\"");
    }

    if (utf16_entity_length > 0) {
      if (tag_name == "i" || tag_name == "em") {
        entities.emplace_back(MessageEntity::Type::Italic, utf16_offset, utf16_entity_length);
      } else if (tag_name == "b" || tag_name == "strong") {
        entities.emplace_back(MessageEntity::Type::Bold, utf16_offset, utf16_entity_length);
      } else if (tag_name == "a") {
        if (url.empty()) {
          url = result.substr(entity_begin_pos);
        }
        auto user_id = get_link_user_id(url);
        if (user_id.is_valid()) {
          entities.emplace_back(utf16_offset, utf16_entity_length, user_id);
        } else {
          auto r_url = check_url(url);
          if (r_url.is_ok()) {
            entities.emplace_back(MessageEntity::Type::TextUrl, utf16_offset, utf16_entity_length, r_url.move_as_ok());
          }
        }
      } else if (tag_name == "pre") {
        entities.emplace_back(MessageEntity::Type::Pre, utf16_offset, utf16_entity_length);
      } else if (tag_name == "code") {
        entities.emplace_back(MessageEntity::Type::Code, utf16_offset, utf16_entity_length);
      }
      utf16_offset += utf16_entity_length;
    }
  }
  text = result;
  return entities;
}

vector<tl_object_ptr<telegram_api::MessageEntity>> get_input_message_entities(const ContactsManager *contacts_manager,
                                                                              const vector<MessageEntity> &entities,
                                                                              const char *source) {
  vector<tl_object_ptr<telegram_api::MessageEntity>> result;
  for (auto &entity : entities) {
    switch (entity.type) {
      case MessageEntity::Type::Mention:
      case MessageEntity::Type::Hashtag:
      case MessageEntity::Type::BotCommand:
      case MessageEntity::Type::Url:
      case MessageEntity::Type::EmailAddress:
      case MessageEntity::Type::Cashtag:
      case MessageEntity::Type::PhoneNumber:
        continue;
      case MessageEntity::Type::Bold:
        result.push_back(make_tl_object<telegram_api::messageEntityBold>(entity.offset, entity.length));
        break;
      case MessageEntity::Type::Italic:
        result.push_back(make_tl_object<telegram_api::messageEntityItalic>(entity.offset, entity.length));
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
        CHECK(input_user != nullptr) << source;
        result.push_back(make_tl_object<telegram_api::inputMessageEntityMentionName>(entity.offset, entity.length,
                                                                                     std::move(input_user)));
        break;
      }
      default:
        UNREACHABLE();
    }
  }

  return result;
}

vector<tl_object_ptr<secret_api::MessageEntity>> get_input_secret_message_entities(
    const vector<MessageEntity> &entities) {
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
      case MessageEntity::Type::PhoneNumber:
        break;
      default:
        UNREACHABLE();
    }
  }

  return result;
}

Result<vector<MessageEntity>> get_message_entities(const ContactsManager *contacts_manager,
                                                   const vector<tl_object_ptr<td_api::textEntity>> &input_entities) {
  vector<MessageEntity> entities;
  for (auto &entity : input_entities) {
    if (entity == nullptr || entity->type_ == nullptr) {
      continue;
    }

    switch (entity->type_->get_id()) {
      case td_api::textEntityTypeMention::ID:
      case td_api::textEntityTypeHashtag::ID:
      case td_api::textEntityTypeBotCommand::ID:
      case td_api::textEntityTypeUrl::ID:
      case td_api::textEntityTypeEmailAddress::ID:
      case td_api::textEntityTypeCashtag::ID:
      case td_api::textEntityTypePhoneNumber::ID:
        break;
      case td_api::textEntityTypeBold::ID:
        entities.emplace_back(MessageEntity::Type::Bold, entity->offset_, entity->length_);
        break;
      case td_api::textEntityTypeItalic::ID:
        entities.emplace_back(MessageEntity::Type::Italic, entity->offset_, entity->length_);
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
        if (!contacts_manager->have_input_user(user_id)) {
          return Status::Error(7, "Have no access to the user");
        }
        entities.emplace_back(entity->offset_, entity->length_, user_id);
        break;
      }
      default:
        UNREACHABLE();
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
        // TODO const telegram_api::messageEntityTextUrl *
        auto entity_text_url = static_cast<telegram_api::messageEntityTextUrl *>(entity.get());
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
          LOG(ERROR) << "Receive unaccessible " << user_id << " in MentionName from " << source;
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

Status fix_formatted_text(string &text, vector<MessageEntity> &entities, bool allow_empty, bool skip_new_entities,
                          bool skip_bot_commands, bool for_draft) {
  if (!check_utf8(text)) {
    return Status::Error(400, "Strings must be encoded in UTF-8");
  }

  fix_entities(entities);

  bool in_entity = false;
  bool has_non_space_in_entity = false;
  size_t current_entity = 0;
  int32 skipped_before_current_entity = 0;
  size_t left_entities = 0;  // will remove all entities containing spaces only

  int32 utf16_offset = 0;
  int32 utf16_skipped = 0;

  size_t text_size = text.size();
  size_t last_non_space_pos = text_size + 1;
  int32 last_non_space_utf16_offset = 0;

  string result;
  result.reserve(text_size);
  for (size_t pos = 0; pos <= text_size; pos++) {
    auto c = static_cast<unsigned char>(text[pos]);
    bool is_utf8_character_begin = is_utf8_character_first_code_unit(c);
    if (is_utf8_character_begin) {
      if (in_entity) {
        CHECK(current_entity < entities.size());
        if (utf16_offset >= entities[current_entity].offset + entities[current_entity].length) {
          if (utf16_offset != entities[current_entity].offset + entities[current_entity].length) {
            CHECK(utf16_offset == entities[current_entity].offset + entities[current_entity].length + 1);
            return Status::Error(16, PSLICE() << "Entity beginning at UTF-16 offset " << entities[current_entity].offset
                                              << " ends in a middle of a UTF-16 symbol at byte offset " << pos);
          }
          entities[current_entity].offset -= skipped_before_current_entity;
          entities[current_entity].length -= utf16_skipped - skipped_before_current_entity;
          in_entity = false;

          if (has_non_space_in_entity) {
            // TODO check entities for validness, for example, that mentions, hashtags, cashtags and URLs are valid
            if (current_entity != left_entities) {
              entities[left_entities] = std::move(entities[current_entity]);
            }
            left_entities++;
          }
          current_entity++;
        }
      }
      if (!in_entity && current_entity < entities.size() && utf16_offset >= entities[current_entity].offset) {
        if (utf16_offset != entities[current_entity].offset) {
          CHECK(utf16_offset == entities[current_entity].offset + 1);
          return Status::Error(16, PSLICE() << "Entity begins in a middle of a UTF-16 symbol at byte offset " << pos);
        }
        in_entity = true;
        has_non_space_in_entity = false;
        skipped_before_current_entity = utf16_skipped;
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
          utf16_offset += 1 + (c >= 0xf0);  // >= 4 bytes in symbol => surrogaite pair
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

        if (c != '\n') {
          has_non_space_in_entity = true;
          last_non_space_pos = result.size();
          last_non_space_utf16_offset = utf16_offset - utf16_skipped;
        }
        break;
    }
  }
  entities.erase(entities.begin() + left_entities, entities.end());

  if (last_non_space_pos == text_size + 1) {
    if (allow_empty) {
      text.clear();
      entities.clear();
      return Status::OK();
    }
    return Status::Error(3, "Message must be non-empty");
  }

  if (for_draft) {
    text = std::move(result);
  } else {
    // rtrim
    result.resize(last_non_space_pos);
    for (auto &entity : entities) {
      if (entity.offset + entity.length > last_non_space_utf16_offset) {
        entity.length = last_non_space_utf16_offset - entity.offset;
        CHECK(entity.length > 0);
      }
    }

    // ltrim
    size_t first_non_spaces_pos = 0;
    size_t first_entity_begin_pos = entities.empty() ? result.size() : entities[0].offset;
    while (first_non_spaces_pos < first_entity_begin_pos &&
           (result[first_non_spaces_pos] == ' ' || result[first_non_spaces_pos] == '\n')) {
      first_non_spaces_pos++;
    }
    if (first_non_spaces_pos > 0) {
      int32 offset = narrow_cast<int32>(first_non_spaces_pos);
      text = result.substr(first_non_spaces_pos);
      for (auto &entity : entities) {
        entity.offset -= offset;
        CHECK(entity.offset >= 0);
      }
    } else {
      text = std::move(result);
    }
  }

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
    while (!entities.empty() && entities.back().offset + entities.back().length > 8192) {
      entities.pop_back();
    }
  }

  if (!skip_new_entities) {
    entities = merge_entities(std::move(entities), find_entities(text, skip_bot_commands));
  }

  for (auto it = entities.begin(); it != entities.end(); ++it) {
    CHECK(it->length > 0);
    if (it + 1 != entities.end()) {
      CHECK(it->offset + it->length <= (it + 1)->offset);
    }
  }

  // TODO MAX_MESSAGE_LENGTH and MAX_CAPTION_LENGTH

  return Status::OK();
}

}  // namespace td
