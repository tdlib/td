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
#include "td/telegram/LinkManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserId.h"
#include "td/telegram/UserManager.h"

#include "td/utils/base64.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/utf8.h"

#include <cstdlib>
#include <initializer_list>
#include <locale>
#include <sstream>
#include <utility>

namespace td {

static constexpr int32 MESSAGES_IN_PAGE = 1000;
static constexpr int32 HISTORY_USERPIC_SIZE = 42;
static constexpr int32 REACTION_USERPIC_SIZE = 20;
static constexpr int32 JOIN_WITHIN_SECONDS = 900;
static constexpr int32 STICKER_MAX_SIZE = 384;
static constexpr int32 THUMBNAIL_MIN_SIZE = 80;
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

    Tag data{tag, true};
    bool is_empty = false;
    if (data.is_block) {
      for (size_t i = 0; i < count; i++) {
        if (sorted[i].name == "inline") {
          data.is_block = false;
        }
      }
    }
    if (data.is_block) {
      result += '\n';
      append_indent(result);
    }
    result += '<';
    result.append(tag.data(), tag.size());
    for (size_t i = 0; i < count; i++) {
      if (sorted[i].name == "inline") {
        continue;
      }
      if (sorted[i].name == "empty") {
        is_empty = true;
        continue;
      }
      result += ' ';
      result.append(sorted[i].name.data(), sorted[i].name.size());
      result += "=\"";
      append_html_escaped(result, sorted[i].value);
      result += '"';
    }
    if (is_empty) {
      result += '/';
    }
    result += '>';
    if (data.is_block) {
      result += '\n';
    }
    if (!is_empty) {
      tags_.push_back(data);
    }
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

  void push_header(string &result, Slice header) {
    context_.push_div(result, "page_header");
    context_.push_div(result, "content");
    context_.push_div(result, "text bold");
    append_html_escaped(result, header);
    context_.pop_tag(result);
    context_.pop_tag(result);
    context_.pop_tag(result);
  }

 private:
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

namespace {

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
      , is_history_cleared_(export_message.is_history_cleared)
      , is_migrated_(is_migrated)
      , custom_emoji_files_(custom_emoji_files)
      , last_message_ids_per_page_(last_message_ids_per_page) {
  }

  ExportHtmlMessageInfo push(string &result, const ExportHtmlMessageInfo *previous);

 private:
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

  void push_photo_media(string &result, const td_api::photoSize *size);

  void push_sticker_media(string &result, const td_api::sticker *sticker);

  void push_animated_media(string &result, const td_api::animation *animation);

