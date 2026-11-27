//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ExportHtmlWriter.h"

#include "td/telegram/CountryInfoManager.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/ExportHtmlAssets.h"
#include "td/telegram/ExportRichMessage.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/base64.h"
#include "td/utils/crypto.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/unicode.h"
#include "td/utils/utf8.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <locale>
#include <map>
#include <sstream>
#include <utility>

namespace td {

static constexpr int32 MESSAGES_IN_PAGE = 1000;
static constexpr int32 HISTORY_USERPIC_SIZE = 42;
static constexpr int32 REACTION_USERPIC_SIZE = 20;
static constexpr int32 JOIN_WITHIN_SECONDS = 900;
static constexpr int32 STICKER_MAX_SIZE = 384;
static constexpr int32 THUMBNAIL_MIN_SIZE = 80;
static constexpr int32 SAVED_MESSAGES_COLOR_INDEX = 3;
static constexpr size_t MAX_LINK_TARGET_SIZE = 4096;

static const char UNAVAILABLE_CUSTOM_EMOJI[] = "(unavailable)";

static void append_html_escaped(string &result, Slice value) {
  result.reserve(result.size() + value.size());
  for (size_t i = 0; i < value.size(); i++) {
    auto c = static_cast<unsigned char>(value[i]);
    switch (c) {
      case '\n':
        result += "<br>";
        break;
      case '"':
        result += "&quot;";
        break;
      case '&':
        result += "&amp;";
        break;
      case '\'':
        result += "&apos;";
        break;
      case '<':
        result += "&lt;";
        break;
      case '>':
        result += "&gt;";
        break;
      default:
        if (c < 32) {
          static const char *hex = "0123456789ABCDEF";
          result += "&#x";
          result += hex[c >> 4];
          result += hex[c & 15];
          result += ';';
        } else if (c == 0xE2 && i + 2 < value.size() && static_cast<unsigned char>(value[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(value[i + 2]) == 0xA8 ||
                    static_cast<unsigned char>(value[i + 2]) == 0xA9)) {
          // U+2028 LINE SEPARATOR and U+2029 PARAGRAPH SEPARATOR
          result += "<br>";
          i += 2;
        } else {
          result += static_cast<char>(c);
        }
    }
  }
}

static string html_escape(Slice value) {
  string result;
  append_html_escaped(result, value);
  return result;
}

static string join_html_list(const vector<string> &values) {
  string result;
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) {
      result += i + 1 == values.size() ? " and " : ", ";
    }
    result += values[i];
  }
  return result;
}

static string join_strings(const vector<string> &values, Slice separator) {
  string result;
  for (size_t i = 0; i < values.size(); i++) {
    if (i > 0) {
      result.append(separator.data(), separator.size());
    }
    result += values[i];
  }
  return result;
}

// printf uses the decimal point of the C locale, which the application can change
static string format_double(double value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << value;
  return stream.str();
}

static string format_coordinate(double value) {
  return PSTRING() << StringBuilder::FixedDouble(value, 6);
}

static void append_two_digits(string &result, int64 value) {
  result += static_cast<char>('0' + value / 10);
  result += static_cast<char>('0' + value % 10);
}

static Slice get_month_name(int64 month) {
  static const char *names[] = {"January", "February", "March",     "April",   "May",      "June",
                                "July",    "August",   "September", "October", "November", "December"};
  if (month < 1 || month > 12) {
    return Slice("Unknown");
  }
  return Slice(names[month - 1]);
}

// "24 November 2026"
static string format_date_text(int64 local_time) {
  auto date_time = get_export_date_time(local_time);
  return PSTRING() << date_time.day << ' ' << get_month_name(date_time.month) << ' ' << date_time.year;
}

// "21:05"
static string format_time_text(int64 local_time) {
  auto date_time = get_export_date_time(local_time);
  string result;
  append_two_digits(result, date_time.seconds / 3600);
  result += ':';
  append_two_digits(result, date_time.seconds / 60 % 60);
  return result;
}

// "24.11.2026 21:05:00", followed by " UTC+03:00" if the time zone is needed
static string format_date_time(int32 date, int32 utc_time_offset, bool with_time_zone) {
  if (date == 0) {
    return string();
  }
  auto date_time = get_export_date_time(get_export_local_time(date, utc_time_offset));
  string result;
  append_two_digits(result, date_time.day);
  result += '.';
  append_two_digits(result, date_time.month);
  result += '.';
  result += to_string(date_time.year);
  result += ' ';
  append_two_digits(result, date_time.seconds / 3600);
  result += ':';
  append_two_digits(result, date_time.seconds / 60 % 60);
  result += ':';
  append_two_digits(result, date_time.seconds % 60);
  if (with_time_zone) {
    auto offset = get_export_utc_time_offset(date, utc_time_offset);
    result += " UTC";
    result += offset < 0 ? '-' : '+';
    offset = std::abs(offset);
    append_two_digits(result, offset / 3600);
    result += ':';
    append_two_digits(result, offset / 60 % 60);
  }
  return result;
}

static int64 get_local_day(int32 date, int32 utc_time_offset) {
  auto local_time = get_export_local_time(date, utc_time_offset);
  return local_time >= 0 ? local_time / 86400 : (local_time - 86399) / 86400;
}

// "1:05:09" or "05:09"
static string format_duration(int64 duration) {
  auto hours = duration / 3600;
  auto minutes = duration % 3600 / 60;
  auto seconds = duration % 60;
  string result;
  if (hours != 0) {
    result += to_string(hours);
    result += ':';
  }
  if (minutes < 10) {
    result += '0';
  }
  result += to_string(minutes);
  result += ':';
  if (seconds < 10) {
    result += '0';
  }
  result += to_string(seconds);
  return result;
}

static string format_file_size(int64 size) {
  if (size >= 1024 * 1024) {
    auto tenths = size * 10 / (1024 * 1024);
    return PSTRING() << tenths / 10 << '.' << tenths % 10 << " MB";
  }
  if (size >= 1024) {
    auto tenths = size * 10 / 1024;
    return PSTRING() << tenths / 10 << '.' << tenths % 10 << " KB";
  }
  return PSTRING() << size << " B";
}

static string format_grams_amount(int64 nanos) {
  auto absolute = nanos < 0 ? ~static_cast<uint64>(nanos) + 1 : static_cast<uint64>(nanos);
  auto whole = absolute / EXPORT_NANOS_IN_GRAM;
  auto fraction = absolute % EXPORT_NANOS_IN_GRAM;
  string result = nanos < 0 ? "-" : "";
  result += to_string(whole);
  if (fraction != 0) {
    string digits = to_string(fraction);
    digits.insert(0, 9 - digits.size(), '0');
    while (digits.back() == '0') {
      digits.pop_back();
    }
    result += '.';
    result += digits;
  }
  result += absolute == static_cast<uint64>(EXPORT_NANOS_IN_GRAM) ? " Gram" : " Grams";
  return result;
}

// the size of an image, which is shown at a half of it, or an empty size if the image is too small
struct ExportHtmlImageSize {
  int32 width = 0;
  int32 height = 0;

  bool empty() const {
    return width == 0 || height == 0;
  }
};

static ExportHtmlImageSize get_thumbnail_size(int32 width, int32 height, int32 max_size, bool expand_for_retina) {
  if (width <= 0 || height <= 0) {
    return {};
  }
  int64 multiplier = expand_for_retina ? 2 : 1;
  int64 result_width = width;
  int64 result_height = height;
  if (result_width * multiplier > max_size || result_height * multiplier > max_size) {
    auto scaled_width = max_size * result_width / result_height;
    if (scaled_width <= max_size) {
      result_width = scaled_width;
      result_height = max_size;
    } else {
      result_height = max_size * result_height / result_width;
      result_width = max_size;
    }
  }
  result_width &= ~static_cast<int64>(1);
  result_height &= ~static_cast<int64>(1);
  if (result_width < THUMBNAIL_MIN_SIZE || result_height < THUMBNAIL_MIN_SIZE) {
    return {};
  }
  return {static_cast<int32>(result_width), static_cast<int32>(result_height)};
}

static string get_size_style(int32 width, int32 height) {
  return PSTRING() << "width: " << width << "px; height: " << height << "px";
}

static int32 get_peer_color_index(uint64 bare_id) {
  static const int32 color_indices[] = {0, 7, 4, 1, 6, 3, 5};
  return color_indices[bare_id % 7];
}

static Slice get_no_file_description(ExportFile::SkipReason skip_reason) {
  switch (skip_reason) {
    case ExportFile::SkipReason::Unavailable:
      return Slice("Unavailable, please try again later.");
    case ExportFile::SkipReason::FileSize:
      return Slice("Exceeds maximum size, change data exporting settings to download.");
    case ExportFile::SkipReason::FileType:
      return Slice("Not included, change data exporting settings to download.");
    case ExportFile::SkipReason::None:
      return Slice();
    default:
      UNREACHABLE();
      return Slice();
  }
}

static bool is_global_link(Slice link) {
  auto lower = to_lower(link.substr(0, 8));
  return begins_with(lower, "http://") || begins_with(lower, "https://");
}

