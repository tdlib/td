//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageEntity.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/misc.h"

#include "td/utils/HttpUrl.h"
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

static bool is_word_character(uint32 a) {
  switch (get_unicode_simple_category(a)) {
    case UnicodeSimpleCategory::Letter:
    case UnicodeSimpleCategory::DecimalNumber:
    case UnicodeSimpleCategory::Number:
      return true;
    default:
      return a == '_';
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

static bool is_alpha_digit(uint32 a) {
  return ('0' <= a && a <= '9') || ('a' <= a && a <= 'z') || ('A' <= a && a <= 'Z');
}

static bool is_alpha_digit_or_underscore(uint32 a) {
  return is_alpha_digit(a) || a == '_';
}

static bool is_alpha_digit_or_underscore_or_minus(uint32 a) {
  return is_alpha_digit_or_underscore(a) || a == '-';
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

    uint32 prev = 0;
    if (ptr != begin) {
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);
    }
    if (is_word_character(prev)) {
      ptr++;
      continue;
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

    uint32 prev = 0;
    if (ptr != begin) {
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);
    }
    if (is_word_character(prev) || prev == '/' || prev == '<' || prev == '>') {
      ptr++;
      continue;
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

static vector<Slice> match_hashtags(Slice str) {
  vector<Slice> result;
  const unsigned char *begin = str.ubegin();
  const unsigned char *end = str.uend();
  const unsigned char *ptr = begin;

  // '/(?<=^|[^\d_\pL\x{200c}])#([\d_\pL\x{200c}]{1,256})(?![\d_\pL\x{200c}]*#)/u'
  // and at least one letter

  UnicodeSimpleCategory category;
  const auto &is_hashtag_letter = [&category](uint32 c) {
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
  };

  while (true) {
    ptr = reinterpret_cast<const unsigned char *>(std::memchr(ptr, '#', narrow_cast<int32>(end - ptr)));
    if (ptr == nullptr) {
      break;
    }

    uint32 prev = 0;
    if (ptr != begin) {
      next_utf8_unsafe(prev_utf8_unsafe(ptr), &prev);
    }
    if (is_hashtag_letter(prev)) {
      ptr++;
      continue;
    }
    auto hashtag_begin = ++ptr;
    size_t hashtag_size = 0;
    const unsigned char *hashtag_end = nullptr;
    bool was_letter = false;
    while (ptr != end) {
      uint32 code;
      auto next_ptr = next_utf8_unsafe(ptr, &code);
      if (!is_hashtag_letter(code)) {
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
      if (url_end_ptr[0] == '/' || url_end_ptr[0] == '#' || path_end_ptr > url_end_ptr + 1) {
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
      {"abb", "abbott", "abogado", "academy", "accenture", "accountant", "accountants", "aco", "active", "actor", "ads",
       "adult", "aeg", "aero", "afl", "agency", "aig", "airforce", "airtel", "allfinanz", "alsace", "amsterdam",
       "android", "apartments", "app", "aquarelle", "archi", "army", "arpa", "asia", "associates", "attorney",
       "auction", "audio", "auto", "autos", "axa", "azure", "band", "bank", "bar", "barcelona", "barclaycard",
       "barclays", "bargains", "bauhaus", "bayern", "bbc", "bbva", "bcn", "beer", "bentley", "berlin", "best", "bet",
       "bharti", "bible", "bid", "bike", "bing", "bingo", "bio", "biz", "black", "blackfriday", "blog", "bloomberg",
       "blue", "bmw", "bnl", "bnpparibas", "boats", "bond", "boo", "boots", "boutique", "bradesco", "bridgestone",
       "broker", "brother", "brussels", "budapest", "build", "builders", "business", "buzz", "bzh", "cab", "cafe",
       "cal", "camera", "camp", "cancerresearch", "canon", "capetown", "capital", "caravan", "cards", "care", "career",
       "careers", "cars", "cartier", "casa", "cash", "casino", "cat", "catering", "cba", "cbn", "ceb", "center", "ceo",
       "cern", "cfa", "cfd", "chanel", "channel", "chat", "cheap", "chloe", "christmas", "chrome", "church", "cisco",
       "citic", "city", "claims", "cleaning", "click", "clinic", "clothing", "cloud", "club", "coach", "codes",
       "coffee", "college", "cologne", "com", "commbank", "community", "company", "computer", "condos", "construction",
       "consulting", "contractors", "cooking", "cool", "coop", "corsica", "country", "coupons", "courses", "credit",
       "creditcard", "cricket", "crown", "crs", "cruises", "cuisinella", "cymru", "cyou", "dabur", "dad", "dance",
       "date", "dating", "datsun", "day", "dclk", "deals", "degree", "delivery", "delta", "democrat", "dental",
       "dentist", "desi", "design", "dev", "diamonds", "diet", "digital", "direct", "directory", "discount", "dnp",
       "docs", "dog", "doha", "domains", "doosan", "download", "drive", "durban", "dvag", "earth", "eat", "edu",
       "education", "email", "emerck", "energy", "engineer", "engineering", "enterprises", "epson", "equipment", "erni",
       "esq", "estate", "eurovision", "eus", "events", "everbank", "exchange", "expert", "exposed", "express", "fage",
       "fail", "faith", "family", "fan", "fans", "farm", "fashion", "feedback", "film", "finance", "financial",
       "firmdale", "fish", "fishing", "fit", "fitness", "flights", "florist", "flowers", "flsmidth", "fly", "foo",
       "football", "forex", "forsale", "forum", "foundation", "frl", "frogans", "fund", "furniture", "futbol", "fyi",
       "gal", "gallery", "game", "garden", "gbiz", "gdn", "gent", "genting", "ggee", "gift", "gifts", "gives", "giving",
       "glass", "gle", "global", "globo", "gmail", "gmo", "gmx", "gold", "goldpoint", "golf", "goo", "goog", "google",
       "gop", "gov", "graphics", "gratis", "green", "gripe", "group", "guge", "guide", "guitars", "guru", "hamburg",
       "hangout", "haus", "healthcare", "help", "here", "hermes", "hiphop", "hitachi", "hiv", "hockey", "holdings",
       "holiday", "homedepot", "homes", "honda", "horse", "host", "hosting", "hoteles", "hotmail", "house", "how",
       "hsbc", "ibm", "icbc", "ice", "icu", "ifm", "iinet", "immo", "immobilien", "industries", "infiniti", "info",
       "ing", "ink", "institute", "insure", "int", "international", "investments", "ipiranga", "irish", "ist",
       "istanbul", "itau", "iwc", "java", "jcb", "jetzt", "jewelry", "jlc", "jll", "jobs", "joburg", "jprs", "juegos",
       "kaufen", "kddi", "kim", "kitchen", "kiwi", "koeln", "komatsu", "krd", "kred", "kyoto", "lacaixa", "lancaster",
       "land", "lasalle", "lat", "latrobe", "law", "lawyer", "lds", "lease", "leclerc", "legal", "lexus", "lgbt",
       "liaison", "lidl", "life", "lighting", "limited", "limo", "link", "live", "lixil", "loan", "loans", "lol",
       "london", "lotte", "lotto", "love", "ltda", "lupin", "luxe", "luxury", "madrid", "maif", "maison", "man",
       "management", "mango", "market", "marketing", "markets", "marriott", "mba", "media", "meet", "melbourne", "meme",
       "memorial", "men", "menu", "miami", "microsoft", "mil", "mini", "mma", "mobi", "moda", "moe", "mom", "monash",
       "money", "montblanc", "mormon", "mortgage", "moscow", "motorcycles", "mov", "movie", "movistar", "mtn", "mtpc",
       "museum", "nadex", "nagoya", "name", "navy", "nec", "net", "netbank", "network", "neustar", "new", "news",
       "nexus", "ngo", "nhk", "nico", "ninja", "nissan", "nokia", "nra", "nrw", "ntt", "nyc", "office", "okinawa",
       "omega", "one", "ong", "onl", "online", "ooo", "oracle", "orange", "org", "organic", "osaka", "otsuka", "ovh",
       "page", "panerai", "paris", "partners", "parts", "party", "pet", "pharmacy", "philips", "photo", "photography",
       "photos", "physio", "piaget", "pics", "pictet", "pictures", "pink", "pizza", "place", "play", "plumbing", "plus",
       "pohl", "poker", "porn", "post", "praxi", "press", "pro", "prod", "productions", "prof", "properties",
       "property", "pub", "qpon", "quebec", "racing", "realtor", "realty", "recipes", "red", "redstone", "rehab",
       "reise", "reisen", "reit", "ren", "rent", "rentals", "repair", "report", "republican", "rest", "restaurant",
       "review", "reviews", "rich", "ricoh", "rio", "rip", "rocks", "rodeo", "rsvp", "ruhr", "run", "ryukyu",
       "saarland", "sakura", "sale", "samsung", "sandvik", "sandvikcoromant", "sanofi", "sap", "sarl", "saxo", "sca",
       "scb", "schmidt", "scholarships", "school", "schule", "schwarz", "science", "scor", "scot", "seat", "seek",
       "sener", "services", "sew", "sex", "sexy", "shiksha", "shoes", "show", "shriram", "singles", "site", "ski",
       "sky", "skype", "sncf", "soccer", "social", "software", "sohu", "solar", "solutions", "sony", "soy", "space",
       "spiegel", "spreadbetting", "srl", "starhub", "statoil", "studio", "study", "style", "sucks", "supplies",
       "supply", "support", "surf", "surgery", "suzuki", "swatch", "swiss", "sydney", "systems", "taipei", "tatamotors",
       "tatar", "tattoo", "tax", "taxi", "team", "tech", "technology", "tel", "telefonica", "temasek", "tennis", "thd",
       "theater", "tickets", "tienda", "tips", "tires", "tirol", "today", "tokyo", "tools", "top", "toray", "toshiba",
       "tours", "town", "toyota", "toys", "trade", "trading", "training", "travel", "trust", "tui", "ubs", "university",
       "uno", "uol", "vacations", "vegas", "ventures", "vermögensberater", "vermögensberatung", "versicherung", "vet",
       "viajes", "video", "villas", "vin", "vision", "vista", "vistaprint", "vlaanderen", "vodka", "vote", "voting",
       "voto", "voyage", "wales", "walter", "wang", "watch", "webcam", "website", "wed", "wedding", "weir", "whoswho",
       "wien", "wiki", "williamhill", "win", "windows", "wine", "wme", "work", "works", "world", "wtc", "wtf", "xbox",
       "xerox", "xin", "xperia", "xxx", "xyz", "yachts", "yandex", "yodobashi", "yoga", "yokohama", "youtube", "zip",
       "zone", "zuerich", "дети", "ком", "москва", "онлайн", "орг", "рус", "сайт", "קום", "بازار", "شبكة", "كوم",
       "موقع", "कॉम", "नेट", "संगठन", "คอม", "みんな", "グーグル", "コム", "世界", "中信", "中文网", "企业", "佛山",
       "信息", "健康", "八卦", "公司", "公益", "商城", "商店", "商标", "在线", "大拿", "娱乐", "工行", "广东", "慈善",
       "我爱你", "手机", "政务", "政府", "新闻", "时尚", "机构", "淡马锡", "游戏", "点看", "移动", "组织机构", "网址",
       "网店", "网络", "谷歌", "集团", "飞利浦", "餐厅", "닷넷", "닷컴", "삼성", "onion", "ac", "ad", "ae", "af", "ag",
       "ai", "al", "am", "an", "ao", "aq", "ar", "as", "at", "au", "aw", "ax", "az", "ba", "bb", "bd", "be", "bf", "bg",
       "bh", "bi", "bj", "bl", "bm", "bn", "bo", "bq", "br", "bs", "bt", "bv", "bw", "by", "bz", "ca", "cc", "cd", "cf",
       "cg", "ch", "ci", "ck", "cl", "cm", "cn", "co", "cr", "cu", "cv", "cw", "cx", "cy", "cz", "de", "dj", "dk", "dm",
       "do", "dz", "ec", "ee", "eg", "eh", "er", "es", "et", "eu", "fi", "fj", "fk", "fm", "fo", "fr", "ga", "gb", "gd",
       "ge", "gf", "gg", "gh", "gi", "gl", "gm", "gn", "gp", "gq", "gr", "gs", "gt", "gu", "gw", "gy", "hk", "hm", "hn",
       "hr", "ht", "hu", "id", "ie", "il", "im", "in", "io", "iq", "ir", "is", "it", "je", "jm", "jo", "jp", "ke", "kg",
       "kh", "ki", "km", "kn", "kp", "kr", "kw", "ky", "kz", "la", "lb", "lc", "li", "lk", "lr", "ls", "lt", "lu", "lv",
       "ly", "ma", "mc", "md", "me", "mf", "mg", "mh", "mk", "ml", "mm", "mn", "mo", "mp", "mq", "mr", "ms", "mt", "mu",
       "mv", "mw", "mx", "my", "mz", "na", "nc", "ne", "nf", "ng", "ni", "nl", "no", "np", "nr", "nu", "nz", "om", "pa",
       "pe", "pf", "pg", "ph", "pk", "pl", "pm", "pn", "pr", "ps", "pt", "pw", "py", "qa", "re", "ro", "rs", "ru", "rw",
       "sa", "sb", "sc", "sd", "se", "sg", "sh", "si", "sj", "sk", "sl", "sm", "sn", "so", "sr", "ss", "st", "su", "sv",
       "sx", "sy", "sz", "tc", "td", "tf", "tg", "th", "tj", "tk", "tl", "tm", "tn", "to", "tp", "tr", "tt", "tv", "tw",
       "tz", "ua", "ug", "uk", "um", "us", "uy", "uz", "va", "vc", "ve", "vg", "vi", "vn", "vu", "wf", "ws", "ye", "yt",
       "za", "zm", "zw", "ελ", "бел", "мкд", "мон", "рф", "срб", "укр", "қаз", "հայ", "الاردن", "الجزائر", "السعودية",
       "المغرب", "امارات", "ایران", "بھارت", "تونس", "سودان", "سورية", "عراق", "عمان", "فلسطين", "قطر", "مصر", "مليسيا",
       "پاکستان", "भारत", "বাংলা", "ভারত", "ਭਾਰਤ", "ભારત", "இந்தியா", "இலங்கை", "சிங்கப்பூர்", "భారత్", "ලංකා", "ไทย", "გე",
       "中国", "中國", "台湾", "台灣", "新加坡", "澳門", "香港",
       // comment for clang-format to prevent him from placing all strings on separate lines
       "한국"});
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
  static const std::unordered_set<Slice, SliceHash> valid_usernames{
      "ya", "gif", "wiki", "vid", "bing", "pic", "bold", "imdb", "coub", "like", "vote", "giff", "cap"};
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

vector<std::pair<Slice, bool>> find_urls(Slice str) {
  vector<std::pair<Slice, bool>> result;
  for (auto url : match_urls(str)) {
    if (is_email_address(url)) {
      result.emplace_back(url, true);
    } else {
      url = fix_url(url);
      if (!url.empty()) {
        result.emplace_back(url, false);
      }
    }
  }
  return result;
}

void fix_entities(vector<MessageEntity> &entities) {
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
  }

  if (!skip_bot_commands && !only_urls) {
    auto bot_commands = find_bot_commands(text);
    for (auto &bot_command : bot_commands) {
      entities.emplace_back(MessageEntity::Type::BotCommand, narrow_cast<int32>(bot_command.begin() - text.begin()),
                            narrow_cast<int32>(bot_command.size()));
    }
  }

  if (!only_urls) {
    auto hashtags = find_hashtags(text);
    for (auto &hashtag : hashtags) {
      entities.emplace_back(MessageEntity::Type::Hashtag, narrow_cast<int32>(hashtag.begin() - text.begin()),
                            narrow_cast<int32>(hashtag.size()));
    }
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

vector<MessageEntity> merge_entities(vector<MessageEntity> old_entities, vector<MessageEntity> new_entities) {
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

string get_first_url(Slice text, const vector<MessageEntity> &entities) {
  for (auto &entity : entities) {
    switch (entity.type) {
      case MessageEntity::Type::Mention:
        break;
      case MessageEntity::Type::Hashtag:
        break;
      case MessageEntity::Type::BotCommand:
        break;
      case MessageEntity::Type::Url:
        return utf8_utf16_substr(text, entity.offset, entity.length).str();
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
        return entity.argument;
      case MessageEntity::Type::MentionName:
        break;
      default:
        UNREACHABLE();
    }
  }

  return string();
}

static UserId get_link_user_id(Slice url) {
  auto lower_cased_url = to_lower(url);
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
      return UserId(to_integer<int32>(value));
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
            auto r_http_url = parse_url(url);
            if (r_http_url.is_ok() && url.find('.') != string::npos) {
              entities.emplace_back(MessageEntity::Type::TextUrl, utf16_offset, utf16_entity_length,
                                    r_http_url.ok().get_url());
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
          auto r_http_url = parse_url(url);
          if (r_http_url.is_ok() && url.find('.') != string::npos) {
            entities.emplace_back(MessageEntity::Type::TextUrl, utf16_offset, utf16_entity_length,
                                  r_http_url.ok().get_url());
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
                                                                              const vector<MessageEntity> &entities) {
  vector<tl_object_ptr<telegram_api::MessageEntity>> result;
  for (auto &entity : entities) {
    switch (entity.type) {
      case MessageEntity::Type::Mention:
      case MessageEntity::Type::Hashtag:
      case MessageEntity::Type::BotCommand:
      case MessageEntity::Type::Url:
      case MessageEntity::Type::EmailAddress:
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
        CHECK(input_user != nullptr);
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
        return Status::Error(400, "EntityMention can't be used in outgoing messages");
      case td_api::textEntityTypeHashtag::ID:
        return Status::Error(400, "EntityHashtag can't be used in outgoing messages");
      case td_api::textEntityTypeBotCommand::ID:
        return Status::Error(400, "EntityBotCommand can't be used in outgoing messages");
      case td_api::textEntityTypeUrl::ID:
        return Status::Error(400, "EntityUrl can't be used in outgoing messages");
      case td_api::textEntityTypeEmailAddress::ID:
        return Status::Error(400, "EntityEmailAddress can't be used in outgoing messages");
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
        auto r_http_url = parse_url(entity_text_url->url_);
        if (r_http_url.is_error()) {
          return Status::Error(400, PSTRING() << "Wrong message entity: " << r_http_url.error().message());
        }
        entities.emplace_back(MessageEntity::Type::TextUrl, entity->offset_, entity->length_,
                              r_http_url.ok().get_url());
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
                                           vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities) {
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
        auto r_http_url = parse_url(entity_text_url->url_);
        if (r_http_url.is_error()) {
          LOG(ERROR) << "Wrong URL entity: \"" << entity_text_url->url_ << "\": " << r_http_url.error().message();
          continue;
        }
        entities.emplace_back(MessageEntity::Type::TextUrl, entity_text_url->offset_, entity_text_url->length_,
                              r_http_url.ok().get_url());
        break;
      }
      case telegram_api::messageEntityMentionName::ID: {
        auto entity_mention_name = static_cast<const telegram_api::messageEntityMentionName *>(entity.get());
        UserId user_id(entity_mention_name->user_id_);
        if (!user_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << user_id << " in MentionName";
          continue;
        }
        if (!contacts_manager->have_user(user_id)) {
          LOG(ERROR) << "Receive unknown " << user_id << " in MentionName";
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
        auto r_http_url = parse_url(entity_text_url->url_);
        if (r_http_url.is_error()) {
          LOG(WARNING) << "Wrong URL entity: \"" << entity_text_url->url_ << "\": " << r_http_url.error().message();
          continue;
        }
        entities.emplace_back(MessageEntity::Type::TextUrl, entity_text_url->offset_, entity_text_url->length_,
                              r_http_url.ok().get_url());
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

}  // namespace td