  void push_video_file_media(string &result, const td_api::video *video);

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

static MediaData get_audio_media_data(const td_api::audio *audio, const ExportFile &file) {
  MediaData result;
  if (!audio->performer_.empty() && !audio->title_.empty()) {
    result.title = PSTRING() << audio->performer_ << " \xE2\x80\x93 " << audio->title_;
  } else if (!audio->file_name_.empty()) {
    result.title = audio->file_name_;
  } else {
    result.title = "Audio file";
  }
  result.status = format_duration(audio->duration_);
  if (file.relative_path.empty()) {
    result.status += ", ";
    result.status += format_file_size(get_export_file_size(audio->audio_.get()));
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
    case td_api::messageAudio::ID:
      result = get_audio_media_data(static_cast<const td_api::messageAudio *>(content)->audio_.get(), file_);
      break;
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
      return push_photo_media(
          result, get_export_photo_size(static_cast<const td_api::messagePhoto *>(content)->photo_->sizes_));
    case td_api::messageSticker::ID:
      return push_sticker_media(result, static_cast<const td_api::messageSticker *>(content)->sticker_.get());
    case td_api::messageAnimation::ID:
      return push_animated_media(result, static_cast<const td_api::messageAnimation *>(content)->animation_.get());
    case td_api::messageVideo::ID:
      return push_video_file_media(result, static_cast<const td_api::messageVideo *>(content)->video_.get());
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

void ExportHtmlMessage::push_photo_media(string &result, const td_api::photoSize *size) {
  auto thumbnail_size = file_.relative_path.empty() || size == nullptr
                            ? ExportHtmlImageSize()
                            : get_thumbnail_size(size->width_, size->height_, EXPORT_HTML_PHOTO_SIZE, false);
  if (thumbnail_size.empty()) {
    MediaData generic;
    generic.title = "Photo";
    if (size != nullptr) {
      generic.status = PSTRING() << size->width_ << "\xC3\x97" << size->height_;
    }
    if (file_.relative_path.empty()) {
      generic.status += ", ";
      generic.status += format_file_size(size == nullptr ? 0 : get_export_file_size(size->photo_.get()));
    } else {
      generic.link = file_.relative_path;
    }
    generic.description = get_no_file_description(file_.skip_reason).str();
    generic.classes = "media_photo";
    return push_generic_media(result, generic);
  }
  // the downloaded smaller photo is used as the thumbnail, or the photo itself if there is none
  const auto &image = thumbnail_.relative_path.empty() ? file_.relative_path : thumbnail_.relative_path;
  context_.push_div(result, "media_wrap clearfix");
  context_.push_tag(result, "a",
                    {{"class", "photo_wrap clearfix pull_left"}, {"href", PSLICE() << base_ << file_.relative_path}});
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

void ExportHtmlMessage::push_video_file_media(string &result, const td_api::video *video) {
  auto thumbnail_size = get_thumbnail_size(video->width_, video->height_, EXPORT_HTML_PHOTO_SIZE, true);
  if (thumbnail_.relative_path.empty() || file_.relative_path.empty() || thumbnail_size.empty()) {
    MediaData generic;
    generic.title = "Video file";
    generic.status = format_duration(video->duration_);
    if (file_.relative_path.empty()) {
      generic.status += ", ";
      generic.status += format_file_size(get_export_file_size(video->video_.get()));
    } else {
      generic.link = file_.relative_path;
    }
    generic.description = get_no_file_description(file_.skip_reason).str();
    generic.classes = "media_video";
    return push_generic_media(result, generic);
  }
  context_.push_div(result, "media_wrap clearfix");
  context_.push_tag(
      result, "a",
      {{"class", "video_file_wrap clearfix pull_left"}, {"href", PSLICE() << base_ << file_.relative_path}});
  context_.push_div(result, "video_play_bg");
  context_.push_div(result, "video_play");
  context_.pop_tag(result);
  context_.pop_tag(result);
  context_.push_div(result, "video_duration");
  result += format_duration(video->duration_);
  context_.pop_tag(result);
  context_.push_tag(result, "img",
                    {{"class", "video_file"},
                     {"style", get_size_style(thumbnail_size.width / 2, thumbnail_size.height / 2)},
                     {"src", PSLICE() << base_ << thumbnail_.relative_path},
                     {"empty", ""}});
  context_.pop_tag(result);
  context_.pop_tag(result);
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

ExportHtmlMessageInfo ExportHtmlMessage::push(string &result, const ExportHtmlMessageInfo *previous) {
  auto info = get_message_info();
  const auto *content = message_.content_.get();
  auto kind = is_history_cleared_ ? ExportContentKind::Service : get_export_content_kind(content);
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
  if (!settings.only_single_dialog()) {
    return Status::Error("Account export isn't supported");
  }
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
  return Status::OK();
}

Status ExportHtmlWriter::write_dialog_start(const ExportDialogInfo &dialog) {
  CHECK(page_ == nullptr);
  dialog_ = dialog;
  if (dialog.dialog_id.get_type() == DialogType::User) {
    dialog_first_name_ = td_->user_manager_->get_user_first_name(dialog.dialog_id.get_user_id()).str();
    dialog_last_name_ = td_->user_manager_->get_user_last_name(dialog.dialog_id.get_user_id()).str();
  } else {
    dialog_first_name_ = dialog.name;
    dialog_last_name_.clear();
  }
  page_ = create_page(get_export_html_page_name(0));
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
  page_->push_header(block, name);
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
  page_ = create_page(next_page_name);
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

Status ExportHtmlWriter::write_dialog_end() {
  CHECK(page_ != nullptr);
  if (message_count_ == 0) {
    CHECK(is_page_empty_);
    TRY_STATUS(write_page_opening(0));
    string block;
    push_service_message(page_->context(), block, page_->base(), --date_message_id_, "No exported messages", nullptr);
    TRY_STATUS(page_->write(block));
  }
  auto status = page_->close();
  page_ = nullptr;
  return status;
}

Status ExportHtmlWriter::finish() {
  return Status::OK();
}

}  // namespace td