static int32 get_hex_value(char c) {
  if ('0' <= c && c <= '9') {
    return c - '0';
  }
  if ('a' <= c && c <= 'f') {
    return c - 'a' + 10;
  }
  if ('A' <= c && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

static bool has_encoded_control(Slice value) {
  for (size_t i = 0; i + 2 < value.size(); i++) {
    if (value[i] != '%') {
      continue;
    }
    auto high = get_hex_value(value[i + 1]);
    auto low = get_hex_value(value[i + 2]);
    if (high < 0 || low < 0) {
      continue;
    }
    auto decoded = high * 16 + low;
    if (decoded < 32 || decoded == 127) {
      return true;
    }
  }
  return false;
}

static bool is_strict_link_target(Slice value) {
  if (value.empty() || value.size() > MAX_LINK_TARGET_SIZE || !check_utf8(value)) {
    return false;
  }
  if (is_space(value[0]) || is_space(value.back())) {
    return false;
  }
  const auto *ptr = value.ubegin();
  const auto *end = value.uend();
  while (ptr != end) {
    uint32 code = 0;
    ptr = next_utf8_unsafe(ptr, &code);
    // control characters, U+2028 LINE SEPARATOR and U+2029 PARAGRAPH SEPARATOR
    if (code < 32 || (127 <= code && code < 160) || code == 0x2028 || code == 0x2029) {
      return false;
    }
  }
  return true;
}

// returns the link if it can be safely opened from the exported page, or an empty string otherwise
static string get_safe_link(Slice value) {
  if (!is_strict_link_target(value) || begins_with(value, "//") || has_encoded_control(value)) {
    return string();
  }
  auto lower = to_lower(value);
  if (begins_with(lower, "mailto:") || begins_with(lower, "tel:")) {
    return value.str();
  }
  auto r_link = LinkManager::check_link(value);
  if (r_link.is_error()) {
    return string();
  }
  auto link = r_link.move_as_ok();
  lower = to_lower(link);
  if (!begins_with(lower, "http://") && !begins_with(lower, "https://") && !begins_with(lower, "tg:")) {
    return string();
  }
  if (!is_strict_link_target(link) || has_encoded_control(link)) {
    return string();
  }
  return link;
}

static string format_text_link(const string &text, Slice target) {
  auto href = get_safe_link(target);
  if (href.empty()) {
    return text;
  }
  string result = "<a href=\"";
  append_html_escaped(result, href);
  result += "\">";
  result += text;
  result += "</a>";
  return result;
}

static string format_custom_emoji(Slice path, Slice text, Slice base) {
  string result;
  if (path.empty()) {
    result = "<a href=\"\" onclick=\"return ShowNotLoadedEmoji();\">";
  } else if (path == Slice(UNAVAILABLE_CUSTOM_EMOJI)) {
    result = "<a href=\"\" onclick=\"return ShowNotAvailableEmoji();\">";
  } else {
    result = "<a href = \"";
    append_html_escaped(result, PSLICE() << base << path);
    result += "\">";
  }
  result.append(text.data(), text.size());
  result += "</a>";
  return result;
}

static string format_text(Slice text, const vector<td_api::object_ptr<td_api::textEntity>> *entities,
                          const ExportCustomEmojiFiles *custom_emoji_files, Slice base) {
  string result;
  for (const auto &part : get_export_text_parts(text, entities)) {
    auto escaped = html_escape(part.text);
    if (part.type == nullptr) {
      result += escaped;
      continue;
    }
    auto escaped_tail = escaped.empty() ? string() : escaped.substr(1);
    auto quoted_tail = PSTRING() << '"' << escaped_tail << '"';
    switch (part.type->get_id()) {
      case td_api::textEntityTypeMention::ID:
        result += PSTRING() << "<a href=\"" << LinkManager::get_t_me_url() << escaped_tail << "\">" << escaped
                            << "</a>";
        break;
      case td_api::textEntityTypeHashtag::ID:
        result += PSTRING() << "<a href=\"\" onclick=\"return ShowHashtag(" << html_escape(quoted_tail) << ")\">"
                            << escaped << "</a>";
        break;
      case td_api::textEntityTypeBotCommand::ID:
        result += PSTRING() << "<a href=\"\" onclick=\"return ShowBotCommand(" << html_escape(quoted_tail) << ")\">"
                            << escaped << "</a>";
        break;
      case td_api::textEntityTypeCashtag::ID:
        result += PSTRING() << "<a href=\"\" onclick=\"return ShowCashtag(" << html_escape(quoted_tail) << ")\">"
                            << escaped << "</a>";
        break;
      case td_api::textEntityTypeUrl::ID:
        result += format_text_link(escaped, part.text);
        break;
      case td_api::textEntityTypeTextUrl::ID:
        result += format_text_link(escaped, static_cast<const td_api::textEntityTypeTextUrl *>(part.type)->url_);
        break;
      case td_api::textEntityTypeEmailAddress::ID:
        result += PSTRING() << "<a href=\"mailto:" << escaped << "\">" << escaped << "</a>";
        break;
      case td_api::textEntityTypePhoneNumber::ID:
        result += PSTRING() << "<a href=\"tel:" << escaped << "\">" << escaped << "</a>";
        break;
      case td_api::textEntityTypeMentionName::ID:
        result += PSTRING() << "<a href=\"\" onclick=\"return ShowMentionName()\">" << escaped << "</a>";
        break;
      case td_api::textEntityTypeBold::ID:
        result += PSTRING() << "<strong>" << escaped << "</strong>";
        break;
      case td_api::textEntityTypeItalic::ID:
        result += PSTRING() << "<em>" << escaped << "</em>";
        break;
      case td_api::textEntityTypeUnderline::ID:
        result += PSTRING() << "<u>" << escaped << "</u>";
        break;
      case td_api::textEntityTypeStrikethrough::ID:
        result += PSTRING() << "<s>" << escaped << "</s>";
        break;
      case td_api::textEntityTypeCode::ID:
        result += PSTRING() << "<code>" << escaped << "</code>";
        break;
      case td_api::textEntityTypePre::ID:
      case td_api::textEntityTypePreCode::ID:
        result += PSTRING() << "<pre>" << escaped << "</pre>";
        break;
      case td_api::textEntityTypeBlockQuote::ID:
      case td_api::textEntityTypeExpandableBlockQuote::ID:
        result += PSTRING() << "<blockquote>" << escaped << "</blockquote>";
        break;
      case td_api::textEntityTypeSpoiler::ID:
        result +=
            PSTRING() << "<span class=\"spoiler hidden\" onclick=\"ShowSpoiler(this)\"><span aria-hidden=\"true\">"
                      << escaped << "</span></span>";
        break;
      case td_api::textEntityTypeCustomEmoji::ID:
        if (custom_emoji_files == nullptr) {
          // the custom emoji of the text weren't exported, so there is nothing to link to
          result += escaped;
          break;
        }
        result += format_custom_emoji(
            get_export_custom_emoji_path(
                custom_emoji_files,
                static_cast<const td_api::textEntityTypeCustomEmoji *>(part.type)->custom_emoji_id_),
            escaped, base);
        break;
      default:
        result += escaped;
        break;
    }
  }
  return result;
}

static string format_text(const td_api::formattedText *text, const ExportCustomEmojiFiles *custom_emoji_files,
                          Slice base) {
  if (text == nullptr) {
    return string();
  }
  return format_text(text->text_, &text->entities_, custom_emoji_files, base);
}

static string get_initial(Slice name) {
  while (!name.empty() && is_space(name[0])) {
    name.remove_prefix(1);
  }
  if (name.empty() || !check_utf8(name)) {
    return string();
  }
  uint32 code = 0;
  const auto *end = next_utf8_unsafe(name.ubegin(), &code);
  return html_escape(name.substr(0, static_cast<size_t>(end - name.ubegin())));
}

static string get_export_html_page_name(int32 index) {
  if (index == 0) {
    return "messages.html";
  }
  return PSTRING() << "messages" << index + 1 << ".html";
}

namespace {

struct HtmlAttribute {
  Slice name;
  Slice value;
};

struct UserpicData {
  int32 color_index = 0;
  int32 pixel_size = 0;
  string image_link;
  string large_link;
  string first_name;
  string last_name;
  string tooltip;
};

struct MediaData {
  string title;
  string description;
  string status;
  string classes;
  string thumbnail;
  string link;
};

// the attributes of a tag, sorted by name like the attributes of push_tag
using HtmlAttributes = std::map<string, string>;

// the stack of the open tags of a page, which decides the indentation
class HtmlContext {
 public:
  // the attributes named "inline" and "empty" are flags for an inline tag and a tag without content
  void push_tag(string &result, Slice tag, std::initializer_list<HtmlAttribute> attributes = {}) {
    CHECK(attributes.size() <= 8);
    HtmlAttribute sorted[8];
    size_t count = 0;
    for (const auto &attribute : attributes) {
      if (attribute.name.empty()) {
        continue;
      }
      auto position = count++;
      while (position > 0 && attribute.name < sorted[position - 1].name) {
        sorted[position] = sorted[position - 1];
        position--;
      }
      sorted[position] = attribute;
    }

    bool is_block = true;
    for (size_t i = 0; i < count; i++) {
      if (sorted[i].name == "inline") {
        is_block = false;
      }
    }
    open_tag(result, tag, is_block);
    bool is_empty = false;
    for (size_t i = 0; i < count; i++) {
      if (sorted[i].name == "inline") {
        continue;
      }
      if (sorted[i].name == "empty") {
        is_empty = true;
        continue;
      }
      append_attribute(result, sorted[i].name, sorted[i].value);
    }
    close_tag(result, tag, is_block, is_empty);
  }

  void push_tag(string &result, Slice tag, const HtmlAttributes &attributes) {
    bool is_block = attributes.count("inline") == 0;
    open_tag(result, tag, is_block);
    bool is_empty = false;
    for (const auto &attribute : attributes) {
      if (attribute.first.empty() || attribute.first == "inline") {
        continue;
      }
      if (attribute.first == "empty") {
        is_empty = true;
        continue;
      }
      append_attribute(result, attribute.first, attribute.second);
    }
    close_tag(result, tag, is_block, is_empty);
  }

  void pop_tag(string &result) {
    CHECK(!tags_.empty());
    auto data = tags_.back();
    tags_.pop_back();
    if (data.is_block) {
      result += '\n';
      append_indent(result);
    }
    result += "</";
    result.append(data.name.data(), data.name.size());
    result += '>';
    if (data.is_block) {
      result += '\n';
    }
  }

  void push_div(string &result, Slice class_name, Slice style = Slice()) {
    if (style.empty()) {
      push_tag(result, "div", {{"class", class_name}});
    } else {
      push_tag(result, "div", {{"class", class_name}, {"style", style}});
    }
  }

  bool empty() const {
    return tags_.empty();
  }

 private:
  struct Tag {
    Slice name;
    bool is_block = true;
  };
  vector<Tag> tags_;

  void append_indent(string &result) const {
    result.append(tags_.size(), ' ');
  }

  void open_tag(string &result, Slice tag, bool is_block) const {
    if (is_block) {
      result += '\n';
      append_indent(result);
    }
    result += '<';
    result.append(tag.data(), tag.size());
  }

  static void append_attribute(string &result, Slice name, Slice value) {
    result += ' ';
    result.append(name.data(), name.size());
    result += "=\"";
    append_html_escaped(result, value);
    result += '"';
  }

  // the name of the tag isn't copied, so it must stay valid until the tag is popped
  void close_tag(string &result, Slice tag, bool is_block, bool is_empty) {
    if (is_empty) {
      result += '/';
    }
    result += '>';
    if (is_block) {
      result += '\n';
    }
    if (!is_empty) {
      tags_.push_back(Tag{tag, is_block});
    }
  }
};

}  // namespace

static const char CUSTOM_STYLESHEET_PATH[] = "css/custom.css";

class ExportHtmlPage {
 public:
  ExportHtmlPage(string path, Slice relative_path, bool has_custom_stylesheet, ExportStats *stats)
      : file_(std::move(path), stats) {
    for (auto c : relative_path) {
      if (c == '/') {
        base_ += "../";
      }
    }
    compose_start(has_custom_stylesheet);
  }

  HtmlContext &context() {
    return context_;
  }

  // the prefix of paths relative to the export directory
  Slice base() const {
    return base_;
  }

  Status write(Slice block) {
    CHECK(!is_closed_);
    Status status;
    if (block.empty() || has_data_) {
      status = file_.write(block);
    } else {
      status = file_.write(PSLICE() << start_ << block);
    }
    has_data_ = true;
    if (status.is_error()) {
      is_closed_ = true;
    }
    return status;
  }

  Status close() {
    if (is_closed_) {
      return Status::OK();
    }
    is_closed_ = true;
    if (!has_data_) {
      return Status::OK();
    }
    string block;
    while (!context_.empty()) {
      context_.pop_tag(block);
    }
    return file_.write(block);
  }

  // the back path is relative to the export directory
  void push_header(string &result, Slice header, Slice back_path = Slice()) {
    context_.push_div(result, "page_header");
    if (back_path.empty()) {
      context_.push_div(result, "content");
    } else {
      context_.push_tag(result, "a",
                        {{"class", "content block_link"},
                         {"href", PSLICE() << base_ << back_path},
                         {"onclick", "return GoBack(this)"}});
    }
    context_.push_div(result, "text bold");
    append_html_escaped(result, header);
    context_.pop_tag(result);
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  void push_about(string &result, Slice text, bool with_divider = false) {
    context_.push_div(result, with_divider ? Slice("page_about details with_divider") : Slice("page_about details"));
    append_html_escaped(result, text);
    context_.pop_tag(result);
  }

  // the link is relative to the export directory
  void push_section(string &result, Slice header, Slice type, int32 count, Slice link) {
    context_.push_tag(
        result, "a",
        {{"class", PSLICE() << "section block_link " << type}, {"href", PSLICE() << base_ << link << "#allow_back"}});
    context_.push_div(result, "counter details");
    result += to_string(count);
    context_.pop_tag(result);
    context_.push_div(result, "label bold");
    append_html_escaped(result, header);
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  // the link is relative to the export directory; the entry isn't a link if it is empty
  void push_list_entry(string &result, Slice link, const UserpicData &userpic, Slice name, Slice subname,
                       std::initializer_list<Slice> details, Slice info);

  // the text is in HTML already
  void push_story_entry(string &result, Slice link, Slice image_link, Slice name, Slice text,
                        const vector<string> &details, Slice info);

  void push_audio_entry(string &result, Slice link, Slice name, Slice details, Slice duration);

 private:
  void push_linked_name(string &result, Slice link, Slice name);
  ExportOutputFile file_;
  HtmlContext context_;
  string base_;
  string start_;
  bool has_data_ = false;
  bool is_closed_ = false;

  void compose_start(bool has_custom_stylesheet) {
    auto css = PSTRING() << base_ << "css/style.css";
    auto js = PSTRING() << base_ << "js/script.js";
    start_ = "<!DOCTYPE html>";
    context_.push_tag(start_, "html");
    context_.push_tag(start_, "head");
    context_.push_tag(start_, "meta", {{"charset", "utf-8"}, {"empty", ""}});
    context_.push_tag(start_, "title", {{"inline", ""}});
    start_ += "Exported Data";
    context_.pop_tag(start_);
    context_.push_tag(start_, "meta",
                      {{"name", "viewport"}, {"content", "width=device-width, initial-scale=1.0"}, {"empty", ""}});
    context_.push_tag(start_, "link", {{"href", css}, {"rel", "stylesheet"}, {"empty", ""}});
    if (has_custom_stylesheet) {
      auto custom_css = PSTRING() << base_ << CUSTOM_STYLESHEET_PATH;
      context_.push_tag(start_, "link", {{"href", custom_css}, {"rel", "stylesheet"}, {"empty", ""}});
    }
    context_.push_tag(start_, "script", {{"src", js}, {"type", "text/javascript"}});
    context_.pop_tag(start_);
    context_.pop_tag(start_);
    context_.push_tag(start_, "body", {{"onload", "CheckLocation();"}});
    context_.push_div(start_, "page_wrap");
  }
};

struct ExportHtmlMessageInfo {
  bool is_service = true;
  int32 id = 0;
  DialogId sender_dialog_id;
  int64 via_bot_user_id = 0;
  int32 date = 0;
  DialogId forwarded_from_dialog_id;
  string forwarded_from_name;
  bool is_forwarded = false;
  bool show_forwarded_as_original = false;
  int32 forwarded_date = 0;
};

static void push_userpic(HtmlContext &context, string &result, Slice base, const UserpicData &userpic) {
  auto size = PSTRING() << userpic.pixel_size << "px";
  if (!userpic.large_link.empty()) {
    context.push_tag(result, "a", {{"class", "userpic_link"}, {"href", PSLICE() << base << userpic.large_link}});
  }
  auto size_style = PSTRING() << "width: " << size << "; height: " << size;
  if (!userpic.image_link.empty()) {
    context.push_tag(
        result, "img",
        {{"class", "userpic"}, {"style", size_style}, {"src", PSLICE() << base << userpic.image_link}, {"empty", ""}});
  } else {
    auto line_height = PSTRING() << "line-height: " << size;
    context.push_tag(result, "div",
                     {{"class", PSLICE() << "userpic userpic" << userpic.color_index + 1}, {"style", size_style}});
    if (userpic.tooltip.empty()) {
      context.push_div(result, "initials", line_height);
    } else {
      context.push_tag(result, "div", {{"class", "initials"}, {"style", line_height}, {"title", userpic.tooltip}});
    }
    result += get_initial(userpic.first_name);
    result += get_initial(userpic.last_name);
    context.pop_tag(result);
    context.pop_tag(result);
  }
  if (!userpic.large_link.empty()) {
    context.pop_tag(result);
  }
}

void ExportHtmlPage::push_list_entry(string &result, Slice link, const UserpicData &userpic, Slice name, Slice subname,
                                     std::initializer_list<Slice> details, Slice info) {
  if (link.empty()) {
    context_.push_div(result, "entry clearfix");
  } else {
    context_.push_tag(result, "a",
                      {{"class", "entry block_link clearfix"}, {"href", PSLICE() << base_ << link << "#allow_back"}});
  }
  context_.push_div(result, "pull_left userpic_wrap");
  push_userpic(context_, result, base_, userpic);
  context_.pop_tag(result);
  context_.push_div(result, "body");
  auto push_text = [&](Slice class_name, Slice text) {
    if (text.empty()) {
      return;
    }
    context_.push_div(result, class_name);
    append_html_escaped(result, text);
    context_.pop_tag(result);
  };
  push_text("pull_right info details", info);
  push_text("name bold", name);
  push_text("subname bold", subname);
  for (auto detail : details) {
    // empty details are written, unlike the other parts
    context_.push_div(result, "details_entry details");
    append_html_escaped(result, detail);
    context_.pop_tag(result);
  }
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlPage::push_story_entry(string &result, Slice link, Slice image_link, Slice name, Slice text,
                                      const vector<string> &details, Slice info) {
  context_.push_div(result, "entry clearfix");
  if (link.empty()) {
    context_.push_div(result, "pull_left userpic_wrap");
  } else {
    context_.push_tag(result, "a",
                      {{"class", "pull_left userpic_wrap"}, {"href", PSLICE() << base_ << link << "#allow_back"}});
  }
  if (!image_link.empty()) {
    context_.push_tag(result, "img",
                      {{"class", "story"},
                       {"style", get_size_style(EXPORT_HTML_STORY_WIDTH, EXPORT_HTML_STORY_HEIGHT)},
                       {"src", PSLICE() << base_ << image_link},
                       {"empty", ""}});
  }
  context_.pop_tag(result);
  context_.push_div(result, "body");
  if (!info.empty()) {
    context_.push_div(result, "pull_right info details");
    append_html_escaped(result, info);
    context_.pop_tag(result);
  }
  push_linked_name(result, link, name);
  if (!text.empty()) {
    context_.push_div(result, "text");
    result.append(text.data(), text.size());
    context_.pop_tag(result);
  }
  for (const auto &detail : details) {
    context_.push_div(result, "details_entry details");
    append_html_escaped(result, detail);
    context_.pop_tag(result);
  }
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlPage::push_audio_entry(string &result, Slice link, Slice name, Slice details, Slice duration) {
  context_.push_div(result, "entry clearfix");
  if (link.empty()) {
    context_.push_div(result, "pull_left userpic_wrap");
  } else {
    context_.push_tag(result, "a",
                      {{"class", "pull_left userpic_wrap"}, {"href", PSLICE() << base_ << link << "#allow_back"}});
  }
  context_.push_div(result, "userpic audio_icon");
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.push_div(result, "body");
  if (!duration.empty()) {
    context_.push_div(result, "pull_right info details");
    append_html_escaped(result, duration);
    context_.pop_tag(result);
  }
  push_linked_name(result, link, name);
  context_.push_div(result, "details_entry details");
  append_html_escaped(result, details);
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlPage::push_linked_name(string &result, Slice link, Slice name) {
  if (name.empty()) {
    return;
  }
  if (!link.empty()) {
    context_.push_tag(result, "a",
                      {{"class", "block_link expanded"}, {"href", PSLICE() << base_ << link << "#allow_back"}});
  }
  context_.push_div(result, "name bold");
  append_html_escaped(result, name);
  context_.pop_tag(result);
  if (!link.empty()) {
    context_.pop_tag(result);
  }
}

static void push_service_message(HtmlContext &context, string &result, Slice base, int32 message_id, Slice text,
                                 const UserpicData *photo) {
  context.push_tag(result, "div", {{"class", "message service"}, {"id", PSLICE() << "message" << message_id}});
  context.push_div(result, "body details");
  result.append(text.data(), text.size());
  context.pop_tag(result);
  if (photo != nullptr) {
    context.push_div(result, "userpic_wrap");
    push_userpic(context, result, base, *photo);
    context.pop_tag(result);
  }
  context.pop_tag(result);
}

static constexpr int32 MEDIA_GROUP_MAX_WIDTH = 430;
static constexpr int32 MEDIA_GROUP_MIN_WIDTH = 100;
static constexpr int32 MEDIA_GROUP_SPACING = 4;

static constexpr size_t RICH_ANCHOR_DIRECT_SIZE = 72;
static constexpr size_t RICH_ANCHOR_PREFIX_SIZE = 48;
static constexpr int32 RICH_TABLE_SPAN_MAX = 1000;
static constexpr size_t RICH_HASHTAG_MIN_LENGTH = 2;
static constexpr size_t RICH_HASHTAG_MAX_LENGTH = 64;
static constexpr size_t RICH_COMMAND_MAX_LENGTH = 64;
static constexpr size_t RICH_CASHTAG_MAX_LENGTH = 8;
static constexpr size_t RICH_USERNAME_MIN_LENGTH = 5;
static constexpr size_t RICH_USERNAME_MAX_LENGTH = 32;

namespace {

struct MediaGroupRect {
  int32 x;
  int32 y;
  int32 width;
  int32 height;
};

class MediaGroupComplexLayouter {
 public:
  MediaGroupComplexLayouter(const vector<double> &ratios, double average_ratio, int32 max_width, int32 min_width,
                            int32 spacing)
      : ratios_(crop_ratios(ratios, average_ratio))
      , count_(static_cast<int32>(ratios_.size()))
      , max_width_(max_width)
      , max_height_(max_width * 4 / 3)
      , min_width_(min_width)
      , spacing_(spacing)
      , average_ratio_(average_ratio) {
  }

  // returns an empty vector if the items can't be placed in at most 4 lines of at most 3 items
  vector<MediaGroupRect> layout() const {
    vector<Attempt> attempts;
    for (int32 first = 1; first < count_; first++) {
      auto second = count_ - first;
      if (first > 3 || second > 3) {
        continue;
      }
      push_attempt(attempts, {first, second});
    }
    for (int32 first = 1; first < count_ - 1; first++) {
      for (int32 second = 1; second < count_ - first; second++) {
        auto third = count_ - first - second;
        if (first > 3 || second > (average_ratio_ < 0.85 ? 4 : 3) || third > 3) {
          continue;
        }
        push_attempt(attempts, {first, second, third});
      }
    }
    for (int32 first = 1; first < count_ - 1; first++) {
      for (int32 second = 1; second < count_ - first; second++) {
        for (int32 third = 1; third < count_ - first - second; third++) {
          auto fourth = count_ - first - second - third;
          if (first > 3 || second > 3 || third > 3 || fourth > 3) {
            continue;
          }
          push_attempt(attempts, {first, second, third, fourth});
        }
      }
    }

    const Attempt *optimal_attempt = nullptr;
    double optimal_diff = 0.0;
    for (const auto &attempt : attempts) {
      const auto &heights = attempt.heights;
      const auto &counts = attempt.line_counts;
      auto line_count = static_cast<int32>(counts.size());
      double total_height = 0.0;
      for (auto height : heights) {
        total_height += height;
      }
      total_height += spacing_ * (line_count - 1);
      auto min_line_height = *std::min_element(heights.begin(), heights.end());
      auto bad1 = min_line_height < min_width_ ? 1.5 : 1.0;
      auto bad2 = 1.0;
      for (int32 line = 1; line < line_count; line++) {
        if (counts[line - 1] > counts[line]) {
          bad2 = 1.5;
          break;
        }
      }
      auto diff = std::abs(total_height - max_height_) * bad1 * bad2;
      if (optimal_attempt == nullptr || diff < optimal_diff) {
        optimal_attempt = &attempt;
        optimal_diff = diff;
      }
    }
    if (optimal_attempt == nullptr) {
      return {};
    }

    vector<MediaGroupRect> result(count_);
    const auto &optimal_counts = optimal_attempt->line_counts;
    const auto &optimal_heights = optimal_attempt->heights;
    auto row_count = static_cast<int32>(optimal_counts.size());
    int32 index = 0;
    int32 y = 0;
    for (int32 row = 0; row < row_count; row++) {
      auto column_count = optimal_counts[row];
      auto line_height = optimal_heights[row];
      auto height = round_value(line_height);
      int32 x = 0;
      for (int32 column = 0; column < column_count; column++) {
        auto width = column == column_count - 1 ? max_width_ - x : round_value(ratios_[index] * line_height);
        result[index] = MediaGroupRect{x, y, width, height};
        x += width + spacing_;
        index++;
      }
      y += height + spacing_;
    }
    return result;
  }

  static int32 round_value(double value) {
    return static_cast<int32>(std::round(value));
  }

 private:
  struct Attempt {
    vector<int32> line_counts;
    vector<double> heights;
  };

  vector<double> ratios_;
  int32 count_;
  int32 max_width_;
  int32 max_height_;
  int32 min_width_;
  int32 spacing_;
  double average_ratio_;

  static vector<double> crop_ratios(const vector<double> &ratios, double average_ratio) {
    vector<double> result;
    result.reserve(ratios.size());
    for (auto ratio : ratios) {
      if (average_ratio > 1.1) {
        result.push_back(clamp(ratio, 1.0, 2.75));
      } else {
        result.push_back(clamp(ratio, 0.6667, 1.0));
      }
    }
    return result;
  }

  double get_line_height(int32 offset, int32 count) const {
    double sum = 0.0;
    for (int32 i = 0; i < count; i++) {
      sum += ratios_[offset + i];
    }
    return (max_width_ - (count - 1) * spacing_) / sum;
  }

  void push_attempt(vector<Attempt> &attempts, vector<int32> line_counts) const {
    Attempt attempt;
    int32 offset = 0;
    for (auto count : line_counts) {
      attempt.heights.push_back(get_line_height(offset, count));
      offset += count;
    }
    attempt.line_counts = std::move(line_counts);
    attempts.push_back(std::move(attempt));
  }
};

// places the items of an album in a box of the maximum width
class MediaGroupLayouter {
 public:
  MediaGroupLayouter(const vector<ExportHtmlImageSize> &sizes, int32 max_width, int32 min_width, int32 spacing)
      : sizes_(sizes)
      , ratios_(get_ratios(sizes))
      , proportions_(get_proportions(ratios_))
      , count_(static_cast<int32>(ratios_.size()))
      , max_width_(max_width)
      , max_height_(max_width)
      , min_width_(min_width)
      , spacing_(spacing)
      , average_ratio_(get_average_ratio(ratios_))
      , max_size_ratio_(max_width_ / static_cast<double>(max_height_)) {
  }

  vector<MediaGroupRect> layout() const {
    if (count_ == 0) {
      return {};
    }
    if (count_ == 1) {
      return layout_one();
    }
    if (count_ >= 5 || std::any_of(ratios_.begin(), ratios_.end(), [](double ratio) { return ratio > 2; })) {
      return MediaGroupComplexLayouter(ratios_, average_ratio_, max_width_, min_width_, spacing_).layout();
    }
    if (count_ == 2) {
      return layout_two();
    }
    if (count_ == 3) {
      return layout_three();
    }
    return layout_four();
  }

 private:
  const vector<ExportHtmlImageSize> &sizes_;
  vector<double> ratios_;
  string proportions_;
  int32 count_;
  int32 max_width_;
  int32 max_height_;
  int32 min_width_;
  int32 spacing_;
  double average_ratio_;
  double max_size_ratio_;

  static int32 round_value(double value) {
    return MediaGroupComplexLayouter::round_value(value);
  }

  static vector<double> get_ratios(const vector<ExportHtmlImageSize> &sizes) {
    vector<double> result;
    result.reserve(sizes.size());
    for (const auto &size : sizes) {
      result.push_back(size.width / static_cast<double>(size.height));
    }
    return result;
  }

  static string get_proportions(const vector<double> &ratios) {
    string result;
    for (auto ratio : ratios) {
      result += ratio > 1.2 ? 'w' : (ratio < 0.8 ? 'n' : 'q');
    }
    return result;
  }

  static double get_average_ratio(const vector<double> &ratios) {
    if (ratios.empty()) {
      return 1.0;
    }
    // the sum starts from 1, and the choice of the layout depends on it
    double sum = 1.0;
    for (auto ratio : ratios) {
      sum += ratio;
    }
    return sum / static_cast<double>(ratios.size());
  }

  vector<MediaGroupRect> layout_one() const {
    auto width = max_width_;
    auto height = static_cast<int32>(static_cast<int64>(sizes_[0].height) * width / sizes_[0].width);
    return {MediaGroupRect{0, 0, width, height}};
  }

  vector<MediaGroupRect> layout_two() const {
    if (proportions_ == "ww" && average_ratio_ > 1.4 * max_size_ratio_ && ratios_[1] - ratios_[0] < 0.2) {
      return layout_two_top_bottom();
    }
    if (proportions_ == "ww" || proportions_ == "qq") {
      return layout_two_left_right_equal();
    }
    return layout_two_left_right();
  }

  vector<MediaGroupRect> layout_three() const {
    if (proportions_[0] == 'n') {
      return layout_three_left_and_other();
    }
    return layout_three_top_and_other();
  }

  vector<MediaGroupRect> layout_four() const {
    if (proportions_[0] == 'w') {
      return layout_four_top_and_other();
    }
    return layout_four_left_and_other();
  }

  vector<MediaGroupRect> layout_two_top_bottom() const {
    auto width = max_width_;
    auto height = round_value(min(width / ratios_[0], min(width / ratios_[1], (max_height_ - spacing_) / 2.0)));
    return {MediaGroupRect{0, 0, width, height}, MediaGroupRect{0, height + spacing_, width, height}};
  }

  vector<MediaGroupRect> layout_two_left_right_equal() const {
    auto width = (max_width_ - spacing_) / 2;
    auto height = round_value(min(width / ratios_[0], min(width / ratios_[1], max_height_ * 1.0)));
    return {MediaGroupRect{0, 0, width, height}, MediaGroupRect{width + spacing_, 0, width, height}};
  }

  vector<MediaGroupRect> layout_two_left_right() const {
    auto minimal_width = round_value(min_width_ * 1.5);
    auto second_width = min(round_value(max(0.4 * (max_width_ - spacing_), (max_width_ - spacing_) / ratios_[0] /
                                                                               (1.0 / ratios_[0] + 1.0 / ratios_[1]))),
                            max_width_ - spacing_ - minimal_width);
    auto first_width = max_width_ - second_width - spacing_;
    auto height = min(max_height_, round_value(min(first_width / ratios_[0], second_width / ratios_[1])));
    return {MediaGroupRect{0, 0, first_width, height}, MediaGroupRect{first_width + spacing_, 0, second_width, height}};
  }

  vector<MediaGroupRect> layout_three_left_and_other() const {
    auto first_height = max_height_;
    auto third_height = round_value(
        min((max_height_ - spacing_) / 2.0, ratios_[1] * (max_width_ - spacing_) / (ratios_[2] + ratios_[1])));
    auto second_height = first_height - third_height - spacing_;
    auto right_width = max(min_width_, round_value(min((max_width_ - spacing_) / 2.0,
                                                       min(third_height * ratios_[2], second_height * ratios_[1]))));
    auto left_width = min(round_value(first_height * ratios_[0]), max_width_ - spacing_ - right_width);
    return {MediaGroupRect{0, 0, left_width, first_height},
            MediaGroupRect{left_width + spacing_, 0, right_width, second_height},
            MediaGroupRect{left_width + spacing_, second_height + spacing_, right_width, third_height}};
  }

  vector<MediaGroupRect> layout_three_top_and_other() const {
    auto first_width = max_width_;
    auto first_height = round_value(min(first_width / ratios_[0], (max_height_ - spacing_) * 0.66));
    auto second_width = (max_width_ - spacing_) / 2;
    auto second_height = min(max_height_ - first_height - spacing_,
                             round_value(min(second_width / ratios_[1], second_width / ratios_[2])));
    auto third_width = first_width - second_width - spacing_;
    return {MediaGroupRect{0, 0, first_width, first_height},
            MediaGroupRect{0, first_height + spacing_, second_width, second_height},
            MediaGroupRect{second_width + spacing_, first_height + spacing_, third_width, second_height}};
  }

  vector<MediaGroupRect> layout_four_top_and_other() const {
    auto w = max_width_;
    auto h0 = round_value(min(w / ratios_[0], (max_height_ - spacing_) * 0.66));
    auto h = round_value((max_width_ - 2 * spacing_) / (ratios_[1] + ratios_[2] + ratios_[3]));
    auto w0 = max(min_width_, round_value(min((max_width_ - 2 * spacing_) * 0.4, h * ratios_[1])));
    auto w2 = round_value(max(max(min_width_ * 1.0, (max_width_ - 2 * spacing_) * 0.33), h * ratios_[3]));
    auto w1 = w - w0 - w2 - 2 * spacing_;
    auto h1 = min(max_height_ - h0 - spacing_, h);
    return {MediaGroupRect{0, 0, w, h0}, MediaGroupRect{0, h0 + spacing_, w0, h1},
            MediaGroupRect{w0 + spacing_, h0 + spacing_, w1, h1},
            MediaGroupRect{w0 + spacing_ + w1 + spacing_, h0 + spacing_, w2, h1}};
  }

  vector<MediaGroupRect> layout_four_left_and_other() const {
    auto h = max_height_;
    auto w0 = round_value(min(h * ratios_[0], (max_width_ - spacing_) * 0.6));
    auto w = round_value((max_height_ - 2 * spacing_) / (1.0 / ratios_[1] + 1.0 / ratios_[2] + 1.0 / ratios_[3]));
    auto h0 = round_value(w / ratios_[1]);
    auto h1 = round_value(w / ratios_[2]);
    auto h2 = h - h0 - h1 - 2 * spacing_;
    auto w1 = max(min_width_, min(max_width_ - w0 - spacing_, w));
    return {MediaGroupRect{0, 0, w0, h}, MediaGroupRect{w0 + spacing_, 0, w1, h0},
            MediaGroupRect{w0 + spacing_, h0 + spacing_, w1, h1},
            MediaGroupRect{w0 + spacing_, h0 + h1 + 2 * spacing_, w1, h2}};
  }
};

// a rectangle with inclusive edges, which are swapped for a negative size when rectangles are united
struct MediaGroupBounds {
  int32 left = 0;
  int32 top = 0;
  int32 right = -1;
  int32 bottom = -1;

  bool is_null() const {
    return right == left - 1 && bottom == top - 1;
  }

  bool is_empty() const {
    return left > right || top > bottom;
  }

  int32 get_width() const {
    return right - left + 1;
  }

  int32 get_height() const {
    return bottom - top + 1;
  }
};

static MediaGroupBounds get_media_group_bounds(const MediaGroupRect &rect) {
  MediaGroupBounds result;
  result.left = rect.x;
  result.top = rect.y;
  result.right = rect.x + rect.width - 1;
  result.bottom = rect.y + rect.height - 1;
  return result;
}

static MediaGroupBounds unite_media_group_bounds(const MediaGroupBounds &lhs, const MediaGroupBounds &rhs) {
  if (lhs.is_null()) {
    return rhs;
  }
  if (rhs.is_null()) {
    return lhs;
  }
  auto get_min = [](int32 from, int32 to) {
    return to - from + 1 < 0 ? to : from;
  };
  auto get_max = [](int32 from, int32 to) {
    return to - from + 1 < 0 ? from : to;
  };
  MediaGroupBounds result;
  result.left = min(get_min(lhs.left, lhs.right), get_min(rhs.left, rhs.right));
  result.right = max(get_max(lhs.left, lhs.right), get_max(rhs.left, rhs.right));
  result.top = min(get_min(lhs.top, lhs.bottom), get_min(rhs.top, rhs.bottom));
  result.bottom = max(get_max(lhs.top, lhs.bottom), get_max(rhs.top, rhs.bottom));
  return result;
}

enum class RichTailState : int32 { Empty, Media, Other };

enum class RichSourceMetaKind : int32 { Source, EmbedPost };

}  // namespace

static Slice get_rich_bool(bool value) {
  return value ? Slice("true") : Slice("false");
}

// identifiers are unsigned in the HTML pages
static string get_rich_id(int64 id) {
  return to_string(static_cast<uint64>(id));
}

static HtmlAttributes get_rich_block_attributes(Slice classes, Slice kind) {
  HtmlAttributes result;
  result.emplace("class", PSTRING() << "rich_block " << classes);
  result.emplace("data-rich-kind", kind.str());
  return result;
}

static Slice get_rich_heading_tag(int32 level) {
  switch (level) {
    case 1:
      return Slice("h1");
    case 2:
      return Slice("h2");
    case 3:
      return Slice("h3");
    case 4:
      return Slice("h4");
    case 5:
      return Slice("h5");
    default:
      return Slice("h6");
  }
}

static string get_rich_button_peer_types(const vector<ExportInlineButtonAction::PeerType> &types) {
  string result = "[";
  for (size_t i = 0; i < types.size(); i++) {
    if (i > 0) {
      result += ',';
    }
    result += '"';
    result += get_export_inline_button_peer_type_string(types[i]).str();
    result += '"';
  }
  result += ']';
  return url_encode(result);
}

static HtmlAttributes get_rich_button_attributes(const ExportInlineButtonAction &action, bool has_style,
                                                 ExportRichButtonStyle style, bool is_inline) {
  using Type = ExportInlineButtonAction::Type;
  auto style_name = get_export_rich_button_style_string(has_style ? style : ExportRichButtonStyle::Default);
  auto type_name = get_export_inline_button_action_type_string(action.type);
  HtmlAttributes result;
  result.emplace("class", PSTRING() << "rich_button " << (is_inline ? "rich_button_inline" : "rich_button_block")
                                    << " rich_button_style_" << style_name << " rich_button_action_" << type_name);
  result.emplace("data-button-type", type_name.str());
  result.emplace("inline", string());
  if (has_style) {
    result.emplace("data-button-style", style_name.str());
  }
  switch (action.type) {
    case Type::Url:
    case Type::Auth:
    case Type::WebView:
      result.emplace("data-button-url", url_encode(action.url));
      if (action.type == Type::Auth) {
        if (action.has_forward_text) {
          result.emplace("data-button-forward-text", url_encode(action.forward_text));
        }
        result.emplace("data-button-id", to_string(action.button_id));
      }
      break;
    case Type::Callback:
    case Type::CallbackWithPassword:
      result.emplace("data-callback-data-base64", base64url_encode(action.callback_data));
      result.emplace("data-button-requires-password", get_rich_bool(action.requires_password).str());
      break;
    case Type::Game:
    case Type::Buy:
    case Type::Disabled:
      break;
    case Type::SwitchInline:
    case Type::SwitchInlineSame:
      result.emplace("data-button-query", url_encode(action.query));
      result.emplace("data-button-same-peer", get_rich_bool(action.same_peer).str());
      if (action.has_peer_types) {
        result.emplace("data-button-peer-types", get_rich_button_peer_types(action.peer_types));
      }
      break;
    case Type::UserProfile:
      result.emplace("data-button-user-id", get_rich_id(action.user_id));
      break;
    case Type::CopyText:
      result.emplace("data-copy-text", url_encode(action.copy_text));
      break;
    default:
      UNREACHABLE();
  }
  if (action.type == Type::Disabled) {
    result.emplace("aria-disabled", "true");
  }
  return result;
}

static string get_rich_percent_value(int32 value, int32 total) {
  return PSTRING() << StringBuilder::FixedDouble(value * 100.0 / total, 3) << '%';
}

static string get_rich_aspect_ratio_style(int32 width, int32 height) {
  return PSTRING() << "aspect-ratio: " << width << " / " << height;
}

static bool rich_text_has_output(const ExportRichText &text) {
  switch (text.type) {
    case ExportRichText::Type::Empty:
      return false;
    case ExportRichText::Type::Plain:
      return !text.text.empty();
    case ExportRichText::Type::Concat:
      for (const auto &child : text.children) {
        if (rich_text_has_output(child)) {
          return true;
        }
      }
      return false;
    default:
      return true;
  }
}

static bool rich_caption_has_output(const ExportRichCaption &caption) {
  return rich_text_has_output(caption.text) || rich_text_has_output(caption.credit);
}

static ExportFile get_rich_file_presentation(const ExportFile *file) {
  ExportFile result;
  if (file == nullptr) {
    result.skip_reason = ExportFile::SkipReason::Unavailable;
    return result;
  }
  result = *file;
  if (!result.relative_path.empty()) {
    result.skip_reason = ExportFile::SkipReason::None;
  } else if (result.skip_reason == ExportFile::SkipReason::None) {
    result.skip_reason = ExportFile::SkipReason::Unavailable;
  }
  return result;
}

static Slice get_rich_file_description(const ExportFile *file) {
  return get_no_file_description(get_rich_file_presentation(file).skip_reason);
}

// returns the scheme of the link in lowercase, or an empty string if the link has none
static string get_link_scheme(Slice link) {
  for (size_t i = 0; i < link.size(); i++) {
    auto c = link[i];
    if (c == ':') {
      return to_lower(link.substr(0, i));
    }
    if (!is_alpha(c) && (i == 0 || (!is_digit(c) && c != '+' && c != '-' && c != '.'))) {
      return string();
    }
  }
  return string();
}

// returns the link of a button, which is opened as an HTTPS link if it has no scheme, or an empty string
static string get_safe_message_link(Slice value) {
  if (!is_strict_link_target(value) || begins_with(value, "//") || has_encoded_control(value)) {
    return string();
  }
  auto scheme = get_link_scheme(value);
  if (scheme.empty()) {
    return get_safe_link(PSLICE() << "https://" << value);
  }
  if (scheme != "http" && scheme != "https" && scheme != "mailto" && scheme != "tel" && scheme != "tg") {
    return string();
  }
  return get_safe_link(value);
}

// returns the HTTP or HTTPS link if it can be safely opened from the exported page, or an empty string otherwise
static string get_safe_http_link(Slice value) {
  if (!is_strict_link_target(value) || begins_with(value, "//") || has_encoded_control(value)) {
    return string();
  }
  auto scheme = get_link_scheme(value);
  if (scheme != "http" && scheme != "https") {
    return string();
  }
  auto r_link = LinkManager::check_link(value, true);
  if (r_link.is_error()) {
    return string();
  }
  auto link = r_link.move_as_ok();
  if (!is_strict_link_target(link) || has_encoded_control(link)) {
    return string();
  }
  return link;
}

static bool is_unicode_space(uint32 code) {
  return get_unicode_simple_category(code) == UnicodeSimpleCategory::Separator;
}

static string get_safe_email_link(Slice address) {
  if (!is_strict_link_target(address)) {
    return string();
  }
  auto separator = address.find('@');
  if (separator == 0 || separator == Slice::npos || separator != address.rfind('@') ||
      separator + 1 == address.size()) {
    return string();
  }
  const auto *ptr = address.ubegin();
  const auto *end = address.uend();
  while (ptr != end) {
    uint32 code = 0;
    ptr = next_utf8_unsafe(ptr, &code);
    if (is_unicode_space(code)) {
      return string();
    }
  }
  for (auto c : Slice("/\\:%?#&=")) {
    if (address.find(c) != Slice::npos) {
      return string();
    }
  }
  string result = PSTRING() << "mailto:" << address;
  return is_strict_link_target(result) ? result : string();
}

static string get_safe_phone_link(Slice phone) {
  if (!is_strict_link_target(phone)) {
    return string();
  }
  bool has_digit = false;
  for (auto c : phone) {
    if (is_digit(c)) {
      has_digit = true;
    } else if (c != '+' && c != '-' && c != ' ' && c != '.' && c != '(' && c != ')') {
      return string();
    }
  }
  string result = PSTRING() << "tel:" << phone;
  return has_digit && is_strict_link_target(result) ? result : string();
}

static bool is_ascii_word(Slice value, size_t min_length, size_t max_length) {
  if (value.size() < min_length || value.size() > max_length) {
    return false;
  }
  for (auto c : value) {
    if (!is_alnum(c) && c != '_') {
      return false;
    }
  }
  return true;
}

static string get_safe_mention_link(Slice mention) {
  if (!is_strict_link_target(mention) || mention.size() < 2 || mention[0] != '@') {
    return string();
  }
  auto username = mention.substr(1);
  for (auto c : username) {
    if (!is_alnum(c) && c != '_') {
      return string();
    }
  }
  return get_safe_http_link(PSLICE() << LinkManager::get_t_me_url() << username);
}

static Slice get_rich_channel_source_name(ExportRichChannel::Source source) {
  using Source = ExportRichChannel::Source;
  switch (source) {
    case Source::ChatEmpty:
      return Slice("chat_empty");
    case Source::Chat:
      return Slice("chat");
    case Source::ChatForbidden:
      return Slice("chat_forbidden");
    case Source::Channel:
      return Slice("channel");
    case Source::ChannelForbidden:
      return Slice("channel_forbidden");
    case Source::Community:
      return Slice("community");
    case Source::CommunityForbidden:
      return Slice("community_forbidden");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static string get_rich_channel_status(const ExportRichChannel &channel) {
  using Source = ExportRichChannel::Source;
  Slice label;
  switch (channel.source) {
    case Source::ChatEmpty:
    case Source::Chat:
      label = Slice("Chat");
      break;
    case Source::ChatForbidden:
      label = Slice("Chat unavailable");
      break;
    case Source::Channel:
      label = Slice("Channel");
      break;
    case Source::ChannelForbidden:
      label = Slice("Channel unavailable");
      break;
    case Source::Community:
      label = Slice("Community");
      break;
    case Source::CommunityForbidden:
      label = Slice("Community unavailable");
      break;
    default:
      UNREACHABLE();
  }
  if (channel.has_username && !channel.username.empty()) {
    return PSTRING() << '@' << channel.username << ", " << label;
  }
  return label.str();
}

static Slice get_rich_map_point_source_name(ExportRichMapPoint::Source source) {
  using Source = ExportRichMapPoint::Source;
  switch (source) {
    case Source::GeoPointEmpty:
      return Slice("geo_point_empty");
    case Source::GeoPoint:
      return Slice("geo_point");
    case Source::InputGeoPointEmpty:
      return Slice("input_geo_point_empty");
    case Source::InputGeoPoint:
      return Slice("input_geo_point");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static bool has_rich_map_coordinates(const ExportRichMapPoint &point) {
  if (point.source != ExportRichMapPoint::Source::GeoPoint &&
      point.source != ExportRichMapPoint::Source::InputGeoPoint) {
    return false;
  }
  return std::isfinite(point.latitude) && std::isfinite(point.longitude) && point.latitude >= -90.0 &&
         point.latitude <= 90.0 && point.longitude >= -180.0 && point.longitude <= 180.0;
}

static bool is_connector_punctuation(uint32 code) {
  return code == 0x5F || code == 0x203F || code == 0x2040 || code == 0x2054 || code == 0xFE33 || code == 0xFE34 ||
         (0xFE4D <= code && code <= 0xFE4F) || code == 0xFF3F;
}

static bool is_unicode_hashtag_word(Slice value) {
  size_t length = 0;
  bool all_decimal_digits = true;
  const auto *ptr = value.ubegin();
  const auto *end = value.uend();
  while (ptr != end) {
    uint32 code = 0;
    ptr = next_utf8_unsafe(ptr, &code);
    length++;
    auto category = get_unicode_simple_category(code);
    if (category != UnicodeSimpleCategory::Letter && category != UnicodeSimpleCategory::DecimalNumber &&
        category != UnicodeSimpleCategory::Number && !is_connector_punctuation(code)) {
      return false;
    }
    if (category != UnicodeSimpleCategory::DecimalNumber) {
      all_decimal_digits = false;
    }
  }
  return RICH_HASHTAG_MIN_LENGTH <= length && length <= RICH_HASHTAG_MAX_LENGTH && !all_decimal_digits;
}

static bool is_rich_hashtag_action(Slice value) {
  auto separator = value.find('@');
  if (separator == Slice::npos) {
    return is_unicode_hashtag_word(value);
  }
  return is_unicode_hashtag_word(value.substr(0, separator)) && separator == value.rfind('@') &&
         is_ascii_word(value.substr(separator + 1), 1, RICH_USERNAME_MAX_LENGTH);
}

static bool is_rich_bot_command_action(Slice value) {
  auto separator = value.find('@');
  if (separator == Slice::npos) {
    return is_ascii_word(value, 1, RICH_COMMAND_MAX_LENGTH);
  }
  return is_ascii_word(value.substr(0, separator), 1, RICH_COMMAND_MAX_LENGTH) && separator == value.rfind('@') &&
         is_ascii_word(value.substr(separator + 1), RICH_USERNAME_MIN_LENGTH, RICH_USERNAME_MAX_LENGTH);
}

static bool is_rich_cashtag_action(Slice value) {
  if (value.empty() || value.size() > RICH_CASHTAG_MAX_LENGTH) {
    return false;
  }
  for (auto c : value) {
    if (c < 'A' || c > 'Z') {
      return false;
    }
  }
  return true;
}

// returns the hashtag, the bot command or the cashtag without the prefix, or an empty string if it is invalid
static string get_safe_rich_action_data(Slice target, char prefix) {
  if (!is_strict_link_target(target) || target[0] != prefix || target.size() < 2) {
    return string();
  }
  auto result = target.substr(1);
  bool is_valid = false;
  switch (prefix) {
    case '#':
      is_valid = is_rich_hashtag_action(result);
      break;
    case '/':
      is_valid = is_rich_bot_command_action(result);
      break;
    case '$':
      is_valid = is_rich_cashtag_action(result);
      break;
    default:
      UNREACHABLE();
  }
  return is_valid && is_strict_link_target(result) ? result.str() : string();
}

static bool is_ascii_decimal(Slice value) {
  if (value.empty()) {
    return false;
  }
  for (auto c : value) {
    if (!is_digit(c)) {
      return false;
    }
  }
  return true;
}

static string get_safe_relative_emoji_link(Slice path, Slice base) {
  if (!is_strict_link_target(path) || path[0] == '/' || path.find('\\') != Slice::npos ||
      path.find(':') != Slice::npos || path.find('%') != Slice::npos || path.find('?') != Slice::npos ||
      path.find('#') != Slice::npos || path == Slice(UNAVAILABLE_CUSTOM_EMOJI) || is_ascii_decimal(path)) {
    return string();
  }
  for (auto component : full_split(path, '/')) {
    if (component.empty() || component == "." || component == "..") {
      return string();
    }
  }
  string result = PSTRING() << base << path;
  return is_strict_link_target(result) ? result : string();
}

static bool append_rich_plain_target(string &result, Slice value) {
  if (result.size() > MAX_LINK_TARGET_SIZE || value.size() > MAX_LINK_TARGET_SIZE - result.size()) {
    return false;
  }
  result.append(value.data(), value.size());
  return true;
}

static bool append_rich_plain_target(string &result, const vector<ExportRichText> &texts);

static bool append_rich_plain_target(string &result, const ExportRichText &text) {
  using Type = ExportRichText::Type;
  switch (text.type) {
    case Type::Empty:
      return true;
    case Type::Plain:
    case Type::CustomEmoji:
      return append_rich_plain_target(result, Slice(text.text));
    case Type::Button:
    case Type::Math:
    case Type::InlineImage:
      return false;
    default:
      return append_rich_plain_target(result, text.children);
  }
}

static bool append_rich_plain_target(string &result, const vector<ExportRichText> &texts) {
  for (const auto &text : texts) {
    if (!append_rich_plain_target(result, text)) {
      return false;
    }
  }
  return true;
}

static string get_rich_anchor_token(Slice name) {
  if (name.empty()) {
    return "empty";
  }
  if (name.size() <= RICH_ANCHOR_DIRECT_SIZE) {
    return base64url_encode(name);
  }
  return PSTRING() << base64url_encode(name.substr(0, RICH_ANCHOR_PREFIX_SIZE)) << '-'
                   << hex_encode(sha256(name)).substr(0, 16);
}

namespace {

class RichHtmlRenderer;

class ExportHtmlMessage {
 public:
  ExportHtmlMessage(Td *td, const ExportSettings &settings, const ExportDialogInfo &dialog, Slice dialog_first_name,
                    Slice dialog_last_name, ExportHtmlPage &page, const ExportMessage &export_message, bool is_migrated,
                    const ExportCustomEmojiFiles &custom_emoji_files, const vector<int32> &last_message_ids_per_page)
      : td_(td)
      , settings_(settings)
      , dialog_(dialog)
      , dialog_first_name_(dialog_first_name)
      , dialog_last_name_(dialog_last_name)
      , context_(page.context())
      , base_(page.base())
      , message_(*export_message.message)
      , file_(export_message.file)
      , thumbnail_(export_message.thumbnail)
      , rich_message_(export_message.rich_message.get())
      , is_history_cleared_(export_message.is_history_cleared)
      , is_migrated_(is_migrated)
      , custom_emoji_files_(custom_emoji_files)
      , last_message_ids_per_page_(last_message_ids_per_page) {
  }

  ExportHtmlMessageInfo push(string &result, const ExportHtmlMessageInfo *previous);

 private:
  friend class RichHtmlRenderer;

  Td *td_;
  const ExportSettings &settings_;
  const ExportDialogInfo &dialog_;
  Slice dialog_first_name_;
  Slice dialog_last_name_;
  HtmlContext &context_;
  Slice base_;
  const td_api::message &message_;
  const ExportFile &file_;
  const ExportFile &thumbnail_;
  const ExportRichMessage *rich_message_;
  bool is_history_cleared_;
  bool is_migrated_;
  const ExportCustomEmojiFiles &custom_emoji_files_;
  const vector<int32> &last_message_ids_per_page_;

  bool is_channel() const {
    return dialog_.type == ExportDialogInfo::Type::PrivateChannel ||
           dialog_.type == ExportDialogInfo::Type::PublicChannel;
  }

  DialogId get_my_dialog_id() const {
    return DialogId(td_->user_manager_->get_my_id());
  }

  int32 get_message_id() const {
    auto server_message_id = get_export_server_message_id(message_.id_);
    return is_migrated_ ? server_message_id + EXPORT_MIGRATED_MESSAGE_ID_SHIFT : server_message_id;
  }

  int32 get_same_chat_message_id(int64 message_id) const {
    auto server_message_id = get_export_server_message_id(message_id);
    if (server_message_id == 0) {
      return 0;
    }
    return is_migrated_ ? server_message_id + EXPORT_MIGRATED_MESSAGE_ID_SHIFT : server_message_id;
  }

  string wrap_message_link(int32 message_id, Slice text) const;

  string get_raw_dialog_name(DialogId dialog_id) const {
    if (!dialog_id.is_valid()) {
      return string();
    }
    return td_->dialog_manager_->get_dialog_title(dialog_id);
  }

  string get_peer_name(DialogId dialog_id) const {
    auto name = get_raw_dialog_name(dialog_id);
    return name.empty() ? string("Deleted") : html_escape(name);
  }

  string get_user_name(int64 user_id) const {
    auto name = td_->user_manager_->get_user_title(UserId(user_id));
    return name.empty() ? string("Deleted Account") : html_escape(name);
  }

  string get_user_names(const vector<int64> &user_ids) const {
    vector<string> names;
    for (auto user_id : user_ids) {
      names.push_back(get_user_name(user_id));
    }
    return join_html_list(names);
  }

  void fill_userpic_names(UserpicData &userpic, DialogId dialog_id) const {
    if (dialog_id.get_type() == DialogType::User) {
      userpic.first_name = td_->user_manager_->get_user_first_name(dialog_id.get_user_id()).str();
      userpic.last_name = td_->user_manager_->get_user_last_name(dialog_id.get_user_id()).str();
    } else if (dialog_id.is_valid()) {
      userpic.first_name = get_raw_dialog_name(dialog_id);
    }
  }

  static void fill_userpic_names(UserpicData &userpic, Slice full_name) {
    auto names = full_split(full_name, ' ');
    userpic.first_name = names[0].str();
    for (size_t i = 1; i < names.size(); i++) {
      if (names[i].empty()) {
        continue;
      }
      if (!userpic.last_name.empty()) {
        userpic.last_name += ' ';
      }
      userpic.last_name.append(names[i].data(), names[i].size());
    }
  }

  static string compose_name(const UserpicData &userpic, Slice empty) {
    if (userpic.first_name.empty() && userpic.last_name.empty()) {
      return empty.str();
    }
    return PSTRING() << userpic.first_name << ' ' << userpic.last_name;
  }

  string format_text(const td_api::formattedText *text) const {
    return td::format_text(text, &custom_emoji_files_, base_);
  }

  string get_service_text(const td_api::MessageContent *content) const;

  const td_api::chatPhoto *get_service_photo(const td_api::MessageContent *content) const;

  ExportHtmlMessageInfo get_message_info() const;

  bool message_needs_wrap(const ExportHtmlMessageInfo &info, const ExportHtmlMessageInfo *previous) const;

  bool forwarded_needs_wrap(const ExportHtmlMessageInfo &info, const ExportHtmlMessageInfo *previous) const;

  MediaData prepare_media_data(const td_api::MessageContent *content) const;

  void push_media(string &result, const td_api::MessageContent *content);

  void push_generic_media(string &result, const MediaData &data);

  void push_photo_media(string &result, const td_api::photoSize *size, const ExportFile &file,
                        const ExportFile &thumbnail);

  void push_sticker_media(string &result, const td_api::sticker *sticker);

  void push_animated_media(string &result, const td_api::animation *animation);

  void push_video_file_media(string &result, int32 width, int32 height, int32 duration, int64 file_size,
                             const ExportFile &file, const ExportFile &thumbnail);

  void push_rich_photo_media(string &result, const ExportRichPhoto *photo);

  void push_rich_video_media(string &result, const ExportRichDocument *document);

  void push_rich_audio_media(string &result, const ExportRichDocument *document);

  void push_rich_file_media(string &result, const ExportRichDocument *document);

  void push_rich_reference_media(string &result, MediaData data, const ExportRichPhoto *photo);

  void push_poll(string &result, const td_api::poll *poll);

  void push_checklist(string &result, const td_api::checklist *checklist);

  void push_giveaway(string &result, const td_api::messageGiveaway *giveaway);

  void push_giveaway_winners(string &result, const td_api::messageGiveawayWinners *winners);

  void push_inline_buttons(string &result);

  void push_reactions(string &result);
};

string ExportHtmlMessage::wrap_message_link(int32 message_id, Slice text) const {
  if (message_id == 0) {
    // the message is unknown
    return text.str();
  }
  // the last message identifiers of the pages are increasing
  auto it = std::lower_bound(last_message_ids_per_page_.begin(), last_message_ids_per_page_.end(), message_id);
  if (it != last_message_ids_per_page_.end()) {
    auto page = static_cast<int32>(it - last_message_ids_per_page_.begin());
    return PSTRING() << "<a href=\"" << get_export_html_page_name(page) << "#go_to_message" << message_id << "\">"
                     << text << "</a>";
  }
  return PSTRING() << "<a href=\"#go_to_message" << message_id << "\" onclick=\"return GoToMessage(" << message_id
                   << ")\">" << text << "</a>";
}

static Slice get_passport_element_type_name(const td_api::PassportElementType *type) {
  switch (type->get_id()) {
    case td_api::passportElementTypePersonalDetails::ID:
      return Slice("Personal details");
    case td_api::passportElementTypePassport::ID:
      return Slice("Passport");
    case td_api::passportElementTypeDriverLicense::ID:
      return Slice("Driver license");
    case td_api::passportElementTypeIdentityCard::ID:
      return Slice("Identity card");
    case td_api::passportElementTypeInternalPassport::ID:
      return Slice("Internal passport");
    case td_api::passportElementTypeAddress::ID:
      return Slice("Address information");
    case td_api::passportElementTypeUtilityBill::ID:
      return Slice("Utility bill");
    case td_api::passportElementTypeBankStatement::ID:
      return Slice("Bank statement");
    case td_api::passportElementTypeRentalAgreement::ID:
      return Slice("Rental agreement");
    case td_api::passportElementTypePassportRegistration::ID:
      return Slice("Passport registration");
    case td_api::passportElementTypeTemporaryRegistration::ID:
      return Slice("Temporary registration");
    case td_api::passportElementTypePhoneNumber::ID:
      return Slice("Phone number");
    case td_api::passportElementTypeEmailAddress::ID:
      return Slice("Email");
    default:
      return Slice();
  }
}

// the price of a suggested post as a number and its currency
static string format_suggested_post_price(const td_api::SuggestedPostPrice *price) {
  if (price == nullptr) {
    return string();
  }
  switch (price->get_id()) {
    case td_api::suggestedPostPriceStar::ID: {
      auto star_count = static_cast<const td_api::suggestedPostPriceStar *>(price)->star_count_;
      if (star_count == 0) {
        return string();
      }
      return PSTRING() << format_double(static_cast<double>(star_count)) << " stars";
    }
    case td_api::suggestedPostPriceGram::ID: {
      auto cent_count = static_cast<const td_api::suggestedPostPriceGram *>(price)->gram_cent_count_;
      if (cent_count == 0) {
        return string();
      }
      return PSTRING() << format_double(static_cast<double>(cent_count) / 100) << " TON";
    }
    default:
      UNREACHABLE();
      return string();
  }
}

static string format_task_ids(const vector<int32> &task_ids) {
  vector<string> values;
  for (auto task_id : task_ids) {
    values.push_back(to_string(task_id));
  }
  return join_html_list(values);
}

string ExportHtmlMessage::get_service_text(const td_api::MessageContent *content) const {
  auto from = get_peer_name(get_export_message_sender_dialog_id(message_.sender_id_.get()));
  auto quoted = [](Slice text) {
    return PSTRING() << "&laquo;" << html_escape(text) << "&raquo;";
  };
  switch (content->get_id()) {
    case td_api::messageBasicGroupChatCreate::ID: {
      const auto *m = static_cast<const td_api::messageBasicGroupChatCreate *>(content);
      string result = PSTRING() << from << " created group " << quoted(m->title_);
      if (!m->member_user_ids_.empty()) {
        result += " with members ";
        result += get_user_names(m->member_user_ids_);
      }
      return result;
    }
    case td_api::messageChatChangeTitle::ID: {
      const auto &title = static_cast<const td_api::messageChatChangeTitle *>(content)->title_;
      if (is_channel()) {
        return PSTRING() << "Channel title changed to " << quoted(title);
      }
      return PSTRING() << from << " changed group title to " << quoted(title);
    }
    case td_api::messageChatChangePhoto::ID:
      return is_channel() ? string("Channel photo changed") : PSTRING() << from << " changed group photo";
    case td_api::messageChatDeletePhoto::ID:
      return is_channel() ? string("Channel photo removed") : PSTRING() << from << " removed group photo";
    case td_api::messageChatAddMembers::ID:
      return PSTRING() << from << " invited "
                       << get_user_names(static_cast<const td_api::messageChatAddMembers *>(content)->member_user_ids_);
    case td_api::messageChatDeleteMember::ID:
      return PSTRING() << from << " removed "
                       << get_user_name(static_cast<const td_api::messageChatDeleteMember *>(content)->user_id_);
    case td_api::messageChatJoinByLink::ID:
      // the inviter isn't known
      return PSTRING() << from << " joined group by link";
    case td_api::messageSupergroupChatCreate::ID:
      return PSTRING() << "Channel "
                       << quoted(static_cast<const td_api::messageSupergroupChatCreate *>(content)->title_)
                       << " created";
    case td_api::messageChatUpgradeTo::ID:
      return PSTRING() << from << " converted this group to a supergroup";
    case td_api::messageChatUpgradeFrom::ID:
      return PSTRING() << from << " converted a basic group to this supergroup "
                       << quoted(static_cast<const td_api::messageChatUpgradeFrom *>(content)->title_);
    case td_api::messagePinMessage::ID:
      return PSTRING() << from << " pinned "
                       << wrap_message_link(get_same_chat_message_id(
                                                static_cast<const td_api::messagePinMessage *>(content)->message_id_),
                                            "this message");
    case td_api::messageGameScore::ID: {
      const auto *m = static_cast<const td_api::messageGameScore *>(content);
      return PSTRING() << from << " scored " << m->score_ << " in "
                       << wrap_message_link(get_same_chat_message_id(m->game_message_id_), "this game");
    }
    case td_api::messagePaymentSuccessful::ID: {
      const auto *m = static_cast<const td_api::messagePaymentSuccessful *>(content);
      auto amount = html_escape(format_export_money_amount(m->total_amount_, m->currency_));
      if (m->is_recurring_ && !m->is_first_recurring_) {
        return PSTRING() << "You were charged " << amount << " via recurring payment";
      }
      // the invoice can be in another chat, which isn't exported with this one
      auto invoice = m->invoice_chat_id_ == message_.chat_id_
                         ? wrap_message_link(get_same_chat_message_id(m->invoice_message_id_), "this invoice")
                         : string("this invoice");
      string result = PSTRING() << "You have successfully transferred " << amount << " for " << invoice;
      if (m->is_first_recurring_) {
        result += " and allowed future recurring payments";
      }
      return result;
    }
    case td_api::messageCall::ID:
    case td_api::messageGroupCall::ID:
      // calls are shown as media
      return string();
    case td_api::messageScreenshotTaken::ID:
      return PSTRING() << from << " took a screenshot";
    case td_api::messageCustomServiceAction::ID:
      return html_escape(static_cast<const td_api::messageCustomServiceAction *>(content)->text_);
    case td_api::messageBotWriteAccessAllowed::ID: {
      const auto *reason = static_cast<const td_api::messageBotWriteAccessAllowed *>(content)->reason_.get();
      switch (reason->get_id()) {
        case td_api::botWriteAccessAllowReasonAddedToAttachmentMenu::ID:
          return "You allowed this bot to message you when you added it in the attachment menu.";
        case td_api::botWriteAccessAllowReasonAcceptedRequest::ID:
          return "You allowed this bot to message you in his web-app.";
        case td_api::botWriteAccessAllowReasonLaunchedWebApp::ID: {
          const auto *web_app =
              static_cast<const td_api::botWriteAccessAllowReasonLaunchedWebApp *>(reason)->web_app_.get();
          return PSTRING() << "You allowed this bot to message you when you opened "
                           << html_escape(web_app == nullptr ? Slice() : Slice(web_app->title_));
        }
        case td_api::botWriteAccessAllowReasonConnectedWebsite::ID:
          return PSTRING() << "You allowed this bot to message you when you logged in on "
                           << html_escape(static_cast<const td_api::botWriteAccessAllowReasonConnectedWebsite *>(reason)
                                              ->domain_name_);
        default:
          UNREACHABLE();
          return string();
      }
    }
    case td_api::messagePassportDataSent::ID: {
      vector<string> types;
      for (const auto &type : static_cast<const td_api::messagePassportDataSent *>(content)->types_) {
        types.push_back(get_passport_element_type_name(type.get()).str());
      }
      return PSTRING() << "You have sent the following documents: " << join_html_list(types);
    }
    case td_api::messageContactRegistered::ID:
      return PSTRING() << from << " joined Telegram";
    case td_api::messageUnpaidMessagesAllowedByLink::ID:
      return PSTRING() << from << " used a link that allows sending messages for free";
    case td_api::messageBotInvite::ID:
      return PSTRING() << from << " was invited by "
                       << get_user_name(static_cast<const td_api::messageBotInvite *>(content)->inviter_user_id_);
    case td_api::messageProximityAlertTriggered::ID: {
      const auto *m = static_cast<const td_api::messageProximityAlertTriggered *>(content);
      auto traveler_dialog_id = get_export_message_sender_dialog_id(m->traveler_id_.get());
      auto watcher_dialog_id = get_export_message_sender_dialog_id(m->watcher_id_.get());
      auto from_name = get_peer_name(traveler_dialog_id);
      auto to_name = get_peer_name(watcher_dialog_id);
      string distance;
      if (m->distance_ >= 1000) {
        distance = format_double(static_cast<double>(10 * (m->distance_ / 10)) / 1000) + " km";
      } else if (m->distance_ == 1) {
        distance = "1 meter";
      } else {
        distance = PSTRING() << m->distance_ << " meters";
      }
      if (traveler_dialog_id == get_my_dialog_id()) {
        return PSTRING() << "You are now within " << distance << " from " << to_name;
      }
      if (watcher_dialog_id == get_my_dialog_id()) {
        return PSTRING() << from_name << " is now within " << distance << " from you";
      }
      return PSTRING() << from_name << " is now within " << distance << " from " << to_name;
    }
    case td_api::messageVideoChatStarted::ID:
    case td_api::messageVideoChatEnded::ID: {
      auto duration = content->get_id() == td_api::messageVideoChatEnded::ID
                          ? static_cast<const td_api::messageVideoChatEnded *>(content)->duration_
                          : 0;
      string duration_text;
      if (duration != 0) {
        duration_text = PSTRING() << " (" << duration << " seconds)";
      }
      if (is_channel()) {
        return PSTRING() << "Voice chat" << duration_text;
      }
      return PSTRING() << from << " started voice chat" << duration_text;
    }
    case td_api::messageVideoChatScheduled::ID: {
      auto date_text = format_date_time(static_cast<const td_api::messageVideoChatScheduled *>(content)->start_date_,
                                        settings_.utc_time_offset, false);
      if (is_channel()) {
        return PSTRING() << "Voice chat scheduled for " << date_text;
      }
      return PSTRING() << from << " scheduled a voice chat for " << date_text;
    }
    case td_api::messageInviteVideoChatParticipants::ID:
      return PSTRING() << from << " invited "
                       << get_user_names(
                              static_cast<const td_api::messageInviteVideoChatParticipants *>(content)->user_ids_)
                       << " to the voice chat";
    case td_api::messageChatSetMessageAutoDeleteTime::ID: {
      auto period =
          static_cast<const td_api::messageChatSetMessageAutoDeleteTime *>(content)->message_auto_delete_time_;
      Slice period_text = period == 7 * 86400 ? Slice("7 days") : period == 86400 ? Slice("24 hours") : Slice();
      if (is_channel()) {
        if (period != 0) {
          return PSTRING() << "New messages will auto-delete in " << period_text;
        }
        return "New messages will not auto-delete";
      }
      if (period != 0) {
        return PSTRING() << from << " has set messages to auto-delete in " << period_text;
      }
      return PSTRING() << from << " has set messages not to auto-delete";
    }
    case td_api::messageChatSetTheme::ID: {
      const auto *theme = static_cast<const td_api::messageChatSetTheme *>(content)->theme_.get();
      Slice emoji;
      if (theme != nullptr && theme->get_id() == td_api::chatThemeEmoji::ID) {
        emoji = static_cast<const td_api::chatThemeEmoji *>(theme)->name_;
      }
      if (emoji.empty()) {
        return is_channel() ? string("Channel theme was disabled") : PSTRING() << from << " disabled chat theme";
      }
      if (is_channel()) {
        return PSTRING() << "Channel theme was changed to " << html_escape(emoji);
      }
      return PSTRING() << from << " changed chat theme to " << html_escape(emoji);
    }
    case td_api::messageChatJoinByRequest::ID:
      return PSTRING() << from << " joined group by request";
    case td_api::messageChatJoinFromCommunity::ID:
      return PSTRING() << from << " joined group via a community";
    case td_api::messageChatAddedToCommunity::ID:
      return PSTRING() << from << " added the chat to a community";
    case td_api::messageChatRemovedFromCommunity::ID:
      return PSTRING() << from << " removed the chat from a community";
    case td_api::messageWebAppDataSent::ID:
      return PSTRING() << "You have just successfully transferred data from the "
                       << quoted(static_cast<const td_api::messageWebAppDataSent *>(content)->button_text_)
                       << " button to the bot";
    case td_api::messageGiftedPremium::ID: {
      const auto *m = static_cast<const td_api::messageGiftedPremium *>(content);
      auto cost = format_export_money_amount(m->amount_, m->currency_);
      if (m->day_count_ == 0 || cost.empty()) {
        return PSTRING() << from << " sent you a gift.";
      }
      return PSTRING() << from << " sent you a gift for " << html_escape(cost) << ": Telegram Premium for "
                       << m->day_count_ << " days.";
    }
    case td_api::messageForumTopicCreated::ID:
      return PSTRING() << from << " created topic "
                       << quoted(static_cast<const td_api::messageForumTopicCreated *>(content)->name_);
    case td_api::messageForumTopicEdited::ID:
    case td_api::messageForumTopicIsClosedToggled::ID:
    case td_api::messageForumTopicIsHiddenToggled::ID: {
      vector<string> parts;
      if (content->get_id() == td_api::messageForumTopicEdited::ID) {
        const auto *m = static_cast<const td_api::messageForumTopicEdited *>(content);
        if (!m->name_.empty()) {
          parts.push_back(PSTRING() << "title to " << quoted(m->name_));
        }
        if (m->edit_icon_custom_emoji_id_) {
          parts.push_back(PSTRING() << "icon to &laquo;" << m->icon_custom_emoji_id_ << "&raquo;");
        }
      }
      return PSTRING() << from << " changed topic " << join_strings(parts, ",");
    }
    case td_api::messageSuggestProfilePhoto::ID:
      return PSTRING() << from << " suggests to use this photo";
    case td_api::messageUsersShared::ID:
    case td_api::messageChatShared::ID:
      return "requested: ";
    case td_api::messageChatSetBackground::ID: {
      auto old_background_message_id =
          static_cast<const td_api::messageChatSetBackground *>(content)->old_background_message_id_;
      if (old_background_message_id != 0) {
        return PSTRING() << from << " set "
                         << wrap_message_link(get_same_chat_message_id(old_background_message_id),
                                              "the same background")
                         << " for this chat";
      }
      return PSTRING() << from << " set a new background for this chat";
    }
    case td_api::messagePremiumGiftCode::ID: {
      const auto *m = static_cast<const td_api::messagePremiumGiftCode *>(content);
      auto days = PSTRING() << m->day_count_ << (m->day_count_ > 1 ? " days" : " day");
      if (m->is_unclaimed_) {
        return PSTRING() << "This is an unclaimed Telegram Premium for " << days
                         << " prize in a giveaway organized by a channel.";
      }
      if (m->is_from_giveaway_) {
        return PSTRING() << "You won a Telegram Premium for " << days << " prize in a giveaway organized by a channel.";
      }
      return PSTRING() << "You've received a Telegram Premium for " << days << " gift from a channel.";
    }
    case td_api::messageGiveawayCreated::ID:
      return PSTRING() << from << " just started a giveaway of Telegram Premium subscriptions to its followers.";
    case td_api::messageGiveawayCompleted::ID: {
      const auto *m = static_cast<const td_api::messageGiveawayCompleted *>(content);
      auto is_star_giveaway = m->is_star_giveaway_;
      auto is_unclaimed = m->unclaimed_prize_count_ != 0;
      if (m->winner_count_ == 0) {
        return "No winners of the giveaway could be selected.";
      }
      if (is_star_giveaway && is_unclaimed) {
        return "Some winners of the giveaway were randomly selected by Telegram and received their prize.";
      }
      if (!is_star_giveaway && is_unclaimed) {
        return "Some winners of the giveaway were randomly selected by Telegram and received private messages with "
               "giftcodes.";
      }
      if (is_star_giveaway) {
        return PSTRING() << m->winner_count_
                         << " of the giveaway was randomly selected by Telegram and received their prize.";
      }
      return PSTRING() << m->winner_count_
                       << " of the giveaway was randomly selected by Telegram and received private messages with "
                          "giftcodes.";
    }
    case td_api::messageChatBoost::ID: {
      auto boost_count = static_cast<const td_api::messageChatBoost *>(content)->boost_count_;
      return PSTRING() << from << " boosted the group " << boost_count << (boost_count > 1 ? " times" : " time");
    }
    case td_api::messagePaymentRefunded::ID: {
      const auto *m = static_cast<const td_api::messagePaymentRefunded *>(content);
      return PSTRING() << get_peer_name(get_export_message_sender_dialog_id(m->owner_id_.get())) << " refunded back "
                       << html_escape(format_export_money_amount(m->total_amount_, m->currency_));
    }
    case td_api::messageGiftedStars::ID: {
      const auto *m = static_cast<const td_api::messageGiftedStars *>(content);
      auto cost = format_export_money_amount(m->amount_, m->currency_);
      if (m->star_count_ == 0 || cost.empty()) {
        return PSTRING() << from << " sent you a gift.";
      }
      return PSTRING() << from << " sent you a gift for " << html_escape(cost) << ": "
                       << format_double(static_cast<double>(m->star_count_)) << " Telegram Stars.";
    }
    case td_api::messageGiftedGrams::ID:
      // the cost of the gift isn't known
      return PSTRING() << from << " sent you a gift.";
    case td_api::messageGiveawayPrizeStars::ID: {
      const auto *m = static_cast<const td_api::messageGiveawayPrizeStars *>(content);
      return PSTRING() << "You won a prize in a giveaway organized by " << get_peer_name(DialogId(m->boosted_chat_id_))
                       << ".\n Your prize is " << m->star_count_ << " Telegram Stars.";
    }
    case td_api::messageGift::ID: {
      const auto *gift = static_cast<const td_api::messageGift *>(content)->gift_.get();
      return PSTRING() << from << " sent you a gift of " << (gift == nullptr ? 0 : gift->star_count_)
                       << " Telegram Stars.";
    }
    case td_api::messageRefundedUpgradedGift::ID: {
      const auto *gift = static_cast<const td_api::messageRefundedUpgradedGift *>(content)->gift_.get();
      return PSTRING() << from << " sent you a gift of " << (gift == nullptr ? 0 : gift->star_count_)
                       << " Telegram Stars.";
    }
    case td_api::messageUpgradedGift::ID:
    case td_api::messageUpgradedGiftPurchaseOffer::ID:
    case td_api::messageUpgradedGiftPurchaseOfferRejected::ID:
    case td_api::messageUpgradedGiftOnChainPurchaseOffer::ID:
      return PSTRING() << from << " sent you a gift of 0 Telegram Stars.";
    case td_api::messagePaidMessagesRefunded::ID: {
      const auto *m = static_cast<const td_api::messagePaidMessagesRefunded *>(content);
      if (message_.is_outgoing_) {
        return PSTRING() << "You refunded " << m->star_count_ << " Stars for " << m->message_count_ << " messages to "
                         << get_peer_name(dialog_.dialog_id);
      }
      return PSTRING() << get_peer_name(dialog_.dialog_id) << " refunded " << m->star_count_ << " Stars for "
                       << m->message_count_ << " messages to you";
    }
    case td_api::messagePaidMessagePriceChanged::ID:
    case td_api::messageDirectMessagePriceChanged::ID: {
      int64 star_count;
      bool is_enabled = false;
      if (content->get_id() == td_api::messagePaidMessagePriceChanged::ID) {
        star_count = static_cast<const td_api::messagePaidMessagePriceChanged *>(content)->paid_message_star_count_;
      } else {
        const auto *m = static_cast<const td_api::messageDirectMessagePriceChanged *>(content);
        star_count = m->paid_message_star_count_;
        is_enabled = m->is_enabled_;
      }
      if (is_channel()) {
        if (!is_enabled) {
          return "Direct messages were disabled.";
        }
        return PSTRING() << "Price per direct message changed to " << star_count << " Telegram Stars.";
      }
      return PSTRING() << "Price per message changed to " << star_count << " Telegram Stars.";
    }
    case td_api::messageChecklistTasksDone::ID: {
      const auto *m = static_cast<const td_api::messageChecklistTasksDone *>(content);
      auto completed = format_task_ids(m->marked_as_done_task_ids_);
      auto incompleted = format_task_ids(m->marked_as_not_done_task_ids_);
      auto link = wrap_message_link(get_same_chat_message_id(m->checklist_message_id_), "this todo list");
      if (completed.empty() && !incompleted.empty()) {
        return PSTRING() << from << " marked " << incompleted << " as not done yet in " << link << '.';
      }
      if (!completed.empty() && incompleted.empty()) {
        return PSTRING() << from << " marked " << completed << " as done in " << link << '.';
      }
      return PSTRING() << from << " marked " << completed << " as done and " << incompleted << " as not done yet in "
                       << link << '.';
    }
    case td_api::messageChecklistTasksAdded::ID: {
      vector<string> tasks;
      for (const auto &task : static_cast<const td_api::messageChecklistTasksAdded *>(content)->tasks_) {
        tasks.push_back(PSTRING() << "&quot;" << format_text(task->text_.get()) << "&quot;");
      }
      return PSTRING() << from << " added tasks: " << join_strings(tasks, ", ");
    }
    case td_api::messagePollOptionAdded::ID: {
      const auto *text = static_cast<const td_api::messagePollOptionAdded *>(content)->text_.get();
      return PSTRING() << from << " added &quot;" << html_escape(text == nullptr ? Slice() : Slice(text->text_))
                       << "&quot; to the poll.";
    }
    case td_api::messagePollOptionDeleted::ID: {
      const auto *text = static_cast<const td_api::messagePollOptionDeleted *>(content)->text_.get();
      return PSTRING() << from << " removed &quot;" << html_escape(text == nullptr ? Slice() : Slice(text->text_))
                       << "&quot; from the poll.";
    }
    case td_api::messageSuggestedPostApproved::ID:
    case td_api::messageSuggestedPostApprovalFailed::ID:
    case td_api::messageSuggestedPostDeclined::ID: {
      bool is_rejected = content->get_id() == td_api::messageSuggestedPostDeclined::ID;
      string price;
      int32 send_date = 0;
      Slice comment;
      if (content->get_id() == td_api::messageSuggestedPostApproved::ID) {
        const auto *m = static_cast<const td_api::messageSuggestedPostApproved *>(content);
        price = format_suggested_post_price(m->price_.get());
        send_date = m->send_date_;
      } else if (content->get_id() == td_api::messageSuggestedPostApprovalFailed::ID) {
        price = format_suggested_post_price(
            static_cast<const td_api::messageSuggestedPostApprovalFailed *>(content)->price_.get());
      } else {
        comment = static_cast<const td_api::messageSuggestedPostDeclined *>(content)->comment_;
      }
      string result = PSTRING() << from << (is_rejected ? " rejected " : " approved ") << "your suggested post";
      if (!price.empty()) {
        result += ", for ";
        result += price;
      }
      if (send_date != 0) {
        auto local_time = get_export_local_time(send_date, settings_.utc_time_offset);
        result += PSTRING() << ", " << format_date_text(local_time) << " at " << format_time_text(local_time);
      }
      if (comment.empty()) {
        result += '.';
      } else {
        result += PSTRING() << ", with comment: &quot;" << html_escape(comment) << "&quot;";
      }
      return result;
    }
    case td_api::messageSuggestedPostPaid::ID: {
      const auto *m = static_cast<const td_api::messageSuggestedPostPaid *>(content);
      string price;
      if (m->gram_amount_ != 0 || m->star_amount_ == nullptr) {
        price = PSTRING() << format_double(static_cast<double>(m->gram_amount_) / EXPORT_NANOS_IN_GRAM) << " TON";
      } else {
        price = PSTRING() << format_double(static_cast<double>(m->star_amount_->star_count_) +
                                           static_cast<double>(m->star_amount_->nanostar_count_) / 1e9)
                          << " stars";
      }
      return PSTRING() << "The paid post was shown for 24 hours and " << price << " were transferred to the channel.";
    }
    case td_api::messageSuggestedPostRefunded::ID: {
      const auto *reason = static_cast<const td_api::messageSuggestedPostRefunded *>(content)->reason_.get();
      if (reason != nullptr && reason->get_id() == td_api::suggestedPostRefundReasonPaymentRefunded::ID) {
        return "The user refunded the payment, post was deleted.";
      }
      return "The admin deleted the post early, the payment was refunded.";
    }
    case td_api::messageSuggestBirthdate::ID: {
      const auto *birthdate = static_cast<const td_api::messageSuggestBirthdate *>(content)->birthdate_.get();
      int32 day = birthdate == nullptr ? 0 : birthdate->day_;
      int32 month = birthdate == nullptr ? 0 : birthdate->month_;
      int32 year = birthdate == nullptr ? 0 : birthdate->year_;
      string result = PSTRING() << from << " suggests to add a date of birth: " << day;
      if (1 <= month && month <= 12) {
        result += ' ';
        result += get_month_name(month).str();
      }
      if (year != 0) {
        result += PSTRING() << ' ' << year;
      }
      return result;
    }
    case td_api::messageChatHasProtectedContentToggled::ID:
      return PSTRING()
             << from
             << (static_cast<const td_api::messageChatHasProtectedContentToggled *>(content)->new_has_protected_content_
                     ? " disabled sharing in this chat"
                     : " enabled sharing in this chat");
    case td_api::messageChatHasProtectedContentDisableRequested::ID:
      return PSTRING() << from << " requested to enable sharing in this chat";
    case td_api::messageChatOwnerLeft::ID:
      return PSTRING() << get_user_name(static_cast<const td_api::messageChatOwnerLeft *>(content)->new_owner_user_id_)
                       << " will become the new main admin in 7 days if " << from << " does not return";
    case td_api::messageChatOwnerChanged::ID:
      return PSTRING() << from << " made "
                       << get_user_name(
                              static_cast<const td_api::messageChatOwnerChanged *>(content)->new_owner_user_id_)
                       << " the new main admin of the group";
    case td_api::messageManagedBotCreated::ID:
      return PSTRING() << from << " created a bot "
                       << get_user_name(static_cast<const td_api::messageManagedBotCreated *>(content)->bot_user_id_);
    case td_api::messageTonWalletTransfer::ID: {
      const auto *m = static_cast<const td_api::messageTonWalletTransfer *>(content);
      auto amount = format_grams_amount(m->amount_);
      string address;
      if (!m->peer_address_.empty()) {
        address = PSTRING() << " (" << html_escape(m->peer_address_) << ')';
      }
      // the senders of messages in the chats with the notification accounts aren't shown
      bool is_hidden = dialog_.dialog_id == DialogId(UserId(static_cast<int64>(333000))) ||
                       dialog_.dialog_id == DialogId(UserId(static_cast<int64>(777000)));
      string result;
      if (message_.is_outgoing_) {
        result = PSTRING() << "You sent " << amount << " to " << get_peer_name(dialog_.dialog_id) << address;
      } else {
        result = PSTRING() << (is_hidden ? string("Someone") : from) << address << " sent you " << amount;
      }
      if (!m->transaction_id_.empty()) {
        result += ", transaction ";
        result += html_escape(m->transaction_id_);
      }
      if (m->is_comment_encrypted_) {
        result += ", with an ";
        context_.push_tag(
            result, "span",
            {{"class", "gram_transfer_encrypted_comment"}, {"data-encrypted-comment", m->comment_}, {"inline", ""}});
        result += "encrypted comment";
        context_.pop_tag(result);
      } else if (!m->comment_.empty()) {
        result += PSTRING() << ", with comment: " << quoted(m->comment_);
      }
      return result;
    }
    case td_api::messageTonConnectRequest::ID: {
      const auto *m = static_cast<const td_api::messageTonConnectRequest *>(content);
      string topic;
      if (!m->topic_.empty()) {
        topic = PSTRING() << ' ' << quoted(m->topic_);
      }
      auto state_id = m->state_ == nullptr ? 0 : m->state_->get_id();
      if (state_id == td_api::tonConnectRequestStateAccepted::ID) {
        return PSTRING() << "You approved a TON Connect request" << topic << '.';
      }
      if (state_id == td_api::tonConnectRequestStateRejected::ID) {
        return PSTRING() << "You declined a TON Connect request" << topic << '.';
      }
      return PSTRING() << "You received a TON Connect request" << topic << '.';
    }
    default:
      UNREACHABLE();
      return string();
  }
}

const td_api::chatPhoto *ExportHtmlMessage::get_service_photo(const td_api::MessageContent *content) const {
  switch (content->get_id()) {
    case td_api::messageChatChangePhoto::ID:
      return static_cast<const td_api::messageChatChangePhoto *>(content)->photo_.get();
    case td_api::messageSuggestProfilePhoto::ID:
      return static_cast<const td_api::messageSuggestProfilePhoto *>(content)->photo_.get();
    default:
      return nullptr;
  }
}

ExportHtmlMessageInfo ExportHtmlMessage::get_message_info() const {
  ExportHtmlMessageInfo info;
  info.id = get_message_id();
  info.sender_dialog_id = get_export_message_sender_dialog_id(message_.sender_id_.get());
  info.via_bot_user_id = message_.via_bot_user_id_;
  info.date = message_.date_;
  const auto *forward_info = message_.forward_info_.get();
  if (forward_info != nullptr && forward_info->origin_ != nullptr) {
    const auto *origin = forward_info->origin_.get();
    switch (origin->get_id()) {
      case td_api::messageOriginUser::ID:
        info.forwarded_from_dialog_id =
            DialogId(UserId(static_cast<const td_api::messageOriginUser *>(origin)->sender_user_id_));
        break;
      case td_api::messageOriginHiddenUser::ID:
        info.forwarded_from_name = static_cast<const td_api::messageOriginHiddenUser *>(origin)->sender_name_;
        break;
      case td_api::messageOriginChat::ID:
        info.forwarded_from_dialog_id =
            DialogId(static_cast<const td_api::messageOriginChat *>(origin)->sender_chat_id_);
        break;
      case td_api::messageOriginChannel::ID:
        info.forwarded_from_dialog_id = DialogId(static_cast<const td_api::messageOriginChannel *>(origin)->chat_id_);
        break;
      default:
        UNREACHABLE();
    }
    info.forwarded_date = forward_info->date_;
  } else if (message_.import_info_ != nullptr) {
    info.forwarded_from_name = message_.import_info_->sender_name_;
    info.forwarded_date = message_.import_info_->date_;
  }
  info.is_forwarded = info.forwarded_from_dialog_id.is_valid() || !info.forwarded_from_name.empty();
  info.show_forwarded_as_original = info.is_forwarded && forward_info != nullptr && forward_info->source_ != nullptr &&
                                    forward_info->source_->chat_id_ != 0;
  return info;
}

bool ExportHtmlMessage::message_needs_wrap(const ExportHtmlMessageInfo &info,
                                           const ExportHtmlMessageInfo *previous) const {
  if (previous == nullptr || previous->is_service) {
    return true;
  }
  if (!info.sender_dialog_id.is_valid() || previous->sender_dialog_id != info.sender_dialog_id) {
    return true;
  }
  if (info.via_bot_user_id != previous->via_bot_user_id) {
    return true;
  }
  if (get_local_day(previous->date, settings_.utc_time_offset) != get_local_day(info.date, settings_.utc_time_offset)) {
    return true;
  }
  if (info.is_forwarded != previous->is_forwarded ||
      info.show_forwarded_as_original != previous->show_forwarded_as_original ||
      info.forwarded_from_dialog_id != previous->forwarded_from_dialog_id ||
      info.forwarded_from_name != previous->forwarded_from_name) {
    return true;
  }
  auto join_within =
      info.forwarded_from_dialog_id.is_valid() || !info.forwarded_from_name.empty() ? 1 : JOIN_WITHIN_SECONDS;
  return std::abs(info.date - previous->date) > join_within;
}

bool ExportHtmlMessage::forwarded_needs_wrap(const ExportHtmlMessageInfo &info,
                                             const ExportHtmlMessageInfo *previous) const {
  CHECK(info.is_forwarded);
  if (message_needs_wrap(info, previous)) {
    return true;
  }
  if (!info.forwarded_from_dialog_id.is_valid() ||
      info.forwarded_from_dialog_id != previous->forwarded_from_dialog_id) {
    return true;
  }
  if (info.forwarded_from_dialog_id.get_type() != DialogType::User) {
    return true;
  }
  return std::abs(info.forwarded_date - previous->forwarded_date) > JOIN_WITHIN_SECONDS;
}

static MediaData get_audio_media_data(Slice performer, Slice title, Slice file_name, int32 duration, int64 file_size,
                                      const ExportFile &file) {
  MediaData result;
  if (!performer.empty() && !title.empty()) {
    result.title = PSTRING() << performer << " \xE2\x80\x93 " << title;
  } else if (!file_name.empty()) {
    result.title = file_name.str();
  } else {
    result.title = "Audio file";
  }
  result.status = format_duration(duration);
  if (file.relative_path.empty()) {
    result.status += ", ";
    result.status += format_file_size(file_size);
  }
  result.classes = "media_audio_file";
  result.link = file.relative_path;
  result.description = get_no_file_description(file.skip_reason).str();
  return result;
}

static MediaData get_file_media_data(Slice file_name, const td_api::file *document_file, const ExportFile &file) {
  MediaData result;
  result.title = file_name.empty() ? string("File") : file_name.str();
  result.status = format_file_size(get_export_file_size(document_file));
  result.classes = "media_file";
  result.link = file.relative_path;
  result.description = get_no_file_description(file.skip_reason).str();
  return result;
}

static string get_map_link(const td_api::location *location) {
  auto coordinates = PSTRING() << format_coordinate(location->latitude_) << ','
                               << format_coordinate(location->longitude_);
  return PSTRING() << "https://maps.google.com/maps?q=" << coordinates << "&ll=" << coordinates << "&z=16";
}

MediaData ExportHtmlMessage::prepare_media_data(const td_api::MessageContent *content) const {
  MediaData result;
  auto is_self_destructing = message_.self_destruct_type_ != nullptr;
  auto set_call = [&](bool is_invitation, bool is_active, bool is_missed, bool is_busy, int32 duration) {
    result.classes = "media_call";
    result.title = get_raw_dialog_name(message_.is_outgoing_ ? dialog_.dialog_id : get_my_dialog_id());
    if (is_invitation) {
      result.status = "Invitation";
    } else if (is_active) {
      result.status = "Ongoing";
    } else if (message_.is_outgoing_) {
      result.status = is_missed ? "Cancelled" : "Outgoing";
    } else if (is_missed) {
      result.status = "Missed";
    } else if (is_busy) {
      result.status = "Declined";
    } else {
      result.status = "Incoming";
    }
    if (duration > 0) {
      result.classes += " success";
      result.status += PSTRING() << " (" << duration << " seconds)";
    }
  };
  auto set_self_destructing = [&](Slice title, bool is_expired, Slice classes) {
    result.title = title.str();
    result.status = is_expired ? "Expired" : "Please view it on your mobile";
    result.classes = classes.str();
  };
  auto set_document = [&](const td_api::file *document_file) {
    result.link = file_.relative_path;
    result.description = get_no_file_description(file_.skip_reason).str();
    return file_.relative_path.empty() ? PSTRING() << ", " << format_file_size(get_export_file_size(document_file))
                                       : string();
  };

  switch (content->get_id()) {
    case td_api::messageCall::ID: {
      const auto *m = static_cast<const td_api::messageCall *>(content);
      auto reason_id = m->discard_reason_ == nullptr ? 0 : m->discard_reason_->get_id();
      set_call(false, false, reason_id == td_api::callDiscardReasonMissed::ID,
               reason_id == td_api::callDiscardReasonDeclined::ID, m->duration_);
      break;
    }
    case td_api::messageGroupCall::ID: {
      const auto *m = static_cast<const td_api::messageGroupCall *>(content);
      auto is_missed = m->was_missed_;
      auto is_active = !is_missed && m->is_active_;
      auto is_invitation = !is_missed && !is_active && m->duration_ == 0;
      set_call(is_invitation, is_active, is_missed, false, m->duration_);
      break;
    }
    case td_api::messagePhoto::ID:
      if (is_self_destructing) {
        set_self_destructing("Self-destructing photo", false, "media_photo");
      }
      break;
    case td_api::messageExpiredPhoto::ID:
      set_self_destructing("Self-destructing photo", true, "media_photo");
      break;
    case td_api::messageAnimation::ID:
    case td_api::messageVideo::ID:
    case td_api::messageVideoNote::ID:
    case td_api::messageVoiceNote::ID:
      if (is_self_destructing) {
        set_self_destructing("Self-destructing video", false, "media_video");
        break;
      }
      if (content->get_id() == td_api::messageVideoNote::ID) {
        const auto *video_note = static_cast<const td_api::messageVideoNote *>(content)->video_note_.get();
        auto size = set_document(video_note->video_.get());
        result.title = "Video message";
        result.status = format_duration(video_note->duration_) + size;
        result.thumbnail = thumbnail_.relative_path;
        result.classes = "media_video";
      } else if (content->get_id() == td_api::messageVoiceNote::ID) {
        const auto *voice_note = static_cast<const td_api::messageVoiceNote *>(content)->voice_note_.get();
        auto size = set_document(voice_note->voice_.get());
        result.title = "Voice message";
        result.status = format_duration(voice_note->duration_) + size;
        result.classes = "media_voice_message";
      }
      break;
    case td_api::messageExpiredVideo::ID:
    case td_api::messageExpiredVideoNote::ID:
    case td_api::messageExpiredVoiceNote::ID:
      set_self_destructing("Self-destructing video", true, "media_video");
      break;
    case td_api::messageAudio::ID: {
      const auto *audio = static_cast<const td_api::messageAudio *>(content)->audio_.get();
      result = get_audio_media_data(audio->performer_, audio->title_, audio->file_name_, audio->duration_,
                                    get_export_file_size(audio->audio_.get()), file_);
      break;
    }
    case td_api::messageDocument::ID: {
      const auto *document = static_cast<const td_api::messageDocument *>(content)->document_.get();
      result = get_file_media_data(document->file_name_, document->document_.get(), file_);
      break;
    }
    case td_api::messageContact::ID: {
      const auto *contact = static_cast<const td_api::messageContact *>(content)->contact_.get();
      result.title = PSTRING() << contact->first_name_ << ' ' << contact->last_name_;
      result.classes = "media_contact";
      result.status = format_export_phone_number(contact->phone_number_);
      if (!contact->vcard_.empty()) {
        result.status += " - vCard";
        result.link = file_.relative_path;
      }
      break;
    }
    case td_api::messageLocation::ID:
    case td_api::messageLiveLocation::ID: {
      const td_api::location *location;
      if (content->get_id() == td_api::messageLiveLocation::ID) {
        const auto *live_location = static_cast<const td_api::messageLiveLocation *>(content)->location_.get();
        location = live_location == nullptr ? nullptr : live_location->location_.get();
        result.classes = "media_live_location";
        result.title = "Live location";
      } else {
        location = static_cast<const td_api::messageLocation *>(content)->location_.get();
        result.classes = "media_location";
        result.title = "Location";
      }
      if (location != nullptr) {
        result.status = PSTRING() << format_coordinate(location->latitude_) << ", "
                                  << format_coordinate(location->longitude_);
        result.link = get_map_link(location);
      }
      break;
    }
    case td_api::messageVenue::ID: {
      const auto *venue = static_cast<const td_api::messageVenue *>(content)->venue_.get();
      result.classes = "media_venue";
      result.title = venue->title_;
      result.description = venue->address_;
      if (venue->location_ != nullptr) {
        result.link = get_map_link(venue->location_.get());
      }
      break;
    }
    case td_api::messageGame::ID: {
      const auto *game = static_cast<const td_api::messageGame *>(content)->game_.get();
      result.classes = "media_game";
      result.title = game->title_;
      result.description = game->description_;
      UserId bot_user_id;
      if (message_.via_bot_user_id_ != 0) {
        bot_user_id = UserId(message_.via_bot_user_id_);
      } else {
        auto info = get_message_info();
        auto bot_dialog_id = info.forwarded_from_dialog_id.get_type() == DialogType::User
                                 ? info.forwarded_from_dialog_id
                                 : info.sender_dialog_id;
        if (bot_dialog_id.get_type() == DialogType::User) {
          bot_user_id = bot_dialog_id.get_user_id();
        }
      }
      if (bot_user_id.is_valid() && !game->short_name_.empty() && td_->user_manager_->is_user_bot(bot_user_id)) {
        auto username = td_->user_manager_->get_user_first_username(bot_user_id);
        if (!username.empty()) {
          result.link = PSTRING() << LinkManager::get_t_me_url() << username << "?game=" << game->short_name_;
          result.status = result.link;
        }
      }
      break;
    }
    case td_api::messageInvoice::ID: {
      const auto *m = static_cast<const td_api::messageInvoice *>(content);
      result.classes = "media_invoice";
      result.title = m->product_info_->title_;
      if (m->product_info_->description_ != nullptr) {
        result.description = m->product_info_->description_->text_;
      }
      result.status = format_export_money_amount(m->total_amount_, m->currency_);
      break;
    }
    case td_api::messagePaidMedia::ID:
      result.classes = "media_invoice";
      result.status =
          format_export_money_amount(static_cast<const td_api::messagePaidMedia *>(content)->star_count_, "XTR");
      break;
    default:
      break;
  }
  return result;
}

void ExportHtmlMessage::push_media(string &result, const td_api::MessageContent *content) {
  auto data = prepare_media_data(content);
  if (!data.classes.empty()) {
    return push_generic_media(result, data);
  }
  switch (content->get_id()) {
    case td_api::messagePhoto::ID:
      return push_photo_media(result,
                              get_export_photo_size(static_cast<const td_api::messagePhoto *>(content)->photo_->sizes_),
                              file_, thumbnail_);
    case td_api::messageSticker::ID:
      return push_sticker_media(result, static_cast<const td_api::messageSticker *>(content)->sticker_.get());
    case td_api::messageAnimation::ID:
      return push_animated_media(result, static_cast<const td_api::messageAnimation *>(content)->animation_.get());
    case td_api::messageVideo::ID: {
      const auto *video = static_cast<const td_api::messageVideo *>(content)->video_.get();
      return push_video_file_media(result, video->width_, video->height_, video->duration_,
                                   get_export_file_size(video->video_.get()), file_, thumbnail_);
    }
    case td_api::messagePoll::ID:
      return push_poll(result, static_cast<const td_api::messagePoll *>(content)->poll_.get());
    case td_api::messageChecklist::ID:
      return push_checklist(result, static_cast<const td_api::messageChecklist *>(content)->list_.get());
    case td_api::messageGiveaway::ID:
      return push_giveaway(result, static_cast<const td_api::messageGiveaway *>(content));
    case td_api::messageGiveawayWinners::ID:
      return push_giveaway_winners(result, static_cast<const td_api::messageGiveawayWinners *>(content));
    default:
      break;
  }
}

void ExportHtmlMessage::push_generic_media(string &result, const MediaData &data) {
  context_.push_div(result, "media_wrap clearfix");
  if (data.link.empty()) {
    context_.push_div(result, PSLICE() << "media clearfix pull_left " << data.classes);
  } else {
    auto href = is_global_link(data.link) ? data.link : PSTRING() << base_ << data.link;
    context_.push_tag(result, "a",
                      {{"class", PSLICE() << "media clearfix pull_left block_link " << data.classes}, {"href", href}});
  }
  if (data.thumbnail.empty()) {
    context_.push_div(result, "fill pull_left");
    context_.pop_tag(result);
  } else {
    context_.push_tag(result, "img",
                      {{"class", "thumb pull_left"}, {"src", PSLICE() << base_ << data.thumbnail}, {"empty", ""}});
  }
  context_.push_div(result, "body");
  if (!data.title.empty()) {
    context_.push_div(result, "title bold");
    append_html_escaped(result, data.title);
    context_.pop_tag(result);
  }
  if (!data.description.empty()) {
    context_.push_div(result, "description");
    append_html_escaped(result, data.description);
    context_.pop_tag(result);
  }
  if (!data.status.empty()) {
    context_.push_div(result, "status details");
    append_html_escaped(result, data.status);
    context_.pop_tag(result);
  }
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_photo_media(string &result, const td_api::photoSize *size, const ExportFile &file,
                                         const ExportFile &thumbnail) {
  auto thumbnail_size = file.relative_path.empty() || size == nullptr
                            ? ExportHtmlImageSize()
                            : get_thumbnail_size(size->width_, size->height_, EXPORT_HTML_PHOTO_SIZE, false);
  if (thumbnail_size.empty()) {
    MediaData generic;
    generic.title = "Photo";
    if (size != nullptr) {
      generic.status = PSTRING() << size->width_ << "\xC3\x97" << size->height_;
    }
    if (file.relative_path.empty()) {
      generic.status += ", ";
      generic.status += format_file_size(size == nullptr ? 0 : get_export_file_size(size->photo_.get()));
    } else {
      generic.link = file.relative_path;
    }
    generic.description = get_no_file_description(file.skip_reason).str();
    generic.classes = "media_photo";
    return push_generic_media(result, generic);
  }
  // the downloaded smaller photo is used as the thumbnail, or the photo itself if there is none
  const auto &image = thumbnail.relative_path.empty() ? file.relative_path : thumbnail.relative_path;
  context_.push_div(result, "media_wrap clearfix");
  context_.push_tag(result, "a",
                    {{"class", "photo_wrap clearfix pull_left"}, {"href", PSLICE() << base_ << file.relative_path}});
  context_.push_tag(result, "img",
                    {{"class", "photo"},
                     {"style", get_size_style(thumbnail_size.width / 2, thumbnail_size.height / 2)},
                     {"src", PSLICE() << base_ << image},
                     {"empty", ""}});
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_sticker_media(string &result, const td_api::sticker *sticker) {
  // only static stickers can be shown by a browser
  auto is_static = sticker->format_ != nullptr && sticker->format_->get_id() == td_api::stickerFormatWebp::ID;
  auto thumbnail_size = file_.relative_path.empty() || !is_static
                            ? ExportHtmlImageSize()
                            : get_thumbnail_size(sticker->width_, sticker->height_, STICKER_MAX_SIZE, false);
  if (thumbnail_size.empty()) {
    MediaData generic;
    generic.title = "Sticker";
    generic.status = sticker->emoji_;
    if (file_.relative_path.empty()) {
      if (!generic.status.empty()) {
        generic.status += ", ";
      }
      generic.status += format_file_size(get_export_file_size(sticker->sticker_.get()));
    } else {
      generic.link = file_.relative_path;
    }
    generic.description = get_no_file_description(file_.skip_reason).str();
    generic.classes = "media_photo";
    return push_generic_media(result, generic);
  }
  auto link = PSTRING() << base_ << file_.relative_path;
  context_.push_div(result, "media_wrap clearfix");
  context_.push_tag(result, "a", {{"class", "sticker_wrap clearfix pull_left"}, {"href", link}});
  context_.push_tag(result, "img",
                    {{"class", "sticker"},
                     {"style", get_size_style(thumbnail_size.width / 2, thumbnail_size.height / 2)},
                     {"src", link},
                     {"empty", ""}});
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_animated_media(string &result, const td_api::animation *animation) {
  auto thumbnail_size = get_thumbnail_size(animation->width_, animation->height_, EXPORT_HTML_PHOTO_SIZE, true);
  if (thumbnail_.relative_path.empty() || file_.relative_path.empty() || thumbnail_size.empty()) {
    MediaData generic;
    generic.title = "Animation";
    generic.status = format_file_size(get_export_file_size(animation->animation_.get()));
    generic.link = file_.relative_path;
    generic.description = get_no_file_description(file_.skip_reason).str();
    generic.classes = "media_video";
    return push_generic_media(result, generic);
  }
  context_.push_div(result, "media_wrap clearfix");
  context_.push_tag(
      result, "a", {{"class", "animated_wrap clearfix pull_left"}, {"href", PSLICE() << base_ << file_.relative_path}});
  context_.push_div(result, "video_play_bg");
  context_.push_div(result, "gif_play");
  result += "GIF";
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.push_tag(result, "img",
                    {{"class", "animated"},
                     {"style", get_size_style(thumbnail_size.width / 2, thumbnail_size.height / 2)},
                     {"src", PSLICE() << base_ << thumbnail_.relative_path},
                     {"empty", ""}});
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_video_file_media(string &result, int32 width, int32 height, int32 duration,
                                              int64 file_size, const ExportFile &file, const ExportFile &thumbnail) {
  auto thumbnail_size = get_thumbnail_size(width, height, EXPORT_HTML_PHOTO_SIZE, true);
  if (thumbnail.relative_path.empty() || file.relative_path.empty() || thumbnail_size.empty()) {
    MediaData generic;
    generic.title = "Video file";
    generic.status = format_duration(duration);
    if (file.relative_path.empty()) {
      generic.status += ", ";
      generic.status += format_file_size(file_size);
    } else {
      generic.link = file.relative_path;
    }
    generic.description = get_no_file_description(file.skip_reason).str();
    generic.classes = "media_video";
    return push_generic_media(result, generic);
  }
  context_.push_div(result, "media_wrap clearfix");
  context_.push_tag(
      result, "a",
      {{"class", "video_file_wrap clearfix pull_left"}, {"href", PSLICE() << base_ << file.relative_path}});
  context_.push_div(result, "video_play_bg");
  context_.push_div(result, "video_play");
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.push_div(result, "video_duration");
  result += format_duration(duration);
  context_.pop_tag(result);
  context_.push_tag(result, "img",
                    {{"class", "video_file"},
                     {"style", get_size_style(thumbnail_size.width / 2, thumbnail_size.height / 2)},
                     {"src", PSLICE() << base_ << thumbnail.relative_path},
                     {"empty", ""}});
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_rich_photo_media(string &result, const ExportRichPhoto *photo) {
  // a missing photo is shown like an unavailable photo with zero width and height
  td_api::photoSize empty_size;
  const td_api::photoSize *size = &empty_size;
  if (photo != nullptr && photo->photo != nullptr) {
    const auto *largest_size = get_export_photo_size(photo->photo->sizes_);
    if (largest_size != nullptr) {
      size = largest_size;
    }
  }
  auto file = get_rich_file_presentation(photo == nullptr ? nullptr : &photo->file);
  push_photo_media(result, size, file, photo == nullptr ? file : photo->thumbnail);
}

void ExportHtmlMessage::push_rich_video_media(string &result, const ExportRichDocument *document) {
  if (document == nullptr) {
    auto file = get_rich_file_presentation(nullptr);
    return push_video_file_media(result, 0, 0, 0, 0, file, file);
  }
  push_video_file_media(result, document->width, document->height, document->duration,
                        get_export_file_size(document->document.get()), get_rich_file_presentation(&document->file),
                        get_rich_file_presentation(&document->thumbnail_file));
}

void ExportHtmlMessage::push_rich_audio_media(string &result, const ExportRichDocument *document) {
  if (document == nullptr) {
    return push_generic_media(
        result, get_audio_media_data(Slice(), Slice(), Slice(), 0, 0, get_rich_file_presentation(nullptr)));
  }
  push_generic_media(result, get_audio_media_data(document->performer, document->title, document->file_name,
                                                  document->duration, get_export_file_size(document->document.get()),
                                                  get_rich_file_presentation(&document->file)));
}

void ExportHtmlMessage::push_rich_file_media(string &result, const ExportRichDocument *document) {
  if (document == nullptr) {
    return push_generic_media(result, get_file_media_data(Slice(), nullptr, get_rich_file_presentation(nullptr)));
  }
  push_generic_media(result, get_file_media_data(document->file_name, document->document.get(),
                                                 get_rich_file_presentation(&document->file)));
}

void ExportHtmlMessage::push_rich_reference_media(string &result, MediaData data, const ExportRichPhoto *photo) {
  data.link = get_rich_file_presentation(photo == nullptr ? nullptr : &photo->file).relative_path;
  if (!data.link.empty()) {
    data.thumbnail = photo->card_thumbnail.relative_path.empty() ? data.link : photo->card_thumbnail.relative_path;
  }
  push_generic_media(result, data);
}

static string format_vote_count(int32 count) {
  if (count > 1) {
    return PSTRING() << count << " votes";
  }
  if (count > 0) {
    return PSTRING() << count << " vote";
  }
  return "No votes";
}

void ExportHtmlMessage::push_poll(string &result, const td_api::poll *poll) {
  context_.push_div(result, "media_wrap clearfix");
  context_.push_div(result, "media_poll");
  context_.push_div(result, "question bold");
  result += format_text(poll->question_.get());
  context_.pop_tag(result);
  context_.push_div(result, "details");
  result += poll->is_closed_ ? "Final results" : "Anonymous poll";
  context_.pop_tag(result);
  for (const auto &option : poll->options_) {
    context_.push_div(result, "answer");
    result += "- ";
    result += format_text(option->text_.get());
    if (option->voter_count_ != 0) {
      result += " <span class=\"details\">";
      result += format_vote_count(option->voter_count_);
      if (option->is_chosen_) {
        result += ", chosen vote";
      }
      result += "</span>";
    }
    context_.pop_tag(result);
  }
  context_.push_div(result, "total details\t");
  result += format_vote_count(poll->total_voter_count_);
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_checklist(string &result, const td_api::checklist *checklist) {
  context_.push_div(result, "media_wrap clearfix");
  context_.push_div(result, "media_poll");
  context_.push_div(result, "question bold");
  result += format_text(checklist->title_.get());
  context_.pop_tag(result);
  context_.push_div(result, "details");
  result += "To-do List";
  context_.pop_tag(result);
  for (const auto &task : checklist->tasks_) {
    context_.push_div(result, "answer");
    result += "- ";
    result += format_text(task->text_.get());
    context_.pop_tag(result);
  }
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_giveaway(string &result, const td_api::messageGiveaway *giveaway) {
  const auto *parameters = giveaway->parameters_.get();
  int32 month_count;
  int64 star_count;
  get_export_giveaway_prize(giveaway->prize_.get(), month_count, star_count);
  auto quantity = giveaway->winner_count_;

  context_.push_div(result, "media_wrap clearfix");
  context_.push_div(result, "media_giveaway");

  context_.push_div(result, "section_title bold");
  result += quantity > 1 ? "Giveaway Prizes" : "Giveaway Prize";
  context_.pop_tag(result);

  context_.push_div(result, "section_body");
  result += PSTRING() << "<b>" << quantity << "</b> " << html_escape(parameters->prize_description_);
  context_.pop_tag(result);
  context_.push_div(result, "section_title bold");
  result += "with";
  context_.pop_tag(result);

  context_.push_div(result, "section_body");
  if (star_count > 0) {
    result += PSTRING() << "<b>" << star_count << (star_count == 1 ? " Star" : " Stars") << "</b> will be distributed "
                        << (quantity == 1 ? "to " : "among ") << "<b>" << quantity << "</b> "
                        << (quantity == 1 ? "winner." : "winners.");
  } else {
    result += PSTRING() << "<b>" << quantity << "</b> "
                        << (quantity > 1 ? "Telegram Premium Subscriptions" : "Telegram Premium Subscription")
                        << " for <b>" << month_count << "</b> " << (month_count > 1 ? "months." : "month.");
  }
  context_.pop_tag(result);

  context_.push_div(result, "section_title bold");
  result += "Participants";
  context_.pop_tag(result);
  context_.push_div(result, "section_body");
  vector<DialogId> channel_dialog_ids{DialogId(parameters->boosted_chat_id_)};
  for (auto chat_id : parameters->additional_chat_ids_) {
    channel_dialog_ids.push_back(DialogId(chat_id));
  }
  bool has_channel = false;
  bool has_group = false;
  for (auto dialog_id : channel_dialog_ids) {
    if (dialog_id.get_type() != DialogType::Channel) {
      continue;
    }
    if (td_->dialog_manager_->is_broadcast_channel(dialog_id)) {
      has_channel = true;
    } else {
      has_group = true;
    }
  }
  bool is_all = !parameters->only_new_members_;
  bool is_single = channel_dialog_ids.size() == 1;
  Slice participants;
  if (has_group && has_channel && !is_single) {
    participants = is_all ? Slice("All members of the groups and channels:")
                          : Slice("All users who joined the groups and channels below after this date:");
  } else if (has_group) {
    if (is_all) {
      participants = is_single ? Slice("All members of the group:") : Slice("All members of the groups:");
    } else {
      participants = is_single ? Slice("All users who joined the group below after this date:")
                               : Slice("All users who joined the groups below after this date:");
    }
  } else if (has_channel) {
    if (is_all) {
      participants = is_single ? Slice("All subscribers of the channel:") : Slice("All subscribers of the channels:");
    } else {
      participants = is_single ? Slice("All users who joined the channel below after this date:")
                               : Slice("All users who joined the channels below after this date:");
    }
  }
  result.append(participants.data(), participants.size());
  context_.pop_tag(result);

  if (!parameters->country_codes_.empty()) {
    vector<string> countries;
    for (const auto &country_code : parameters->country_codes_) {
      // the names of countries aren't known without loading the country list
      countries.push_back(PSTRING() << CountryInfoManager::get_country_flag_emoji(country_code) << "\xC2\xA0"
                                    << country_code);
    }
    context_.push_div(result, "section_body");
    result += "from ";
    append_html_escaped(result, join_html_list(countries));
    context_.pop_tag(result);
  }
  context_.push_div(result, "section_title bold");
  result += "Winners Selection Date";
  context_.pop_tag(result);
  context_.push_div(result, "section_body");
  result += format_date_time(parameters->winners_selection_date_, settings_.utc_time_offset, false);
  context_.pop_tag(result);

  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_giveaway_winners(string &result, const td_api::messageGiveawayWinners *winners) {
  int32 month_count;
  int64 star_count;
  get_export_giveaway_prize(winners->prize_.get(), month_count, star_count);
  auto winner_count = winners->winner_count_;

  context_.push_div(result, "media_wrap clearfix");
  context_.push_div(result, "media_giveaway");

  context_.push_div(result, "section_title bold");
  result += winner_count > 1 ? "Winners Selected!" : "Winner Selected!";
  context_.pop_tag(result);

  context_.push_div(result, "section_body");
  result += PSTRING() << "<b>" << winner_count << "</b> " << (winner_count > 1 ? "winners" : "winner") << " of the "
                      << (winners->boosted_chat_id_ == message_.chat_id_
                              ? wrap_message_link(get_same_chat_message_id(winners->giveaway_message_id_), "Giveaway")
                              : string("Giveaway"))
                      << " was randomly selected by Telegram.";
  context_.pop_tag(result);

  context_.push_div(result, "section_title bold");
  result += winner_count > 1 ? "Winners" : "Winner";
  context_.pop_tag(result);

  context_.push_div(result, "section_body");
  vector<string> names;
  for (auto user_id : winners->winner_user_ids_) {
    names.push_back(PSTRING() << "<b>" << get_peer_name(DialogId(UserId(user_id))) << "</b>");
  }
  result += join_strings(names, ", ");
  auto shown_count = static_cast<int32>(winners->winner_user_ids_.size());
  if (winner_count > shown_count) {
    result += PSTRING() << " and " << winner_count - shown_count << " more!";
  }
  context_.pop_tag(result);

  context_.push_div(result, "section_body");
  if (star_count != 0 && winner_count == 1) {
    result += PSTRING() << "The winner received <b>" << star_count << "</b>"
                        << (star_count == 1 ? " Star." : " Stars.");
  } else if (star_count != 0 && winner_count > 1) {
    result += PSTRING() << "All winners received <b>" << star_count << "</b>"
                        << (star_count == 1 ? " Star in total." : " Stars in total.");
  } else if (winners->unclaimed_prize_count_ != 0) {
    result += "Some winners couldn&apos;t be selected.";
  } else if (winner_count == 1) {
    result += "The winner received their gift link in a private message.";
  } else if (winner_count > 1) {
    result += "All winners received gift links in private messages.";
  }
  context_.pop_tag(result);

  context_.pop_tag(result);
  context_.pop_tag(result);
}

static Slice get_inline_button_type_name(const td_api::InlineKeyboardButtonType *type) {
  switch (type->get_id()) {
    case td_api::inlineKeyboardButtonTypeUrl::ID:
      return Slice("url");
    case td_api::inlineKeyboardButtonTypeLoginUrl::ID:
      return Slice("auth");
    case td_api::inlineKeyboardButtonTypeWebApp::ID:
      return Slice("web_view");
    case td_api::inlineKeyboardButtonTypeCallback::ID:
      return Slice("callback");
    case td_api::inlineKeyboardButtonTypeCallbackWithPassword::ID:
      return Slice("callback_with_password");
    case td_api::inlineKeyboardButtonTypeCallbackGame::ID:
      return Slice("game");
    case td_api::inlineKeyboardButtonTypeSwitchInline::ID: {
      const auto *target_chat =
          static_cast<const td_api::inlineKeyboardButtonTypeSwitchInline *>(type)->target_chat_.get();
      return target_chat != nullptr && target_chat->get_id() == td_api::targetChatCurrent::ID
                 ? Slice("switch_inline_same")
                 : Slice("switch_inline");
    }
    case td_api::inlineKeyboardButtonTypeBuy::ID:
      return Slice("buy");
    case td_api::inlineKeyboardButtonTypeUser::ID:
      return Slice("user_profile");
    case td_api::inlineKeyboardButtonTypeCopyText::ID:
      return Slice("copy_text");
    case td_api::inlineKeyboardButtonTypeDisabled::ID:
      return Slice("disabled");
    default:
      UNREACHABLE();
      return Slice();
  }
}

static string get_inline_button_data(const td_api::InlineKeyboardButtonType *type, string &forward_text) {
  switch (type->get_id()) {
    case td_api::inlineKeyboardButtonTypeUrl::ID:
      return static_cast<const td_api::inlineKeyboardButtonTypeUrl *>(type)->url_;
    case td_api::inlineKeyboardButtonTypeLoginUrl::ID: {
      const auto *login_url = static_cast<const td_api::inlineKeyboardButtonTypeLoginUrl *>(type);
      forward_text = login_url->forward_text_;
      return login_url->url_;
    }
    case td_api::inlineKeyboardButtonTypeWebApp::ID:
      return static_cast<const td_api::inlineKeyboardButtonTypeWebApp *>(type)->url_;
    case td_api::inlineKeyboardButtonTypeCallback::ID:
      return static_cast<const td_api::inlineKeyboardButtonTypeCallback *>(type)->data_;
    case td_api::inlineKeyboardButtonTypeCallbackWithPassword::ID:
      return static_cast<const td_api::inlineKeyboardButtonTypeCallbackWithPassword *>(type)->data_;
    case td_api::inlineKeyboardButtonTypeSwitchInline::ID:
      return static_cast<const td_api::inlineKeyboardButtonTypeSwitchInline *>(type)->query_;
    case td_api::inlineKeyboardButtonTypeUser::ID:
      return to_string(static_cast<const td_api::inlineKeyboardButtonTypeUser *>(type)->user_id_);
    case td_api::inlineKeyboardButtonTypeCopyText::ID:
      return static_cast<const td_api::inlineKeyboardButtonTypeCopyText *>(type)->text_;
    default:
      return string();
  }
}

void ExportHtmlMessage::push_inline_buttons(string &result) {
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
  context_.push_tag(result, "table", {{"class", "bot_buttons_table"}});
  context_.push_tag(result, "tbody");
  for (const auto &row : rows) {
    if (row.empty()) {
      continue;
    }
    context_.push_tag(result, "tr");
    context_.push_tag(result, "td", {{"class", "bot_button_row"}});
    for (size_t i = 0; i < row.size(); i++) {
      const auto *button_type = row[i]->type_.get();
      string forward_text;
      auto data = get_inline_button_data(button_type, forward_text);
      if (!check_utf8(data)) {
        // callback data are arbitrary bytes
        data = base64url_encode(data);
      }
      string content;
      if (!data.empty()) {
        content += PSTRING() << "Data: " << data << " | ";
      }
      if (!forward_text.empty()) {
        content += PSTRING() << "Forward text: " << forward_text << " | ";
      }
      content += PSTRING() << "Type: " << get_inline_button_type_name(button_type);

      // a bot chooses the link, so only safe links are opened
      string link;
      string onclick;
      if (button_type->get_id() == td_api::inlineKeyboardButtonTypeUrl::ID) {
        link = get_safe_link(data);
      } else {
        string escaped_content;
        for (auto c : content) {
          if (c == '\n') {
            escaped_content += "\\n";
          } else if (c == '\r') {
            escaped_content += "\\r";
          } else {
            if (c == '\\' || c == '\'') {
              escaped_content += '\\';
            }
            escaped_content += c;
          }
        }
        onclick = PSTRING() << "return ShowTextCopied('" << escaped_content << "');";
      }
      context_.push_tag(result, "div", {{"class", "bot_button"}});
      context_.push_tag(
          result, "a",
          {{link.empty() ? Slice() : Slice("href"), link}, {onclick.empty() ? Slice() : Slice("onclick"), onclick}});
      context_.push_tag(result, "div");
      append_html_escaped(result, row[i]->text_);
      context_.pop_tag(result);
      context_.pop_tag(result);
      context_.pop_tag(result);

      if (i + 1 != row.size()) {
        context_.push_tag(result, "div", {{"class", "bot_button_column_separator"}});
        context_.pop_tag(result);
      }
    }
    context_.pop_tag(result);
    context_.pop_tag(result);
  }
  context_.pop_tag(result);
  context_.pop_tag(result);
}

void ExportHtmlMessage::push_reactions(string &result) {
  const auto *interaction_info = message_.interaction_info_.get();
  if (interaction_info == nullptr || interaction_info->reactions_ == nullptr ||
      interaction_info->reactions_->reactions_.empty()) {
    return;
  }
  auto my_dialog_id = get_my_dialog_id();
  context_.push_tag(result, "span", {{"class", "reactions"}});
  for (const auto &reaction : interaction_info->reactions_->reactions_) {
    const auto *type = reaction->type_.get();
    auto type_id = type == nullptr ? 0 : type->get_id();
    string reaction_class = "reaction";
    for (const auto &sender_id : reaction->recent_sender_ids_) {
      if (get_export_message_sender_dialog_id(sender_id.get()) == my_dialog_id) {
        reaction_class += " active";
        break;
      }
    }
    if (type_id == td_api::reactionTypePaid::ID) {
      reaction_class += " paid";
    }

    context_.push_tag(result, "span", {{"class", reaction_class}});
    context_.push_tag(result, "span", {{"class", "emoji"}});
    switch (type_id) {
      case td_api::reactionTypeEmoji::ID:
        append_html_escaped(result, static_cast<const td_api::reactionTypeEmoji *>(type)->emoji_);
        break;
      case td_api::reactionTypeCustomEmoji::ID:
        result += format_custom_emoji(
            get_export_custom_emoji_path(&custom_emoji_files_,
                                         static_cast<const td_api::reactionTypeCustomEmoji *>(type)->custom_emoji_id_),
            "\xF0\x9F\x91\x8B", base_);
        break;
      case td_api::reactionTypePaid::ID:
        result += "\xE2\xAD\x90";
        break;
      default:
        break;
    }
    context_.pop_tag(result);
    if (!reaction->recent_sender_ids_.empty()) {
      context_.push_tag(result, "span", {{"class", "userpics"}});
      for (const auto &sender_id : reaction->recent_sender_ids_) {
        auto dialog_id = get_export_message_sender_dialog_id(sender_id.get());
        UserpicData userpic;
        userpic.color_index = get_peer_color_index(static_cast<uint64>(get_export_dialog_bare_id(dialog_id)));
        userpic.pixel_size = REACTION_USERPIC_SIZE;
        fill_userpic_names(userpic, dialog_id);
        userpic.tooltip = get_raw_dialog_name(dialog_id);
        push_userpic(context_, result, base_, userpic);
      }
      context_.pop_tag(result);
    }
    if (reaction->recent_sender_ids_.empty() ||
        reaction->total_count_ > static_cast<int32>(reaction->recent_sender_ids_.size())) {
      context_.push_tag(result, "span", {{"class", "count"}});
      result += to_string(reaction->total_count_);
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
  }
  context_.pop_tag(result);
}

class RichHtmlRenderer {
 public:
  RichHtmlRenderer(ExportHtmlMessage &message, const ExportRichMessage &rich_message)
      : message_(message), context_(message.context_), rich_message_(rich_message) {
  }

  void render_message(string &result) {
    collect_anchors();
    context_.push_tag(result, "div",
                      {{"class", "text rich_message"},
                       {"data-rich-message", PSLICE() << message_.get_message_id()},
                       {"data-rich-part", get_rich_bool(rich_message_.part)},
                       {"dir", rich_message_.rtl ? Slice("rtl") : Slice("auto")},
                       {"inline", ""}});
    render_blocks(result, rich_message_.blocks);
    context_.pop_tag(result);
  }

 private:
  ExportHtmlMessage &message_;
  HtmlContext &context_;
  const ExportRichMessage &rich_message_;
  bool is_link_active_ = false;
  std::map<string, string> first_anchor_ids_;
  std::map<string, int32> anchor_occurrences_;
  std::map<const ExportRichText *, string> text_anchor_ids_;
  std::map<const ExportRichBlock *, string> block_anchor_ids_;

  // sets is_link_active_ until the end of the scope, because links can't be nested
  class LinkGuard {
   public:
    explicit LinkGuard(bool &is_link_active) : is_link_active_(is_link_active), previous_(is_link_active) {
      is_link_active_ = true;
    }
    LinkGuard(const LinkGuard &) = delete;
    LinkGuard &operator=(const LinkGuard &) = delete;
    ~LinkGuard() {
      is_link_active_ = previous_;
    }

   private:
    bool &is_link_active_;
    bool previous_;
  };

  string format_date_time(int32 date) const {
    return td::format_date_time(date, message_.settings_.utc_time_offset, false);
  }

  string get_iso_date_time(int32 date) const {
    return get_export_date_time_string(get_export_local_time(date, message_.settings_.utc_time_offset));
  }

  const ExportRichPhoto *find_photo(int64 photo_id) const {
    return photo_id == 0 ? nullptr : rich_message_.get_photo(photo_id);
  }

  const ExportRichDocument *find_document(int64 document_id) const {
    return document_id == 0 ? nullptr : rich_message_.get_document(document_id);
  }

  bool get_media_item_size(const ExportRichBlock &block, ExportHtmlImageSize &size) const {
    if (block.kind == ExportRichBlock::Kind::Photo) {
      const auto *photo = find_photo(block.photo_id);
      if (photo == nullptr || photo->file.relative_path.empty() || photo->photo == nullptr) {
        return false;
      }
      const auto *photo_size = get_export_photo_size(photo->photo->sizes_);
      if (photo_size == nullptr) {
        return false;
      }
      size = ExportHtmlImageSize{photo_size->width_, photo_size->height_};
    } else if (block.kind == ExportRichBlock::Kind::Video) {
      const auto *document = find_document(block.document_id);
      if (document == nullptr || document->file.relative_path.empty() ||
          document->thumbnail_file.relative_path.empty()) {
        return false;
      }
      size = ExportHtmlImageSize{document->width, document->height};
    } else {
      return false;
    }
    return size.width > 0 && size.height > 0;
  }

  // returns an empty vector unless all the blocks are media, which can be laid out as an album
  vector<ExportHtmlImageSize> get_media_group_sizes(const vector<ExportRichBlock> &blocks) const {
    vector<ExportHtmlImageSize> result;
    result.reserve(blocks.size());
    for (const auto &block : blocks) {
      ExportHtmlImageSize size;
      if (!get_media_item_size(block, size) || rich_caption_has_output(block.caption) ||
          (block.has_optional_url && !block.optional_url.empty())) {
        return {};
      }
      result.push_back(size);
    }
    return result;
  }

  RichTailState get_tail_state(const vector<ExportRichBlock> &blocks) const {
    for (auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
      auto state = get_tail_state(*it);
      if (state != RichTailState::Empty) {
        return state;
      }
    }
    return RichTailState::Empty;
  }

  RichTailState get_tail_state(const ExportRichBlock &block) const {
    using Kind = ExportRichBlock::Kind;
    auto get_media_state = [](bool has_other) {
      return has_other ? RichTailState::Other : RichTailState::Media;
    };
    switch (block.kind) {
      case Kind::Unsupported:
      case Kind::Heading:
      case Kind::Paragraph:
      case Kind::Footer:
      case Kind::Thinking:
      case Kind::AuthorDate:
      case Kind::Code:
      case Kind::Divider:
      case Kind::Math:
      case Kind::Table:
      case Kind::ButtonRow:
        return RichTailState::Other;
      case Kind::Anchor:
        return RichTailState::Empty;
      case Kind::List: {
        if (block.list_items.empty()) {
          return RichTailState::Empty;
        }
        const auto &item = block.list_items.back();
        if (item.content == ExportRichContent::Blocks) {
          auto state = get_tail_state(item.blocks);
          if (state != RichTailState::Empty) {
            return state;
          }
        }
        return RichTailState::Other;
      }
      case Kind::Quote:
        if (rich_text_has_output(block.quote_caption)) {
          return RichTailState::Other;
        }
        if (block.quote_content == ExportRichContent::Blocks) {
          auto state = get_tail_state(block.blocks);
          if (state != RichTailState::Empty) {
            return state;
          }
        }
        return RichTailState::Other;
      case Kind::Photo:
        return get_media_state(rich_caption_has_output(block.caption) ||
                               (block.has_optional_url && !block.optional_url.empty()));
      case Kind::Video:
      case Kind::Audio:
      case Kind::File:
      case Kind::Map:
      case Kind::InputMap:
        return get_media_state(rich_caption_has_output(block.caption));
      case Kind::Cover:
        return get_tail_state(block.blocks);
      case Kind::Embed:
        return get_media_state(rich_caption_has_output(block.caption) ||
                               (block.has_poster_photo_id && block.has_optional_url && !block.optional_url.empty()));
      case Kind::EmbedPost: {
        if (rich_caption_has_output(block.caption)) {
          return RichTailState::Other;
        }
        auto state = get_tail_state(block.blocks);
        if (state != RichTailState::Empty) {
          return state;
        }
        return get_media_state(!block.url.empty());
      }
      case Kind::Collage:
      case Kind::Slideshow:
        return rich_caption_has_output(block.caption) ? RichTailState::Other : get_tail_state(block.blocks);
      case Kind::Channel:
        return RichTailState::Media;
      case Kind::Details:
        if (block.open) {
          auto state = get_tail_state(block.blocks);
          if (state != RichTailState::Empty) {
            return state;
          }
        }
        return RichTailState::Other;
      case Kind::RelatedArticles: {
        if (block.related_articles.empty()) {
          return rich_text_has_output(block.text) ? RichTailState::Other : RichTailState::Empty;
        }
        const auto &article = block.related_articles.back();
        if (!article.url.empty()) {
          return RichTailState::Other;
        }
        if (article.has_photo_id && article.has_description && !article.description.empty()) {
          const auto *photo = find_photo(article.photo_id);
          if (!get_rich_file_description(photo == nullptr ? nullptr : &photo->file).empty()) {
            return RichTailState::Other;
          }
        }
        return RichTailState::Media;
      }
      default:
        UNREACHABLE();
        return RichTailState::Other;
    }
  }

  void render_caption(string &result, const ExportRichCaption &caption) {
    string text;
    render_text(text, caption.text);
    string credit;
    render_text(credit, caption.credit);
    if (text.empty() && credit.empty()) {
      return;
    }
    context_.push_tag(result, "figcaption", {{"class", "rich_media_caption"}, {"dir", "auto"}});
    if (!text.empty()) {
      context_.push_div(result, "rich_caption_text");
      result += text;
      context_.pop_tag(result);
    }
    if (!credit.empty()) {
      context_.push_tag(result, "cite", {{"class", "rich_caption_credit"}});
      result += credit;
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
  }

  void render_source_meta(string &result, Slice source, RichSourceMetaKind kind) {
    if (source.empty()) {
      return;
    }
    context_.push_div(result,
                      kind == RichSourceMetaKind::Source ? Slice("rich_source_meta") : Slice("rich_embed_post_meta"));
    auto href = get_safe_http_link(source);
    if (!href.empty() && !is_link_active_) {
      context_.push_tag(result, "a", {{"class", "rich_source_link"}, {"href", href}, {"inline", ""}});
      LinkGuard guard(is_link_active_);
      append_html_escaped(result, source);
      context_.pop_tag(result);
    } else {
      context_.push_tag(result, "span", {{"class", "rich_source_text"}, {"inline", ""}});
      append_html_escaped(result, source);
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
  }

  void render_photo(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_media_item", "photo");
    attributes.emplace("data-photo-id", get_rich_id(block.photo_id));
    attributes.emplace("data-spoiler", get_rich_bool(block.spoiler).str());
    if (block.has_optional_webpage_id) {
      attributes.emplace("data-webpage-id", get_rich_id(block.optional_webpage_id));
    }
    context_.push_tag(result, "figure", attributes);
    message_.push_rich_photo_media(result, find_photo(block.photo_id));
    if (block.has_optional_url) {
      render_source_meta(result, block.optional_url, RichSourceMetaKind::Source);
    }
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_video(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_media_item", "video");
    attributes.emplace("data-document-id", get_rich_id(block.document_id));
    attributes.emplace("data-autoplay", get_rich_bool(block.autoplay).str());
    attributes.emplace("data-loop", get_rich_bool(block.loop).str());
    attributes.emplace("data-spoiler", get_rich_bool(block.spoiler).str());
    context_.push_tag(result, "figure", attributes);
    message_.push_rich_video_media(result, find_document(block.document_id));
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_audio(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_media_item", "audio");
    attributes.emplace("data-document-id", get_rich_id(block.document_id));
    context_.push_tag(result, "figure", attributes);
    message_.push_rich_audio_media(result, find_document(block.document_id));
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_file(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_media_item", "file");
    attributes.emplace("data-document-id", get_rich_id(block.document_id));
    context_.push_tag(result, "figure", attributes);
    message_.push_rich_file_media(result, find_document(block.document_id));
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_media_group(string &result, const ExportRichBlock &block, Slice kind, bool render_group_caption) {
    auto attributes = get_rich_block_attributes("rich_media_group", kind);
    attributes.emplace("data-rich-has-items", get_rich_bool(!block.blocks.empty()).str());
    attributes.emplace("data-rich-items-end-media",
                       get_rich_bool(get_tail_state(block.blocks) == RichTailState::Media).str());
    context_.push_tag(result, "section", attributes);
    context_.push_div(result, "rich_media_group_items");
    render_blocks(result, block.blocks);
    context_.pop_tag(result);
    if (render_group_caption) {
      render_caption(result, block.caption);
    }
    context_.pop_tag(result);
  }

  void render_collage(string &result, const ExportRichBlock &block) {
    auto sizes = get_media_group_sizes(block.blocks);
    if (sizes.empty()) {
      return render_media_group(result, block, "collage", true);
    }
    auto layout = MediaGroupLayouter(sizes, MEDIA_GROUP_MAX_WIDTH, MEDIA_GROUP_MIN_WIDTH, MEDIA_GROUP_SPACING).layout();
    MediaGroupBounds full;
    for (const auto &part : layout) {
      full = unite_media_group_bounds(full, get_media_group_bounds(part));
    }
    if (layout.size() != sizes.size() || full.is_empty()) {
      return render_media_group(result, block, "collage", true);
    }
    auto full_width = full.get_width();
    auto full_height = full.get_height();
    auto attributes = get_rich_block_attributes("rich_media_group", "collage");
    attributes.emplace("data-rich-has-items", "true");
    attributes.emplace("data-rich-items-end-media", "true");
    context_.push_tag(result, "section", attributes);
    context_.push_div(result, "rich_collage_box", get_rich_aspect_ratio_style(full_width, full_height));
    for (size_t i = 0; i < block.blocks.size(); i++) {
      const auto &geometry = layout[i];
      context_.push_div(result, "rich_collage_item",
                        PSLICE() << "left: " << get_rich_percent_value(geometry.x - full.left, full_width)
                                 << "; top: " << get_rich_percent_value(geometry.y - full.top, full_height)
                                 << "; width: " << get_rich_percent_value(geometry.width, full_width)
                                 << "; height: " << get_rich_percent_value(geometry.height, full_height));
      render_block(result, block.blocks[i]);
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_slideshow(string &result, const ExportRichBlock &block) {
    auto sizes = get_media_group_sizes(block.blocks);
    if (sizes.empty()) {
      return render_media_group(result, block, "slideshow", true);
    }
    auto widest = sizes[0];
    for (const auto &size : sizes) {
      if (static_cast<int64>(size.width) * widest.height > static_cast<int64>(widest.width) * size.height) {
        widest = size;
      }
    }
    auto has_controls = block.blocks.size() > 1;
    auto attributes = get_rich_block_attributes("rich_media_group", "slideshow");
    attributes.emplace("data-rich-has-items", "true");
    attributes.emplace("data-rich-items-end-media", "true");
    context_.push_tag(result, "section", attributes);
    context_.push_div(result, "rich_slideshow_box", get_rich_aspect_ratio_style(widest.width, widest.height));
    context_.push_div(result, "rich_slideshow_track");
    for (const auto &item : block.blocks) {
      context_.push_div(result, "rich_slide");
      render_block(result, item);
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
    if (has_controls) {
      context_.push_tag(result, "button", {{"class", "rich_slideshow_prev"}, {"type", "button"}, {"inline", ""}});
      result += "&#8592;";
      context_.pop_tag(result);
      context_.push_tag(result, "button", {{"class", "rich_slideshow_next"}, {"type", "button"}, {"inline", ""}});
      result += "&#8594;";
      context_.pop_tag(result);
      context_.push_div(result, "rich_slideshow_dots");
      for (size_t i = 0; i < block.blocks.size(); i++) {
        context_.push_tag(result, "button", {{"class", "rich_slideshow_dot"}, {"type", "button"}, {"inline", ""}});
        context_.pop_tag(result);
      }
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_embed(string &result, const ExportRichBlock &block) {
    auto has_poster = block.has_poster_photo_id;
    auto has_meta = has_poster && block.has_optional_url && !block.optional_url.empty();
    auto attributes = get_rich_block_attributes("rich_media_item rich_embed", "embed");
    attributes.emplace("data-full-width", get_rich_bool(block.full_width).str());
    attributes.emplace("data-allow-scrolling", get_rich_bool(block.allow_scrolling).str());
    attributes.emplace("data-rich-has-meta", get_rich_bool(has_meta).str());
    if (block.has_poster_photo_id) {
      attributes.emplace("data-poster-photo-id", get_rich_id(block.poster_photo_id));
    }
    if (block.has_size) {
      attributes.emplace("data-source-width", to_string(block.width));
      attributes.emplace("data-source-height", to_string(block.height));
    }
    context_.push_tag(result, "figure", attributes);
    if (has_poster) {
      message_.push_rich_photo_media(result, find_photo(block.poster_photo_id));
      if (block.has_optional_url) {
        render_source_meta(result, block.optional_url, RichSourceMetaKind::Source);
      }
    } else {
      MediaData media;
      media.title = "Embed";
      media.classes = "media_file";
      if (block.has_optional_url) {
        media.status = block.optional_url;
        media.link = get_safe_http_link(block.optional_url);
      }
      message_.push_generic_media(result, media);
    }
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_embed_post(string &result, const ExportRichBlock &block) {
    const auto *photo = find_photo(block.author_photo_id);
    auto attributes = get_rich_block_attributes("rich_embed_post", "embed-post");
    attributes.emplace("data-webpage-id", get_rich_id(block.webpage_id));
    attributes.emplace("data-author-photo-id", get_rich_id(block.author_photo_id));
    attributes.emplace("data-date", to_string(block.date));
    attributes.emplace("data-rich-has-meta", get_rich_bool(!block.url.empty()).str());
    attributes.emplace("data-rich-has-items", get_rich_bool(!block.blocks.empty()).str());
    attributes.emplace("data-rich-items-end-media",
                       get_rich_bool(get_tail_state(block.blocks) == RichTailState::Media).str());
    context_.push_tag(result, "section", attributes);
    context_.push_div(result, "rich_embed_post_author");
    MediaData media;
    media.title = block.author.empty() ? string("Embed post") : block.author;
    media.description = get_rich_file_description(photo == nullptr ? nullptr : &photo->file).str();
    media.status = format_date_time(block.date);
    media.classes = "media_contact";
    message_.push_rich_reference_media(result, std::move(media), photo);
    context_.pop_tag(result);
    render_source_meta(result, block.url, RichSourceMetaKind::EmbedPost);
    context_.push_div(result, "rich_embed_post_blocks");
    render_blocks(result, block.blocks);
    context_.pop_tag(result);
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_channel(string &result, const ExportRichBlock &block) {
    const auto &channel = block.channel;
    auto attributes = get_rich_block_attributes("rich_reference_block", "channel");
    attributes.emplace("data-channel-source", get_rich_channel_source_name(channel.source).str());
    attributes.emplace("data-channel-id", get_rich_id(channel.id));
    attributes.emplace("data-broadcast", get_rich_bool(channel.broadcast).str());
    attributes.emplace("data-megagroup", get_rich_bool(channel.megagroup).str());
    attributes.emplace("data-monoforum", get_rich_bool(channel.monoforum).str());
    if (channel.has_access_hash) {
      attributes.emplace("data-access-hash", to_string(channel.access_hash));
    }
    context_.push_tag(result, "section", attributes);
    MediaData media;
    media.title = channel.has_title && !channel.title.empty() ? channel.title : string("Channel");
    media.status = get_rich_channel_status(channel);
    media.classes = "media_contact";
    if (channel.has_username && is_ascii_word(channel.username, RICH_USERNAME_MIN_LENGTH, RICH_USERNAME_MAX_LENGTH)) {
      media.link = get_safe_http_link(PSLICE() << LinkManager::get_t_me_url() << channel.username);
    }
    message_.push_generic_media(result, media);
    context_.pop_tag(result);
  }

  void render_map(string &result, const ExportRichBlock &block, Slice kind) {
    const auto &point = block.map_point;
    auto attributes = get_rich_block_attributes("rich_reference_block", kind);
    attributes.emplace("data-point-source", get_rich_map_point_source_name(point.source).str());
    attributes.emplace("data-zoom", to_string(block.zoom));
    attributes.emplace("data-source-width", to_string(block.map_width));
    attributes.emplace("data-source-height", to_string(block.map_height));
    if (point.has_access_hash) {
      attributes.emplace("data-access-hash", to_string(point.access_hash));
    }
    if (point.has_accuracy_radius) {
      attributes.emplace("data-accuracy-radius", to_string(point.accuracy_radius));
    }
    context_.push_tag(result, "figure", attributes);
    MediaData media;
    media.title = "Location";
    media.classes = "media_location";
    if (has_rich_map_coordinates(point)) {
      auto latitude = format_coordinate(point.latitude);
      auto longitude = format_coordinate(point.longitude);
      media.status = PSTRING() << latitude << ", " << longitude;
      auto coordinates = PSTRING() << latitude << ',' << longitude;
      media.link = get_safe_http_link(PSLICE() << "https://maps.google.com/maps?q=" << coordinates
                                               << "&ll=" << coordinates << "&z=16");
    }
    message_.push_generic_media(result, media);
    render_caption(result, block.caption);
    context_.pop_tag(result);
  }

  void render_related_article(string &result, const ExportRichRelatedArticle &article, int32 index) {
    HtmlAttributes attributes;
    attributes.emplace("class", "rich_related_article");
    attributes.emplace("data-rich-article-index", to_string(index));
    attributes.emplace("data-webpage-id", get_rich_id(article.webpage_id));
    if (article.has_photo_id) {
      attributes.emplace("data-photo-id", get_rich_id(article.photo_id));
    }
    if (article.has_published_date) {
      attributes.emplace("data-published-date", to_string(article.published_date));
    }
    context_.push_tag(result, "article", attributes);
    MediaData media;
    media.title = article.has_title && !article.title.empty() ? article.title : string("Related article");
    if (article.has_author) {
      media.status = article.author;
    }
    auto published_date = article.has_published_date ? format_date_time(article.published_date) : string();
    if (!published_date.empty()) {
      if (!media.status.empty()) {
        media.status += ", ";
      }
      media.status += published_date;
    }
    auto description = article.has_description ? article.description : string();
    bool is_description_displaced = false;
    if (article.has_photo_id) {
      const auto *photo = find_photo(article.photo_id);
      auto file_description = get_rich_file_description(photo == nullptr ? nullptr : &photo->file);
      media.classes = "media_photo";
      media.description = file_description.empty() ? description : file_description.str();
      is_description_displaced = !file_description.empty() && !description.empty();
      message_.push_rich_reference_media(result, std::move(media), photo);
    } else {
      media.classes = "media_file";
      media.description = description;
      message_.push_generic_media(result, media);
    }
    if (is_description_displaced) {
      context_.push_div(result, "rich_article_description");
      append_html_escaped(result, description);
      context_.pop_tag(result);
    }
    render_source_meta(result, article.url, RichSourceMetaKind::Source);
    context_.pop_tag(result);
  }

  void render_related_articles(string &result, const ExportRichBlock &block) {
    string title;
    render_text(title, block.text);
    context_.push_tag(result, "section", get_rich_block_attributes("rich_related_articles", "related-articles"));
    if (!title.empty()) {
      context_.push_tag(result, "div", {{"class", "rich_related_title"}, {"dir", "auto"}});
      result += title;
      context_.pop_tag(result);
    }
    context_.push_div(result, "rich_related_items");
    int32 index = 0;
    for (const auto &article : block.related_articles) {
      render_related_article(result, article, index++);
    }
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  void collect_anchors() {
    first_anchor_ids_.clear();
    anchor_occurrences_.clear();
    text_anchor_ids_.clear();
    block_anchor_ids_.clear();
    collect_block_anchors(rich_message_.blocks);
  }

  string register_anchor(const string &name) {
    string base = PSTRING() << "rich-message-" << message_.get_message_id() << "-anchor-"
                            << get_rich_anchor_token(name);
    auto occurrence = ++anchor_occurrences_[base];
    auto result = occurrence > 1 ? PSTRING() << base << '-' << occurrence : base;
    first_anchor_ids_.emplace(name, result);
    return result;
  }

  void collect_text_anchors(const ExportRichText &text) {
    using Type = ExportRichText::Type;
    switch (text.type) {
      case Type::Anchor:
        text_anchor_ids_.emplace(&text, register_anchor(text.data));
        collect_text_anchors(text.children);
        break;
      case Type::Diff:
        collect_text_anchors(text.children);
        collect_text_anchors(text.old_children);
        break;
      case Type::Empty:
      case Type::Plain:
      case Type::Math:
      case Type::CustomEmoji:
      case Type::InlineImage:
        break;
      default:
        collect_text_anchors(text.children);
        break;
    }
  }

  void collect_text_anchors(const vector<ExportRichText> &texts) {
    for (const auto &text : texts) {
      collect_text_anchors(text);
    }
  }

  void collect_caption_anchors(const ExportRichCaption &caption) {
    collect_text_anchors(caption.text);
    collect_text_anchors(caption.credit);
  }

  void collect_block_anchors(const vector<ExportRichBlock> &blocks) {
    for (const auto &block : blocks) {
      collect_block_anchors(block);
    }
  }

  void collect_block_anchors(const ExportRichBlock &block) {
    using Kind = ExportRichBlock::Kind;
    switch (block.kind) {
      case Kind::Heading:
      case Kind::Paragraph:
      case Kind::Footer:
      case Kind::Thinking:
      case Kind::AuthorDate:
      case Kind::Code:
        collect_text_anchors(block.text);
        break;
      case Kind::Anchor:
        block_anchor_ids_.emplace(&block, register_anchor(block.name));
        break;
      case Kind::List:
        for (const auto &item : block.list_items) {
          if (item.content == ExportRichContent::Text) {
            collect_text_anchors(item.text);
          } else {
            collect_block_anchors(item.blocks);
          }
        }
        break;
      case Kind::Quote:
        if (block.quote_content == ExportRichContent::Text) {
          collect_text_anchors(block.text);
        } else {
          collect_block_anchors(block.blocks);
        }
        collect_text_anchors(block.quote_caption);
        break;
      case Kind::Photo:
      case Kind::Video:
      case Kind::Audio:
      case Kind::File:
      case Kind::Embed:
      case Kind::Map:
      case Kind::InputMap:
        collect_caption_anchors(block.caption);
        break;
      case Kind::EmbedPost:
      case Kind::Collage:
      case Kind::Slideshow:
        collect_block_anchors(block.blocks);
        collect_caption_anchors(block.caption);
        break;
      case Kind::Cover:
        collect_block_anchors(block.blocks);
        break;
      case Kind::Table:
        collect_text_anchors(block.text);
        for (const auto &row : block.table_rows) {
          for (const auto &cell : row.cells) {
            if (cell.has_text) {
              collect_text_anchors(cell.text);
            }
          }
        }
        break;
      case Kind::Details:
        collect_text_anchors(block.text);
        collect_block_anchors(block.blocks);
        break;
      case Kind::RelatedArticles:
        collect_text_anchors(block.text);
        break;
      case Kind::ButtonRow:
        collect_text_anchors(block.buttons);
        break;
      case Kind::Unsupported:
      case Kind::Divider:
      case Kind::Channel:
      case Kind::Math:
        break;
      default:
        UNREACHABLE();
    }
  }

  // returns an empty string if there is no such anchor
  string get_fragment_link(Slice target) const {
    if (!is_strict_link_target(target) || target[0] != '#') {
      return string();
    }
    auto it = first_anchor_ids_.find(target.substr(1).str());
    return it == first_anchor_ids_.end() ? string() : PSTRING() << '#' << it->second;
  }

  template <class T>
  static string get_anchor_id(const std::map<const T *, string> &anchor_ids, const T &object) {
    auto it = anchor_ids.find(&object);
    return it == anchor_ids.end() ? string() : it->second;
  }

  void render_texts(string &result, const vector<ExportRichText> &texts) {
    for (const auto &text : texts) {
      render_text(result, text);
    }
  }

  void render_unsupported_text(string &result) {
    context_.push_tag(result, "span",
                      {{"class", "rich_inline_fallback"}, {"data-rich-kind", "unsupported-text"}, {"inline", ""}});
    result += "Unsupported rich text";
    context_.pop_tag(result);
  }

  void render_text_children(string &result, const ExportRichText &text) {
    if (!text.children.empty()) {
      return render_texts(result, text.children);
    }
    render_unsupported_text(result);
  }

  void render_text_link(string &result, const ExportRichText &text, bool has_href, Slice href,
                        HtmlAttributes attributes) {
    attributes.emplace("inline", string());
    if (!has_href || is_link_active_) {
      attributes.erase("href");
      attributes.erase("onclick");
      attributes.emplace("class", "rich_inert_link");
      context_.push_tag(result, "span", attributes);
      render_text_children(result, text);
      context_.pop_tag(result);
      return;
    }
    attributes.emplace("href", href.str());
    context_.push_tag(result, "a", attributes);
    LinkGuard guard(is_link_active_);
    render_text_children(result, text);
    context_.pop_tag(result);
  }

  void render_text_link(string &result, const ExportRichText &text, const string &href) {
    render_text_link(result, text, !href.empty(), href, HtmlAttributes());
  }

  void render_button(string &result, const ExportRichText &button, bool is_inline) {
    using Type = ExportInlineButtonAction::Type;
    if (button.button == nullptr) {
      return render_text_children(result, button);
    }
    const auto &action = button.button->action;
    auto attributes = get_rich_button_attributes(action, button.button->has_style, button.button->style, is_inline);
    Slice tag("span");
    auto url = action.type == Type::Url ? get_safe_message_link(action.url) : string();
    if (!is_link_active_ && !url.empty()) {
      tag = Slice("a");
      attributes.emplace("href", url);
    } else if (!is_link_active_ && action.type == Type::CopyText) {
      tag = Slice("button");
      attributes.emplace("type", "button");
      attributes.emplace("onclick", "return ShowTextCopied(decodeURIComponent(this.dataset.copyText))");
    }
    context_.push_tag(result, tag, attributes);
    LinkGuard guard(is_link_active_);
    render_texts(result, button.children);
    context_.pop_tag(result);
  }

  void render_button_row(string &result, const ExportRichBlock &block) {
    auto alignment = get_export_rich_button_alignment_string(block.button_alignment);
    auto attributes =
        get_rich_block_attributes(PSLICE() << "rich_button_row rich_button_align_" << alignment, "button-row");
    attributes.emplace("data-button-alignment", alignment.str());
    context_.push_tag(result, "div", attributes);
    context_.push_div(result, "rich_button_group");
    for (const auto &button : block.buttons) {
      render_button(result, button, false);
    }
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  void render_custom_emoji_link(string &result, Slice alt, HtmlAttributes attributes) {
    attributes.emplace("inline", string());
    if (is_link_active_) {
      attributes.erase("href");
      attributes.erase("onclick");
      attributes.emplace("class", "rich_inert_link");
      context_.push_tag(result, "span", attributes);
      result.append(alt.data(), alt.size());
      context_.pop_tag(result);
      return;
    }
    context_.push_tag(result, "a", attributes);
    LinkGuard guard(is_link_active_);
    result.append(alt.data(), alt.size());
    context_.pop_tag(result);
  }

  void render_custom_emoji(string &result, const ExportRichText &text) {
    context_.push_tag(result, "span",
                      {{"class", "rich_custom_emoji"}, {"data-document-id", get_rich_id(text.id)}, {"inline", ""}});
    auto alt = html_escape(text.text);
    auto path = get_export_custom_emoji_path(&message_.custom_emoji_files_, text.id);
    auto is_unavailable = path == UNAVAILABLE_CUSTOM_EMOJI;
    auto is_not_loaded = path.empty() || is_ascii_decimal(path);
    auto href = is_unavailable || is_not_loaded ? string() : get_safe_relative_emoji_link(path, message_.base_);
    if (!href.empty()) {
      HtmlAttributes attributes;
      attributes.emplace("href", std::move(href));
      render_custom_emoji_link(result, alt, std::move(attributes));
    } else if (is_unavailable || is_not_loaded) {
      HtmlAttributes attributes;
      attributes.emplace("href", string());
      attributes.emplace("onclick", is_unavailable ? "return ShowNotAvailableEmoji()" : "return ShowNotLoadedEmoji()");
      render_custom_emoji_link(result, alt, std::move(attributes));
    } else {
      result += alt;
    }
    context_.pop_tag(result);
  }

  void render_wrapped_text(string &result, const ExportRichText &text, Slice tag, Slice class_name = Slice()) {
    if (class_name.empty()) {
      context_.push_tag(result, tag, {{"inline", ""}});
    } else {
      context_.push_tag(result, tag, {{"class", class_name}, {"inline", ""}});
    }
    render_text_children(result, text);
    context_.pop_tag(result);
  }

  void render_text(string &result, const ExportRichText &text) {
    using Type = ExportRichText::Type;
    switch (text.type) {
      case Type::Empty:
        return;
      case Type::Plain:
        return append_html_escaped(result, text.text);
      case Type::Concat:
        return render_texts(result, text.children);
      case Type::Bold:
        return render_wrapped_text(result, text, "strong");
      case Type::Italic:
        return render_wrapped_text(result, text, "em");
      case Type::Underline:
        return render_wrapped_text(result, text, "u");
      case Type::Strike:
        return render_wrapped_text(result, text, "s");
      case Type::Fixed:
        return render_wrapped_text(result, text, "code", "rich_inline_code");
      case Type::Url: {
        auto href = text.data[0] == '#' ? get_fragment_link(text.data) : get_safe_http_link(text.data);
        HtmlAttributes attributes;
        if (text.id != 0) {
          attributes.emplace("data-webpage-id", get_rich_id(text.id));
        }
        return render_text_link(result, text, !href.empty(), href, std::move(attributes));
      }
      case Type::Email:
        return render_text_link(result, text, get_safe_email_link(text.data));
      case Type::Phone:
        return render_text_link(result, text, get_safe_phone_link(text.data));
      case Type::Subscript:
        return render_wrapped_text(result, text, "sub");
      case Type::Superscript:
        return render_wrapped_text(result, text, "sup");
      case Type::Marked:
        return render_wrapped_text(result, text, "mark");
      case Type::Anchor: {
        auto id = get_anchor_id(text_anchor_ids_, text);
        if (id.empty()) {
          context_.push_tag(result, "span", {{"class", "rich_reference"}, {"inline", ""}});
        } else {
          context_.push_tag(result, "span", {{"class", "rich_reference"}, {"id", id}, {"inline", ""}});
        }
        render_texts(result, text.children);
        context_.pop_tag(result);
        return;
      }
      case Type::Math:
        context_.push_tag(
            result, "span",
            {{"class", "rich_math_inline"}, {"data-rich-kind", "inline-math"}, {"dir", "ltr"}, {"inline", ""}});
        append_html_escaped(result, text.data);
        context_.pop_tag(result);
        return;
      case Type::CustomEmoji:
        return render_custom_emoji(result, text);
      case Type::Spoiler:
        context_.push_tag(result, "span",
                          {{"class", "spoiler hidden"}, {"inline", ""}, {"onclick", "ShowSpoiler(this)"}});
        context_.push_tag(result, "span", {{"aria-hidden", "true"}, {"inline", ""}});
        render_text_children(result, text);
        context_.pop_tag(result);
        context_.pop_tag(result);
        return;
      case Type::Mention: {
        string target;
        auto href = append_rich_plain_target(target, text) ? get_safe_mention_link(target) : string();
        return render_text_link(result, text, href);
      }
      case Type::Hashtag:
      case Type::BotCommand:
      case Type::Cashtag: {
        auto prefix = text.type == Type::Hashtag ? '#' : (text.type == Type::BotCommand ? '/' : '$');
        string target;
        auto action = append_rich_plain_target(target, text) ? get_safe_rich_action_data(target, prefix) : string();
        if (action.empty()) {
          return render_text_link(result, text, false, Slice(), HtmlAttributes());
        }
        HtmlAttributes attributes;
        attributes.emplace(text.type == Type::BotCommand ? "data-command" : "data-tag", std::move(action));
        attributes.emplace("onclick", text.type == Type::Hashtag ? "return ShowHashtag(this.dataset.tag)"
                                                                 : (text.type == Type::BotCommand
                                                                        ? "return ShowBotCommand(this.dataset.command)"
                                                                        : "return ShowCashtag(this.dataset.tag)"));
        return render_text_link(result, text, true, Slice(), std::move(attributes));
      }
      case Type::AutoUrl: {
        string target;
        auto href = append_rich_plain_target(target, text) ? get_safe_http_link(target) : string();
        return render_text_link(result, text, href);
      }
      case Type::AutoEmail: {
        string target;
        auto href = append_rich_plain_target(target, text) ? get_safe_email_link(target) : string();
        return render_text_link(result, text, href);
      }
      case Type::AutoPhone: {
        string target;
        auto href = append_rich_plain_target(target, text) ? get_safe_phone_link(target) : string();
        return render_text_link(result, text, href);
      }
      case Type::BankCard:
        return render_wrapped_text(result, text, "span", "rich_bank_card");
      case Type::TonAddress:
        return render_wrapped_text(result, text, "span", "rich_ton_address");
      case Type::MentionName: {
        HtmlAttributes attributes;
        attributes.emplace("data-user-id", get_rich_id(text.id));
        attributes.emplace("onclick", "return ShowMentionName()");
        return render_text_link(result, text, true, Slice(), std::move(attributes));
      }
      case Type::FormattedDate: {
        HtmlAttributes attributes;
        attributes.emplace("class", "rich_formatted_date");
        attributes.emplace("data-date", to_string(text.date));
        attributes.emplace("data-day-of-week", get_rich_bool(text.day_of_week).str());
        attributes.emplace("data-long-date", get_rich_bool(text.long_date).str());
        attributes.emplace("data-long-time", get_rich_bool(text.long_time).str());
        attributes.emplace("data-relative", get_rich_bool(text.relative).str());
        attributes.emplace("data-short-date", get_rich_bool(text.short_date).str());
        attributes.emplace("data-short-time", get_rich_bool(text.short_time).str());
        attributes.emplace("datetime", get_iso_date_time(text.date));
        attributes.emplace("dir", "ltr");
        attributes.emplace("inline", string());
        attributes.emplace("title", format_date_time(text.date));
        context_.push_tag(result, "time", attributes);
        render_text_children(result, text);
        context_.pop_tag(result);
        return;
      }
      case Type::InlineImage:
        context_.push_tag(result, "span",
                          {{"class", "rich_inline_fallback"},
                           {"data-document-id", get_rich_id(text.id)},
                           {"data-rich-kind", "inline-image"},
                           {"data-source-height", PSLICE() << text.height},
                           {"data-source-width", PSLICE() << text.width},
                           {"inline", ""}});
        result += "Inline image";
        context_.pop_tag(result);
        return;
      case Type::Diff:
        context_.push_tag(result, "span", {{"class", "rich_diff"}, {"data-rich-kind", "diff"}, {"inline", ""}});
        render_text_children(result, text);
        context_.push_tag(result, "del", {{"class", "rich_diff_previous"}, {"inline", ""}});
        result += "Previous: ";
        if (text.old_children.empty()) {
          render_unsupported_text(result);
        } else {
          render_texts(result, text.old_children);
        }
        context_.pop_tag(result);
        context_.pop_tag(result);
        return;
      case Type::Button:
        return render_button(result, text, true);
      default:
        UNREACHABLE();
    }
  }

  void render_blocks(string &result, const vector<ExportRichBlock> &blocks) {
    for (const auto &block : blocks) {
      render_block(result, block);
    }
  }

  void render_text_block(string &result, Slice tag, const HtmlAttributes &attributes, const ExportRichText &text) {
    context_.push_tag(result, tag, attributes);
    render_text(result, text);
    context_.pop_tag(result);
  }

  void render_author_date(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_author_date", "author-date");
    attributes.emplace("dir", "auto");
    context_.push_tag(result, "address", attributes);
    render_text(result, block.text);
    if (block.date != 0) {
      result += " \xE2\x80\xA2 ";
      context_.push_tag(result, "time",
                        {{"data-date", PSLICE() << block.date},
                         {"datetime", get_iso_date_time(block.date)},
                         {"dir", "ltr"},
                         {"inline", ""}});
      append_html_escaped(result, format_date_time(block.date));
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
  }

  void render_code(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_code", "code");
    attributes.emplace("dir", "auto");
    attributes.emplace("inline", string());
    if (!block.language.empty()) {
      attributes.emplace("data-language", block.language);
    }
    context_.push_tag(result, "pre", attributes);
    context_.push_tag(result, "code", {{"class", "rich_code_content"}, {"inline", ""}});
    render_text(result, block.text);
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  void render_list(string &result, const ExportRichBlock &block) {
    auto is_ordered = block.list_kind == ExportRichListKind::Ordered;
    auto attributes = get_rich_block_attributes("rich_list", is_ordered ? Slice("ordered-list") : Slice("list"));
    if (is_ordered) {
      if (block.reversed) {
        attributes.emplace("reversed", string());
      }
      if (block.has_start) {
        attributes.emplace("start", to_string(block.start));
      }
      if (block.has_list_type) {
        const auto &type = block.list_type;
        auto is_native = type == "1" || type == "a" || type == "A" || type == "i" || type == "I";
        attributes.emplace(is_native ? "type" : "data-list-type", type);
      }
    }
    context_.push_tag(result, is_ordered ? Slice("ol") : Slice("ul"), attributes);
    for (const auto &item : block.list_items) {
      render_list_item(result, item, is_ordered);
    }
    context_.pop_tag(result);
  }

  void render_list_item(string &result, const ExportRichListItem &item, bool is_ordered) {
    auto is_task = item.task_state != ExportRichTaskState::None;
    auto is_checked = item.task_state == ExportRichTaskState::Checked;
    auto has_custom_marker = is_ordered && !is_task && item.has_num && !item.num.empty();
    auto has_blocks = item.content == ExportRichContent::Blocks;
    string classes = "rich_list_item";
    if (is_task) {
      classes += " rich_task_item";
    } else if (has_custom_marker) {
      classes += " rich_order_item";
    }
    HtmlAttributes attributes;
    attributes.emplace("class", std::move(classes));
    attributes.emplace("data-rich-content", has_blocks ? "blocks" : "text");
    if (item.has_num) {
      attributes.emplace("data-num", item.num);
    }
    if (item.has_type) {
      attributes.emplace("data-item-type", item.type);
    }
    if (item.has_value) {
      attributes.emplace("value", to_string(item.value));
    }
    context_.push_tag(result, "li", attributes);
    if (is_task) {
      context_.push_tag(result, "span",
                        {{"aria-checked", get_rich_bool(is_checked)},
                         {"class", "rich_task_marker"},
                         {"inline", ""},
                         {"role", "checkbox"}});
      result += is_checked ? "\xE2\x98\x91" : "\xE2\x98\x90";
      context_.pop_tag(result);
    } else if (has_custom_marker) {
      context_.push_tag(result, "span", {{"class", "rich_order_marker"}, {"inline", ""}});
      append_html_escaped(result, item.num);
      context_.pop_tag(result);
    }
    context_.push_tag(result, has_blocks ? Slice("div") : Slice("span"),
                      {{"class", "rich_list_content"}, {"inline", ""}});
    if (has_blocks) {
      render_blocks(result, item.blocks);
    } else {
      render_text(result, item.text);
    }
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  void render_quote(string &result, const ExportRichBlock &block) {
    auto has_blocks = block.quote_content == ExportRichContent::Blocks;
    auto kind = block.pullquote ? Slice("pullquote") : (has_blocks ? Slice("quote-blocks") : Slice("quote"));
    auto classes = block.pullquote ? Slice("rich_quote rich_pullquote") : Slice("rich_quote");
    context_.push_tag(result, block.pullquote ? Slice("aside") : Slice("blockquote"),
                      get_rich_block_attributes(classes, kind));
    context_.push_div(result, "rich_quote_body");
    if (has_blocks) {
      render_blocks(result, block.blocks);
    } else {
      render_text(result, block.text);
    }
    context_.pop_tag(result);
    string caption;
    render_text(caption, block.quote_caption);
    if (!caption.empty()) {
      context_.push_tag(result, "cite", {{"class", "rich_quote_caption"}});
      result += caption;
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
  }

  void render_details(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_details", "details");
    if (block.open) {
      attributes.emplace("open", string());
    }
    context_.push_tag(result, "details", attributes);
    context_.push_tag(result, "summary");
    render_text(result, block.text);
    context_.pop_tag(result);
    context_.push_div(result, "rich_details_body");
    render_blocks(result, block.blocks);
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  void render_table(string &result, const ExportRichBlock &block) {
    auto attributes = get_rich_block_attributes("rich_table_wrap", "table");
    attributes.emplace("tabindex", "0");
    context_.push_tag(result, "div", attributes);
    string classes = "rich_table";
    if (block.bordered) {
      classes += " bordered";
    }
    if (block.striped) {
      classes += " striped";
    }
    if (block.compact) {
      classes += " compact";
    }
    context_.push_tag(result, "table", {{"class", classes}});
    string caption;
    render_text(caption, block.text);
    if (!caption.empty()) {
      context_.push_tag(result, "caption");
      result += caption;
      context_.pop_tag(result);
    }
    context_.push_tag(result, "tbody");
    for (const auto &row : block.table_rows) {
      context_.push_tag(result, "tr");
      for (const auto &cell : row.cells) {
        render_table_cell(result, cell);
      }
      context_.pop_tag(result);
    }
    context_.pop_tag(result);
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

  static void add_table_span(HtmlAttributes &attributes, Slice name, Slice source_name, bool has_span, int32 span) {
    if (!has_span || span <= 0) {
      return;
    }
    auto clamped_span = min(span, RICH_TABLE_SPAN_MAX);
    attributes.emplace(name.str(), to_string(clamped_span));
    if (clamped_span != span) {
      attributes.emplace(source_name.str(), to_string(span));
    }
  }

  void render_table_cell(string &result, const ExportRichTableCell &cell) {
    Slice horizontal_class("rich_align_left");
    switch (cell.alignment) {
      case ExportRichTableAlignment::Center:
        horizontal_class = Slice("rich_align_center");
        break;
      case ExportRichTableAlignment::Right:
        horizontal_class = Slice("rich_align_right");
        break;
      default:
        break;
    }
    Slice vertical_class("rich_valign_top");
    switch (cell.vertical_alignment) {
      case ExportRichTableVerticalAlignment::Middle:
        vertical_class = Slice("rich_valign_middle");
        break;
      case ExportRichTableVerticalAlignment::Bottom:
        vertical_class = Slice("rich_valign_bottom");
        break;
      default:
        break;
    }
    HtmlAttributes attributes;
    attributes.emplace("class", PSTRING() << horizontal_class << ' ' << vertical_class);
    add_table_span(attributes, "colspan", "data-source-colspan", cell.has_colspan, cell.colspan);
    add_table_span(attributes, "rowspan", "data-source-rowspan", cell.has_rowspan, cell.rowspan);
    context_.push_tag(result, cell.header ? Slice("th") : Slice("td"), attributes);
    if (cell.has_text) {
      render_text(result, cell.text);
    }
    context_.pop_tag(result);
  }

  void render_fallback(string &result, Slice kind, Slice label) {
    context_.push_tag(result, "div", get_rich_block_attributes("rich_fallback", kind));
    append_html_escaped(result, label);
    context_.pop_tag(result);
  }

  void render_block(string &result, const ExportRichBlock &block) {
    using Kind = ExportRichBlock::Kind;
    switch (block.kind) {
      case Kind::Unsupported:
        return render_fallback(result, "unsupported", "Unsupported rich content");
      case Kind::Heading: {
        auto attributes = get_rich_block_attributes("rich_heading", "heading");
        attributes.emplace("data-level", to_string(block.heading_level));
        return render_text_block(result, get_rich_heading_tag(block.heading_level), attributes, block.text);
      }
      case Kind::Paragraph: {
        auto attributes = get_rich_block_attributes("rich_paragraph", "paragraph");
        attributes.emplace("dir", "auto");
        return render_text_block(result, "p", attributes, block.text);
      }
      case Kind::Footer: {
        auto attributes = get_rich_block_attributes("rich_footer", "footer");
        attributes.emplace("dir", "auto");
        return render_text_block(result, "footer", attributes, block.text);
      }
      case Kind::Thinking: {
        auto attributes = get_rich_block_attributes("rich_thinking", "thinking");
        attributes.emplace("dir", "auto");
        return render_text_block(result, "p", attributes, block.text);
      }
      case Kind::AuthorDate:
        return render_author_date(result, block);
      case Kind::Code:
        return render_code(result, block);
      case Kind::Divider: {
        auto attributes = get_rich_block_attributes("rich_divider", "divider");
        attributes.emplace("empty", string());
        return context_.push_tag(result, "hr", attributes);
      }
      case Kind::Anchor: {
        auto id = get_anchor_id(block_anchor_ids_, block);
        if (id.empty()) {
          return render_fallback(result, "malformed", "Malformed rich content");
        }
        auto attributes = get_rich_block_attributes("rich_anchor", "anchor");
        attributes.emplace("id", std::move(id));
        attributes.emplace("inline", string());
        context_.push_tag(result, "span", attributes);
        context_.pop_tag(result);
        return;
      }
      case Kind::List:
        return render_list(result, block);
      case Kind::Quote:
        return render_quote(result, block);
      case Kind::Photo:
        return render_photo(result, block);
      case Kind::Video:
        return render_video(result, block);
      case Kind::Cover:
        return render_media_group(result, block, "cover", false);
      case Kind::Embed:
        return render_embed(result, block);
      case Kind::EmbedPost:
        return render_embed_post(result, block);
      case Kind::Collage:
        return render_collage(result, block);
      case Kind::Slideshow:
        return render_slideshow(result, block);
      case Kind::Channel:
        return render_channel(result, block);
      case Kind::Audio:
        return render_audio(result, block);
      case Kind::File:
        return render_file(result, block);
      case Kind::Math: {
        auto attributes = get_rich_block_attributes("rich_math_display", "math");
        attributes.emplace("dir", "ltr");
        attributes.emplace("inline", string());
        context_.push_tag(result, "div", attributes);
        append_html_escaped(result, block.formula);
        context_.pop_tag(result);
        return;
      }
      case Kind::Table:
        return render_table(result, block);
      case Kind::Details:
        return render_details(result, block);
      case Kind::RelatedArticles:
        return render_related_articles(result, block);
      case Kind::Map:
        return render_map(result, block, "map");
      case Kind::InputMap:
        return render_map(result, block, "input-map");
      case Kind::ButtonRow:
        return render_button_row(result, block);
      default:
        UNREACHABLE();
    }
  }
};

ExportHtmlMessageInfo ExportHtmlMessage::push(string &result, const ExportHtmlMessageInfo *previous) {
  auto info = get_message_info();
  const auto *content = message_.content_.get();
  auto kind = is_history_cleared_ ? ExportContentKind::Service : get_export_content_kind(content);
  if (kind == ExportContentKind::Ordinary && content->get_id() == td_api::messageRichMessage::ID &&
      rich_message_ == nullptr) {
    kind = ExportContentKind::Unsupported;
  }
  if (kind == ExportContentKind::Unsupported) {
    push_service_message(context_, result, base_, info.id,
                         "This message is not supported by this version of Telegram. Please update the application.",
                         nullptr);
    return info;
  }

  if (kind == ExportContentKind::Service) {
    auto service_text = is_history_cleared_ ? string("History cleared") : get_service_text(content);
    if (!service_text.empty()) {
      const auto *photo = is_history_cleared_ ? nullptr : get_service_photo(content);
      if (photo == nullptr) {
        push_service_message(context_, result, base_, info.id, service_text, nullptr);
      } else {
        UserpicData userpic;
        userpic.color_index = get_peer_color_index(static_cast<uint64>(get_export_dialog_bare_id(dialog_.dialog_id)));
        userpic.first_name = dialog_first_name_.str();
        userpic.last_name = dialog_last_name_.str();
        userpic.pixel_size = EXPORT_HTML_SERVICE_PHOTO_SIZE;
        userpic.large_link = file_.relative_path;
        if (!userpic.large_link.empty()) {
          userpic.image_link = thumbnail_.relative_path.empty() ? file_.relative_path : thumbnail_.relative_path;
        }
        push_service_message(context_, result, base_, info.id, service_text, &userpic);
      }
      return info;
    }
  }
  info.is_service = false;

  auto wrap = message_needs_wrap(info, previous);
  auto show_forwarded_info = info.is_forwarded && !info.show_forwarded_as_original;
  UserpicData forwarded_userpic;
  if (info.is_forwarded) {
    forwarded_userpic.color_index =
        info.forwarded_from_dialog_id.is_valid()
            ? get_peer_color_index(static_cast<uint64>(get_export_dialog_bare_id(info.forwarded_from_dialog_id)))
            : get_peer_color_index(static_cast<uint64>(static_cast<int64>(info.id)));
    forwarded_userpic.pixel_size = HISTORY_USERPIC_SIZE;
    if (info.forwarded_from_dialog_id.is_valid()) {
      fill_userpic_names(forwarded_userpic, info.forwarded_from_dialog_id);
    } else {
      fill_userpic_names(forwarded_userpic, info.forwarded_from_name);
    }
  }
  UserpicData userpic;
  if (info.show_forwarded_as_original) {
    userpic = forwarded_userpic;
  } else {
    userpic.color_index = get_peer_color_index(static_cast<uint64>(get_export_dialog_bare_id(info.sender_dialog_id)));
    userpic.pixel_size = HISTORY_USERPIC_SIZE;
    fill_userpic_names(userpic, info.sender_dialog_id);
  }

  string via;
  if (message_.via_bot_user_id_ != 0) {
    via = html_escape(td_->user_manager_->get_user_first_username(UserId(message_.via_bot_user_id_)));
  }

  context_.push_tag(result, "div",
                    {{"class", wrap ? Slice("message default clearfix") : Slice("message default clearfix joined")},
                     {"id", PSLICE() << "message" << info.id}});
  if (wrap) {
    context_.push_div(result, "pull_left userpic_wrap");
    push_userpic(context_, result, base_, userpic);
    context_.pop_tag(result);
  }
  context_.push_div(result, "body");
  context_.push_tag(result, "div",
                    {{"class", "pull_right date details"},
                     {"title", format_date_time(message_.date_, settings_.utc_time_offset, true)}});
  result += format_time_text(get_export_local_time(message_.date_, settings_.utc_time_offset));
  context_.pop_tag(result);
  if (wrap) {
    context_.push_div(result, "from_name");
    append_html_escaped(result, compose_name(userpic, "Deleted Account"));
    if (!via.empty() && (!info.is_forwarded || info.show_forwarded_as_original)) {
      result += " via @";
      result += via;
    }
    context_.pop_tag(result);
  }
  if (show_forwarded_info) {
    auto forwarded_wrap = forwarded_needs_wrap(info, previous);
    if (forwarded_wrap) {
      context_.push_div(result, "pull_left forwarded userpic_wrap");
      push_userpic(context_, result, base_, forwarded_userpic);
      context_.pop_tag(result);
    }
    context_.push_div(result, "forwarded body");
    if (forwarded_wrap) {
      context_.push_div(result, "from_name");
      append_html_escaped(result, compose_name(forwarded_userpic, "Deleted Account"));
      if (!via.empty()) {
        result += " via @";
        result += via;
      }
      context_.push_tag(result, "span",
                        {{"class", "date details"},
                         {"title", format_date_time(info.forwarded_date, settings_.utc_time_offset, true)},
                         {"inline", ""}});
      result += ' ';
      result += format_date_time(info.forwarded_date, settings_.utc_time_offset, false);
      context_.pop_tag(result);
      context_.pop_tag(result);
    }
  }
  if (message_.reply_to_ != nullptr && message_.reply_to_->get_id() == td_api::messageReplyToMessage::ID) {
    const auto *reply_to = static_cast<const td_api::messageReplyToMessage *>(message_.reply_to_.get());
    auto reply_to_message_id = get_export_server_message_id(reply_to->message_id_);
    if (reply_to_message_id != 0) {
      context_.push_div(result, "reply_to details");
      if (reply_to->chat_id_ != 0 && reply_to->chat_id_ != message_.chat_id_) {
        result += "In reply to a message in another chat";
      } else {
        result += "In reply to ";
        result += wrap_message_link(get_same_chat_message_id(reply_to->message_id_), "this message");
      }
      context_.pop_tag(result);
    }
  }

  push_media(result, content);

  if (rich_message_ != nullptr) {
    RichHtmlRenderer(*this, *rich_message_).render_message(result);
  } else {
    string text;
    if (content->get_id() == td_api::messageAnimatedEmoji::ID) {
      text = html_escape(static_cast<const td_api::messageAnimatedEmoji *>(content)->emoji_);
    } else {
      text = format_text(get_export_message_text(content));
    }
    if (!text.empty()) {
      context_.push_div(result, "text");
      result += text;
      context_.pop_tag(result);
    }
  }
  push_inline_buttons(result);
  if (!message_.author_signature_.empty()) {
    context_.push_div(result, "signature details");
    append_html_escaped(result, message_.author_signature_);
    context_.pop_tag(result);
  }
  if (show_forwarded_info) {
    context_.pop_tag(result);
  }
  push_reactions(result);
  context_.pop_tag(result);
  context_.pop_tag(result);
  return info;
}

}  // namespace

ExportHtmlWriter::ExportHtmlWriter(Td *td) : td_(td) {
}

ExportHtmlWriter::~ExportHtmlWriter() = default;

static constexpr int32 CHATS_SECTION_PRIORITY = 0;

static const char SUMMARY_PAGE_PATH[] = "export_results.html";

static const char DIALOGS_PAGE_PATH[] = "lists/chats.html";

unique_ptr<ExportHtmlPage> ExportHtmlWriter::create_page(Slice relative_path) const {
  string path = settings_.path;
  for (auto c : relative_path) {
    path += c == '/' ? TD_DIR_SLASH : c;
  }
  return td::make_unique<ExportHtmlPage>(std::move(path), relative_path, !settings_.html_stylesheet.empty(), stats_);
}

// the images are written into the style sheet, so that a custom style sheet can replace any of them
static string inline_export_html_images(Slice stylesheet) {
  const string text = stylesheet.str();
  const string prefix = "url(../";
  string result;
  size_t pos = 0;
  while (true) {
    auto begin = text.find(prefix, pos);
    if (begin == string::npos) {
      break;
    }
    auto end = text.find(')', begin);
    CHECK(end != string::npos);
    Slice path(text.data() + begin + prefix.size(), text.data() + end);
    CHECK(ends_with(path, ".png"));
    const ExportHtmlAsset *image = nullptr;
    for (const auto &asset : get_export_html_assets()) {
      if (path == Slice(asset.path)) {
        image = &asset;
        break;
      }
    }
    CHECK(image != nullptr);
    result.append(text, pos, begin - pos);
    result += "url(data:image/png;base64,";
    result += base64_encode(Slice(image->data, image->size));
    result += ')';
    pos = end + 1;
  }
  result.append(text, pos, string::npos);
  return result;
}

Status ExportHtmlWriter::write_file(Slice relative_path, Slice data) {
  string path = settings_.path;
  for (auto c : relative_path) {
    path += c == '/' ? TD_DIR_SLASH : c;
  }
  ExportOutputFile file(std::move(path), stats_);
  return file.write(data);
}

Status ExportHtmlWriter::start(const ExportSettings &settings, ExportStats *stats) {
  CHECK(!settings.path.empty() && settings.path.back() == TD_DIR_SLASH);
  settings_ = settings;
  stats_ = stats;

  for (const auto &asset : get_export_html_assets()) {
    Slice path(asset.path);
    Slice data(asset.data, asset.size);
    if (ends_with(path, ".png")) {
      continue;
    }
    if (path == Slice("css/style.css")) {
      TRY_STATUS(write_file(path, inline_export_html_images(data)));
    } else {
      TRY_STATUS(write_file(path, data));
    }
  }
  if (!settings_.html_stylesheet.empty()) {
    TRY_STATUS(write_file(CUSTOM_STYLESHEET_PATH, settings_.html_stylesheet));
  }

  if (settings_.only_single_dialog()) {
    return Status::OK();
  }
  summary_ = create_page(SUMMARY_PAGE_PATH);
  string block;
  summary_->push_header(block, "Exported Data");
  summary_->context().push_div(block, "page_body");
  return summary_->write(block);
}

void ExportHtmlWriter::add_section(int32 priority, Slice label, Slice type, int32 count, Slice path) {
  Section section;
  section.priority = priority;
  section.label = label.str();
  section.type = type.str();
  section.count = count;
  section.path = path.str();
  sections_.push_back(std::move(section));
}

Status ExportHtmlWriter::write_sections() {
  CHECK(summary_ != nullptr);
  if (sections_.empty()) {
    return Status::OK();
  }
  string block;
  if (!has_sections_) {
    summary_->context().push_div(block, summary_needs_divider_ ? Slice("sections with_divider") : Slice("sections"));
    has_sections_ = true;
    summary_needs_divider_ = false;
  }
  std::stable_sort(sections_.begin(), sections_.end(),
                   [](const Section &lhs, const Section &rhs) { return lhs.priority < rhs.priority; });
  for (const auto &section : sections_) {
    summary_->push_section(block, section.label, section.type, section.count, section.path);
  }
  sections_.clear();
  return summary_->write(block);
}

static constexpr int32 CONTACTS_SECTION_PRIORITY = 2;
static constexpr int32 FREQUENT_CONTACTS_SECTION_PRIORITY = 3;
static constexpr int32 SESSIONS_SECTION_PRIORITY = 7;
static constexpr int32 WEB_SESSIONS_SECTION_PRIORITY = 8;
static constexpr int32 OTHER_DATA_SECTION_PRIORITY = 9;

static string compose_name(Slice first_name, Slice last_name, Slice empty_name) {
  if (first_name.empty() && last_name.empty()) {
    return empty_name.str();
  }
  return PSTRING() << first_name << ' ' << last_name;
}

// a hash of the string, which chooses the color of an entry without an identifier
static uint64 get_string_bare_id(Slice data) {
  uint64 result = 0xFF;
  for (auto c : data) {
    result *= 239;
    result += static_cast<unsigned char>(c);
    result &= 0xFF;
  }
  return result;
}

static int32 get_application_color_index(int64 api_id) {
  switch (api_id) {
    case 1:
    case 7:
      return 0;
    case 6:
    case 21724:
      return 1;
    case 2834:
      return 2;
    case 2496:
      return 3;
    case 2040:
      return 4;
    case 1429:
      return 5;
    default:
      return get_peer_color_index(static_cast<uint64>(api_id));
  }
}

static constexpr int32 USERPICS_SECTION_PRIORITY = 4;
static constexpr int32 STORIES_SECTION_PRIORITY = 5;
static constexpr int32 PROFILE_AUDIOS_SECTION_PRIORITY = 6;

static const char USERPICS_PAGE_PATH[] = "lists/profile_pictures.html";
static const char STORIES_PAGE_PATH[] = "lists/stories.html";
static const char PROFILE_AUDIOS_PAGE_PATH[] = "lists/profile_music.html";

static Slice get_list_file_status(const ExportFile &file, Slice name, int64 size, string &status) {
  switch (file.skip_reason) {
    case ExportFile::SkipReason::None:
      status = format_file_size(size);
      return status;
    case ExportFile::SkipReason::Unavailable:
      status = PSTRING() << '(' << name << " unavailable, please try again later)";
      return status;
    case ExportFile::SkipReason::FileSize:
      status = PSTRING() << '(' << name << " exceeds maximum size. Change data exporting settings to download.)";
      return status;
    case ExportFile::SkipReason::FileType:
      status = PSTRING() << '(' << name << " not included. Change data exporting settings to download.)";
      return status;
    default:
      UNREACHABLE();
      return Slice();
  }
}

Status ExportHtmlWriter::write_prepared_personal_info(const ExportPersonalInfo &info, const ExportUserpic *userpic) {
  CHECK(summary_ != nullptr);
  UserpicData userpic_data;
  userpic_data.color_index = self_color_index_;
  userpic_data.pixel_size = EXPORT_HTML_PERSONAL_USERPIC_SIZE;
  if (userpic != nullptr && !userpic->file.relative_path.empty()) {
    userpic_data.large_link = USERPICS_PAGE_PATH;
    userpic_data.image_link = userpic->personal_thumbnail.relative_path.empty()
                                  ? userpic->file.relative_path
                                  : userpic->personal_thumbnail.relative_path;
  }
  userpic_data.first_name = info.first_name;
  userpic_data.last_name = info.last_name;

  string block;
  auto &context = summary_->context();
  context.push_div(block, "personal_info clearfix");
  context.push_div(block, "pull_right userpic_wrap");
  push_userpic(context, block, summary_->base(), userpic_data);
  context.pop_tag(block);
  struct Row {
    Slice label;
    Slice value;
  };
  auto push_rows = [&](Slice name, std::initializer_list<Row> rows) {
    context.push_div(block, PSLICE() << "rows " << name);
    for (const auto &row : rows) {
      if (row.value.empty()) {
        continue;
      }
      context.push_div(block, "row");
      context.push_div(block, "label details");
      append_html_escaped(block, row.label);
      context.pop_tag(block);
      context.push_div(block, "value bold");
      append_html_escaped(block, row.value);
      context.pop_tag(block);
      context.pop_tag(block);
    }
    context.pop_tag(block);
  };
  push_rows("names", {{"First name", info.first_name}, {"Last name", info.last_name}});
  auto phone_number = format_export_phone_number(info.phone_number);
  string username;
  if (!info.username.empty()) {
    username = PSTRING() << '@' << info.username;
  }
  push_rows("info", {{"Phone number", phone_number}, {"Username", username}});
  push_rows("bio", {{"Bio", info.bio}});
  context.pop_tag(block);

  summary_needs_divider_ = true;
  return summary_->write(block);
}

Status ExportHtmlWriter::write_personal_info(const ExportPersonalInfo &info) {
  self_color_index_ = info.color >= 0 ? info.color : get_peer_color_index(static_cast<uint64>(info.user_id));
  if ((settings_.types & ExportSettings::Userpics) != 0) {
    // the information is written with the current profile photo, which is received later
    delayed_personal_info_ = td::make_unique<ExportPersonalInfo>(info);
    return Status::OK();
  }
  return write_prepared_personal_info(info, nullptr);
}

Status ExportHtmlWriter::write_delayed_personal_info(const ExportUserpic *userpic) {
  if (delayed_personal_info_ == nullptr) {
    return Status::OK();
  }
  auto info = std::move(delayed_personal_info_);
  TRY_STATUS(write_prepared_personal_info(*info, userpic));
  if (userpic_count_ > 0) {
    add_section(USERPICS_SECTION_PRIORITY, "Profile pictures", "photos", userpic_count_, USERPICS_PAGE_PATH);
  }
  return Status::OK();
}

Status ExportHtmlWriter::write_userpics_start(int32 count) {
  CHECK(summary_ != nullptr);
  CHECK(userpics_page_ == nullptr);
  userpic_count_ = count;
  if (count <= 0) {
    return Status::OK();
  }
  string block;
  userpics_page_ = create_list_page(USERPICS_PAGE_PATH, "Profile pictures", Slice(), block);
  TRY_STATUS(userpics_page_->write(block));
  if (delayed_personal_info_ == nullptr) {
    add_section(USERPICS_SECTION_PRIORITY, "Profile pictures", "photos", userpic_count_, USERPICS_PAGE_PATH);
  }
  return Status::OK();
}

Status ExportHtmlWriter::write_userpics_slice(const vector<ExportUserpic> &userpics) {
  if (userpics.empty()) {
    return Status::OK();
  }
  TRY_STATUS(write_delayed_personal_info(&userpics[0]));
  if (userpics_page_ == nullptr) {
    return Status::OK();
  }

  string block;
  string status;
  for (const auto &userpic : userpics) {
    const auto &path = userpic.file.relative_path;
    UserpicData userpic_data;
    userpic_data.color_index = self_color_index_;
    userpic_data.pixel_size = EXPORT_HTML_ENTRY_USERPIC_SIZE;
    if (!path.empty()) {
      userpic_data.image_link = userpic.thumbnail.relative_path.empty() ? path : userpic.thumbnail.relative_path;
    }
    userpic_data.first_name = path;
    string date;
    if (userpic.date > 0) {
      date = format_date_time(userpic.date, settings_.utc_time_offset, false);
    }
    userpics_page_->push_list_entry(block, path, userpic_data, path.empty() ? Slice("Photo unavailable") : Slice(path),
                                    Slice(), {get_list_file_status(userpic.file, "Photo", userpic.size, status)}, date);
  }
  return userpics_page_->write(block);
}

Status ExportHtmlWriter::write_userpics_end() {
  TRY_STATUS(write_delayed_personal_info(nullptr));
  if (userpics_page_ == nullptr) {
    return Status::OK();
  }
  auto page = std::move(userpics_page_);
  return page->close();
}

Status ExportHtmlWriter::write_stories_start(int32 count) {
  CHECK(summary_ != nullptr);
  CHECK(stories_page_ == nullptr);
  story_count_ = count;
  if (count <= 0) {
    return Status::OK();
  }
  string block;
  stories_page_ = create_list_page(STORIES_PAGE_PATH, "Stories archive", Slice(), block);
  return stories_page_->write(block);
}

Status ExportHtmlWriter::write_stories_slice(const vector<ExportStory> &stories, int32 skipped_count) {
  story_count_ -= skipped_count;
  if (stories_page_ == nullptr || stories.empty()) {
    return Status::OK();
  }

  string block;
  string status;
  vector<string> details;
  for (const auto &story : stories) {
    const auto &path = story.file.relative_path;
    details.clear();
    if (story.is_pinned) {
      details.push_back("Saved to Profile");
    }
    if (story.expire_date > 0) {
      details.push_back(PSTRING() << "Expiring: "
                                  << format_date_time(story.expire_date, settings_.utc_time_offset, false));
    }
    details.push_back(get_list_file_status(story.file, "Story", story.size, status).str());
    string date;
    if (story.date > 0) {
      date = format_date_time(story.date, settings_.utc_time_offset, false);
    }
    Slice image_link = story.thumbnail.relative_path.empty() ? Slice(path) : Slice(story.thumbnail.relative_path);
    stories_page_->push_story_entry(block, path, image_link, path.empty() ? Slice("Story unavailable") : Slice(path),
                                    format_text(story.caption.get(), nullptr, stories_page_->base()), details, date);
  }
  return stories_page_->write(block);
}

Status ExportHtmlWriter::write_stories_end() {
  // unlike the other lists, the count of stories is known only after all of them are received
  if (story_count_ > 0) {
    add_section(STORIES_SECTION_PRIORITY, "Stories archive", "stories", story_count_, STORIES_PAGE_PATH);
  }
  if (stories_page_ == nullptr) {
    return Status::OK();
  }
  auto page = std::move(stories_page_);
  return page->close();
}

Status ExportHtmlWriter::write_profile_audios_start(int32 count) {
  CHECK(summary_ != nullptr);
  CHECK(profile_audios_page_ == nullptr);
  profile_audio_count_ = count;
  if (count <= 0) {
    return Status::OK();
  }
  string block;
  profile_audios_page_ = create_list_page(PROFILE_AUDIOS_PAGE_PATH, "Profile Music", Slice(), block);
  return profile_audios_page_->write(block);
}

Status ExportHtmlWriter::write_profile_audios_slice(const vector<ExportMessage> &audios) {
  if (profile_audios_page_ == nullptr || audios.empty()) {
    return Status::OK();
  }

  string block;
  string status;
  for (const auto &export_message : audios) {
    const auto *content = export_message.message->content_.get();
    CHECK(content->get_id() == td_api::messageAudio::ID);
    const auto *audio = static_cast<const td_api::messageAudio *>(content)->audio_.get();
    const auto &file = export_message.file;
    Slice title = !audio->title_.empty()       ? Slice(audio->title_)
                  : !audio->file_name_.empty() ? Slice(audio->file_name_)
                                               : Slice("Unknown Track");
    Slice performer = !audio->performer_.empty() ? Slice(audio->performer_) : Slice("Unknown Artist");
    auto name = PSTRING() << performer << " - " << title;
    string duration;
    if (audio->duration_ > 0) {
      duration = format_duration(audio->duration_);
    }
    profile_audios_page_->push_audio_entry(
        block, file.relative_path, name,
        get_list_file_status(file, "File", get_export_file_size(audio->audio_.get()), status), duration);
  }
  return profile_audios_page_->write(block);
}

Status ExportHtmlWriter::write_profile_audios_end() {
  if (profile_audio_count_ > 0) {
    add_section(PROFILE_AUDIOS_SECTION_PRIORITY, "Profile Music", "music", profile_audio_count_,
                PROFILE_AUDIOS_PAGE_PATH);
  }
  if (profile_audios_page_ == nullptr) {
    return Status::OK();
  }
  auto page = std::move(profile_audios_page_);
  return page->close();
}

unique_ptr<ExportHtmlPage> ExportHtmlWriter::create_list_page(Slice relative_path, Slice header, Slice about,
                                                              string &block) const {
  auto page = create_page(relative_path);
  page->push_header(block, header, SUMMARY_PAGE_PATH);
  page->context().push_div(block, "page_body list_page");
  if (!about.empty()) {
    page->push_about(block, about);
  }
  page->context().push_div(block, "entry_list");
  return page;
}

static Status write_list_page(ExportHtmlPage &page, Slice block) {
  TRY_STATUS(page.write(block));
  return page.close();
}

Status ExportHtmlWriter::write_contacts(const ExportContactList &contacts) {
  CHECK(summary_ != nullptr);
  if (!contacts.contacts.empty()) {
    static const char PATH[] = "lists/contacts.html";
    string block;
    auto page = create_list_page(PATH, "Contacts", get_export_about_text(ExportAbout::Contacts), block);
    for (const auto &contact : contacts.contacts) {
      UserpicData userpic;
      userpic.color_index = get_peer_color_index(get_string_bare_id(contact.phone_number));
      userpic.pixel_size = EXPORT_HTML_ENTRY_USERPIC_SIZE;
      userpic.first_name = contact.first_name;
      userpic.last_name = contact.last_name;
      page->push_list_entry(block, Slice(), userpic,
                            compose_name(contact.first_name, contact.last_name, "Deleted Account"), Slice(),
                            {format_export_phone_number(contact.phone_number)},
                            format_date_time(contact.date, settings_.utc_time_offset, false));
    }
    TRY_STATUS(write_list_page(*page, block));
    add_section(CONTACTS_SECTION_PRIORITY, "Contacts", "contacts", narrow_cast<int32>(contacts.contacts.size()), PATH);
  }

  auto top_peer_count = contacts.correspondents.size() + contacts.inline_bots.size() + contacts.phone_calls.size();
  if (top_peer_count == 0) {
    return Status::OK();
  }
  static const char PATH[] = "lists/frequent.html";
  string block;
  auto page = create_list_page(PATH, "Frequent contacts", get_export_about_text(ExportAbout::FrequentContacts), block);
  auto push_top_peers = [&](const vector<ExportTopPeer> &top_peers, Slice category) {
    for (const auto &top_peer : top_peers) {
      auto dialog_id = top_peer.dialog_id;
      UserpicData userpic;
      userpic.color_index = get_peer_color_index(static_cast<uint64>(get_export_dialog_bare_id(dialog_id)));
      userpic.pixel_size = EXPORT_HTML_ENTRY_USERPIC_SIZE;
      if (dialog_id.get_type() != DialogType::User) {
        userpic.first_name = td_->dialog_manager_->get_dialog_title(dialog_id);
      } else if (dialog_id.get_user_id() == td_->user_manager_->get_my_id()) {
        userpic.first_name = "Saved messages";
      } else {
        userpic.first_name = td_->user_manager_->get_user_first_name(dialog_id.get_user_id()).str();
        userpic.last_name = td_->user_manager_->get_user_last_name(dialog_id.get_user_id()).str();
      }
      auto rating = PSTRING() << "Rating: " << StringBuilder::FixedDouble(top_peer.rating, 6);
      page->push_list_entry(block, Slice(), userpic,
                            compose_name(userpic.first_name, userpic.last_name, "Deleted Account"), Slice(), {rating},
                            category);
    }
  };
  push_top_peers(contacts.correspondents, "people");
  push_top_peers(contacts.inline_bots, "inline bots");
  push_top_peers(contacts.phone_calls, "calls");
  TRY_STATUS(write_list_page(*page, block));
  add_section(FREQUENT_CONTACTS_SECTION_PRIORITY, "Frequent contacts", "frequent", narrow_cast<int32>(top_peer_count),
              PATH);
  return Status::OK();
}

Status ExportHtmlWriter::write_sessions(const ExportSessionList &sessions) {
  CHECK(summary_ != nullptr);
  auto utc_time_offset = settings_.utc_time_offset;
  auto push_session = [](ExportHtmlPage &page, string &block, int64 api_id, Slice name, Slice subname,
                         std::initializer_list<Slice> details, Slice info) {
    UserpicData userpic;
    userpic.color_index = get_application_color_index(api_id);
    userpic.pixel_size = EXPORT_HTML_ENTRY_USERPIC_SIZE;
    userpic.first_name = name.str();
    page.push_list_entry(block, Slice(), userpic, name, subname, details, info);
  };
  // U+2013 EN DASH
  static const char DASH[] = " \xE2\x80\x93 ";

  if (!sessions.sessions.empty()) {
    static const char PATH[] = "lists/sessions.html";
    string block;
    auto page = create_list_page(PATH, "Sessions", get_export_about_text(ExportAbout::Sessions), block);
    for (const auto &session : sessions.sessions) {
      auto name = PSTRING() << (session.application_name.empty() ? Slice("Unknown") : Slice(session.application_name))
                            << ' ' << session.application_version;
      auto subname = PSTRING() << session.device_model << ", " << session.platform << ' ' << session.system_version;
      auto location = PSTRING() << session.ip << DASH << session.region
                                << (session.region.empty() || session.country.empty() ? "" : ", ") << session.country;
      auto last_active = PSTRING() << "Last active: " << format_date_time(session.last_active, utc_time_offset, false);
      auto created = PSTRING() << "Created: " << format_date_time(session.created, utc_time_offset, false);
      push_session(*page, block, session.api_id, name, subname, {location, last_active, created}, Slice());
    }
    TRY_STATUS(write_list_page(*page, block));
    add_section(SESSIONS_SECTION_PRIORITY, "Sessions", "sessions", narrow_cast<int32>(sessions.sessions.size()), PATH);
  }

  if (sessions.web_sessions.empty()) {
    return Status::OK();
  }
  static const char PATH[] = "lists/web_sessions.html";
  string block;
  auto page = create_list_page(PATH, "Web sessions", get_export_about_text(ExportAbout::WebSessions), block);
  for (const auto &session : sessions.web_sessions) {
    auto api_id = 0x1000 + static_cast<int64>(get_string_bare_id(session.domain));
    auto subname = PSTRING() << session.platform << ", " << session.browser;
    auto location = PSTRING() << session.ip << DASH << session.region;
    auto last_active = PSTRING() << "Last active: " << format_date_time(session.last_active, utc_time_offset, false);
    auto created = PSTRING() << "Created: " << format_date_time(session.created, utc_time_offset, false);
    string bot_username;
    if (!session.bot_username.empty()) {
      bot_username = PSTRING() << '@' << session.bot_username;
    }
    push_session(*page, block, api_id, session.domain.empty() ? Slice("Unknown") : Slice(session.domain), subname,
                 {location, last_active, created}, bot_username);
  }
  TRY_STATUS(write_list_page(*page, block));
  add_section(WEB_SESSIONS_SECTION_PRIORITY, "Web sessions", "web", narrow_cast<int32>(sessions.web_sessions.size()),
              PATH);
  return Status::OK();
}

Status ExportHtmlWriter::write_other_data(const ExportFile &file) {
  CHECK(summary_ != nullptr);
  if (file.skip_reason == ExportFile::SkipReason::None) {
    add_section(OTHER_DATA_SECTION_PRIORITY, "Other data", "other", 1, file.relative_path);
  }
  return Status::OK();
}

Status ExportHtmlWriter::write_dialogs_start(int32 dialog_count) {
  CHECK(dialogs_page_ == nullptr);
  if (settings_.only_single_dialog() || dialog_count == 0) {
    return Status::OK();
  }
  dialogs_page_ = create_page(DIALOGS_PAGE_PATH);
  string block;
  dialogs_page_->push_header(block, "Chats", SUMMARY_PAGE_PATH);
  dialogs_page_->context().push_div(block, "page_body list_page");
  TRY_STATUS(dialogs_page_->write(block));

  add_section(CHATS_SECTION_PRIORITY, "Chats", "chats", dialog_count, DIALOGS_PAGE_PATH);
  return write_sections();
}

Status ExportHtmlWriter::write_dialog_start(const ExportDialogInfo &dialog) {
  CHECK(page_ == nullptr);
  CHECK(settings_.only_single_dialog() || dialogs_page_ != nullptr);
  dialog_ = dialog;
  if (dialog.dialog_id.get_type() == DialogType::User) {
    dialog_first_name_ = td_->user_manager_->get_user_first_name(dialog.dialog_id.get_user_id()).str();
    dialog_last_name_ = td_->user_manager_->get_user_last_name(dialog.dialog_id.get_user_id()).str();
  } else {
    dialog_first_name_ = dialog.name;
    dialog_last_name_.clear();
  }
  page_ = create_page(PSLICE() << dialog_.relative_path << get_export_html_page_name(0));
  is_page_empty_ = true;
  message_count_ = 0;
  date_message_id_ = 0;
  last_message_info_ = nullptr;
  last_message_ids_per_page_.clear();
  return Status::OK();
}

Status ExportHtmlWriter::write_page_opening(int32 index) {
  string name;
  if (dialog_first_name_.empty() && dialog_last_name_.empty()) {
    name = "Deleted Account";
  } else {
    name = PSTRING() << dialog_first_name_ << ' ' << dialog_last_name_;
  }
  string block;
  page_->push_header(block, name, settings_.only_single_dialog() ? Slice() : Slice(DIALOGS_PAGE_PATH));
  auto &context = page_->context();
  context.push_div(block, "page_body chat_page");
  context.push_div(block, "history");
  if (index > 0) {
    context.push_tag(block, "a", {{"class", "pagination block_link"}, {"href", get_export_html_page_name(index - 1)}});
    block += "Previous messages";
    context.pop_tag(block);
  }
  return page_->write(block);
}

Status ExportHtmlWriter::switch_to_next_page(int32 index) {
  CHECK(page_ != nullptr);
  auto next_page_name = get_export_html_page_name(index);
  string block;
  auto &context = page_->context();
  context.push_tag(block, "a", {{"class", "pagination block_link"}, {"href", next_page_name}});
  block += "Next messages";
  context.pop_tag(block);
  TRY_STATUS(page_->write(block));
  TRY_STATUS(page_->close());
  page_ = create_page(PSLICE() << dialog_.relative_path << next_page_name);
  is_page_empty_ = true;
  return Status::OK();
}

Status ExportHtmlWriter::write_dialog_messages(const vector<ExportMessage> &messages, bool is_migrated,
                                               const ExportCustomEmojiFiles &custom_emoji_files) {
  CHECK(page_ != nullptr);
  auto old_index = message_count_ > 0 ? (message_count_ - 1) / MESSAGES_IN_PAGE : 0;
  const ExportHtmlMessageInfo *previous = last_message_info_.get();
  ExportHtmlMessageInfo saved;
  bool has_saved = false;
  string block;
  for (const auto &export_message : messages) {
    const auto &message = export_message.message;
    if (message == nullptr || !settings_.is_inside_date_range(message->date_)) {
      continue;
    }
    auto new_index = message_count_ / MESSAGES_IN_PAGE;
    if (old_index != new_index) {
      CHECK(has_saved || last_message_info_ != nullptr);
      TRY_STATUS(page_->write(block));
      last_message_ids_per_page_.push_back(has_saved ? saved.id : last_message_info_->id);
      TRY_STATUS(switch_to_next_page(new_index));
      block.clear();
      last_message_info_ = nullptr;
      previous = nullptr;
      has_saved = false;
      old_index = new_index;
    }
    if (is_page_empty_) {
      TRY_STATUS(write_page_opening(old_index));
      is_page_empty_ = false;
    }
    auto &context = page_->context();
    if (previous == nullptr || get_local_day(message->date_, settings_.utc_time_offset) !=
                                   get_local_day(previous->date, settings_.utc_time_offset)) {
      push_service_message(context, block, page_->base(), --date_message_id_,
                           format_date_text(get_export_local_time(message->date_, settings_.utc_time_offset)), nullptr);
    }
    auto info = ExportHtmlMessage(td_, settings_, dialog_, dialog_first_name_, dialog_last_name_, *page_,
                                  export_message, is_migrated, custom_emoji_files, last_message_ids_per_page_)
                    .push(block, previous);
    message_count_++;
    saved = std::move(info);
    has_saved = true;
    previous = &saved;
  }
  if (has_saved) {
    last_message_info_ = td::make_unique<ExportHtmlMessageInfo>(std::move(saved));
  }
  if (block.empty()) {
    return Status::OK();
  }
  return page_->write(block);
}

Status ExportHtmlWriter::write_dialog_end(bool only_my_messages) {
  CHECK(page_ != nullptr);
  dialog_.only_my_messages = only_my_messages;
  if (message_count_ == 0 && settings_.only_single_dialog()) {
    CHECK(is_page_empty_);
    TRY_STATUS(write_page_opening(0));
    string block;
    push_service_message(page_->context(), block, page_->base(), --date_message_id_, "No exported messages", nullptr);
    TRY_STATUS(page_->write(block));
  }
  // a page without messages isn't created
  auto status = page_->close();
  page_ = nullptr;
  TRY_STATUS(std::move(status));
  if (settings_.only_single_dialog()) {
    return Status::OK();
  }
  return write_dialog_list_entry();
}

Status ExportHtmlWriter::write_dialog_list_start(bool is_left_channel) {
  auto mode = is_left_channel ? DialogsMode::Left : DialogsMode::Chats;
  if (dialogs_mode_ == mode) {
    return Status::OK();
  }
  string block;
  if (dialogs_mode_ != DialogsMode::None) {
    dialogs_page_->context().pop_tag(block);
  }
  dialogs_mode_ = mode;
  dialogs_page_->push_about(block,
                            get_export_about_text(is_left_channel ? ExportAbout::LeftChats : ExportAbout::Chats));
  dialogs_page_->context().push_div(block, "entry_list");
  return dialogs_page_->write(block);
}

Status ExportHtmlWriter::write_dialog_list_entry() {
  CHECK(dialogs_page_ != nullptr);
  using Type = ExportDialogInfo::Type;
  Slice type;
  Slice deleted_name;
  switch (dialog_.type) {
    case Type::Unknown:
      type = Slice("unknown");
      deleted_name = Slice("Deleted Account");
      break;
    case Type::Self:
    case Type::Replies:
    case Type::VerifyCodes:
    case Type::Personal:
      type = Slice("private");
      deleted_name = Slice("Deleted Account");
      break;
    case Type::Bot:
      type = Slice("bot");
      deleted_name = Slice("Deleted Account");
      break;
    case Type::PrivateGroup:
    case Type::PrivateSupergroup:
    case Type::PublicSupergroup:
      type = Slice("group");
      deleted_name = Slice("Deleted Group");
      break;
    case Type::PrivateChannel:
    case Type::PublicChannel:
      type = Slice("channel");
      deleted_name = Slice("Deleted Channel");
      break;
    default:
      UNREACHABLE();
  }

  UserpicData userpic;
  userpic.pixel_size = EXPORT_HTML_ENTRY_USERPIC_SIZE;
  switch (dialog_.type) {
    case Type::Self:
      userpic.color_index = SAVED_MESSAGES_COLOR_INDEX;
      userpic.first_name = "Saved messages";
      break;
    case Type::Replies:
      userpic.color_index = SAVED_MESSAGES_COLOR_INDEX;
      userpic.first_name = "Replies";
      break;
    case Type::VerifyCodes:
      userpic.color_index = SAVED_MESSAGES_COLOR_INDEX;
      userpic.first_name = "Verification Codes";
      break;
    default:
      userpic.color_index = get_peer_color_index(static_cast<uint64>(get_export_dialog_bare_id(dialog_.dialog_id)));
      userpic.first_name = dialog_first_name_;
      if (dialog_.type == Type::Personal || dialog_.type == Type::Bot) {
        userpic.last_name = dialog_last_name_;
      }
      break;
  }
  string name;
  if (userpic.first_name.empty() && userpic.last_name.empty()) {
    name = deleted_name.str();
  } else {
    name = PSTRING() << userpic.first_name << ' ' << userpic.last_name;
  }

  auto is_outgoing = dialog_.only_my_messages;
  string count;
  if (message_count_ == 1) {
    count = is_outgoing ? "1 outgoing message" : "1 message";
  } else if (message_count_ == 0) {
    count = is_outgoing ? "No outgoing messages" : "No messages";
  } else {
    count = PSTRING() << message_count_ << (is_outgoing ? " outgoing messages" : " messages");
  }
  string link;
  if (message_count_ > 0) {
    link = PSTRING() << dialog_.relative_path << get_export_html_page_name(0);
  }

  TRY_STATUS(write_dialog_list_start(dialog_.is_left_channel));
  string block;
  dialogs_page_->push_list_entry(block, link, userpic, name, Slice(), {count}, type);
  return dialogs_page_->write(block);
}

Status ExportHtmlWriter::write_dialogs_end() {
  if (dialogs_page_ == nullptr) {
    return Status::OK();
  }
  auto status = dialogs_page_->close();
  dialogs_page_ = nullptr;
  return status;
}

Status ExportHtmlWriter::finish() {
  if (settings_.only_single_dialog()) {
    return Status::OK();
  }
  CHECK(summary_ != nullptr);
  TRY_STATUS(write_sections());
  string block;
  if (has_sections_) {
    summary_->context().pop_tag(block);
    summary_needs_divider_ = true;
    has_sections_ = false;
  }
  summary_->push_about(block, get_export_about_text(ExportAbout::Telegram), summary_needs_divider_);
  TRY_STATUS(summary_->write(block));
  auto status = summary_->close();
  summary_ = nullptr;
  return status;
}

}  // namespace td
