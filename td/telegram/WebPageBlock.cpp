//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebPageBlock.h"

#include "td/telegram/AccentColorId.h"
#include "td/telegram/AnimationsManager.h"
#include "td/telegram/AnimationsManager.hpp"
#include "td/telegram/AudiosManager.h"
#include "td/telegram/AudiosManager.hpp"
#include "td/telegram/ChannelId.h"
#include "td/telegram/ChatManager.h"
#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogPhoto.h"
#include "td/telegram/DialogPhoto.hpp"
#include "td/telegram/Dimensions.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DocumentsManager.hpp"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/FormattedDate.h"
#include "td/telegram/FormattedDate.hpp"
#include "td/telegram/LinkManager.h"
#include "td/telegram/Location.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/PeerColor.h"
#include "td/telegram/Photo.h"
#include "td/telegram/Photo.hpp"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/ThemeManager.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/Version.h"
#include "td/telegram/VideosManager.h"
#include "td/telegram/VideosManager.hpp"
#include "td/telegram/VoiceNotesManager.h"
#include "td/telegram/VoiceNotesManager.hpp"
#include "td/telegram/WebPageId.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/tl_helpers.h"

#include <type_traits>
#include <unordered_map>

namespace td {

class RichText;

struct GetWebPageBlockObjectContext {
  Td *td_ = nullptr;
  Slice base_url_;
  string real_url_host_;
  string real_url_rhash_;
  bool skip_bot_commands_ = false;

  bool is_first_pass_ = true;
  bool has_anchor_urls_ = false;
  std::unordered_map<Slice, const RichText *, SliceHash> anchors_;  // anchor -> text
};

static vector<td_api::object_ptr<td_api::PageBlock>> get_page_blocks_object(
    const vector<unique_ptr<WebPageBlock>> &page_blocks, GetWebPageBlockObjectContext *context) {
  return transform(page_blocks, [context](const unique_ptr<WebPageBlock> &page_block) {
    return page_block->get_page_block_object(context);
  });
}

class RichText {
  static vector<td_api::object_ptr<td_api::RichText>> get_rich_texts_object(const vector<RichText> &rich_texts,
                                                                            GetWebPageBlockObjectContext *context) {
    return transform(rich_texts,
                     [context](const RichText &rich_text) { return rich_text.get_rich_text_object(context); });
  }

  static vector<telegram_api::object_ptr<telegram_api::RichText>> get_input_rich_texts(
      const vector<RichText> &rich_texts, const Td *td,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) {
    return transform(rich_texts,
                     [&](const RichText &rich_text) { return rich_text.get_input_rich_text(td, documents); });
  }

  static string trim_first(const string &str, char c) {
    if (!str.empty() && str[0] == c) {
      return str.substr(1);
    }
    LOG(ERROR) << "Expected string starting with '" << c << "', but received \"" << str << '"';
    return str;
  }

 public:
  enum class Type : int32 {
    Plain,
    Bold,
    Italic,
    Underline,
    Strikethrough,
    Fixed,
    Url,
    EmailAddress,
    Concatenation,
    Subscript,
    Superscript,
    Marked,
    PhoneNumber,
    Icon,
    Anchor,
    Math,
    CustomEmoji,
    Spoiler,
    Mention,
    Hashtag,
    Cashtag,
    BotCommand,
    AutoUrl,
    AutoEmailAddress,
    AutoPhoneNumber,
    FormattedDate,
    BankCardNumber,
    MentionName
  };
  Type type = Type::Plain;
  string content;
  vector<RichText> texts;
  FileId document_file_id;
  CustomEmojiId custom_emoji_id;
  WebPageId web_page_id;
  FormattedDate date;
  UserId user_id;

  bool empty() const {
    return type == Type::Plain && content.empty();
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const {
    if (type == RichText::Type::Icon) {
      CHECK(document_file_id.is_valid());
      Document(Document::Type::General, document_file_id).append_file_ids(td, file_ids);
    } else {
      for (auto &text : texts) {
        text.append_file_ids(td, file_ids);
      }
    }
  }

  void add_dependencies(Dependencies &dependencies) const {
    for (auto &text : texts) {
      text.add_dependencies(dependencies);
    }
    dependencies.add(web_page_id);
    dependencies.add(user_id);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const {
    if (recurse_text) {
      for (auto &text : texts) {
        text.for_each_rich_text(recurse_text, callback);
      }
    }
    callback(this);
  }

  string get_full_text() const {
    string result;
    for_each_rich_text(true, [&](const RichText *text) {
      if (text->type == RichText::Type::Plain || text->type == RichText::Type::Math ||
          text->type == RichText::Type::CustomEmoji) {
        result += text->content;
      }
    });
    return result;
  }

  int32 get_index_mask() const {
    switch (type) {
      case Type::Url:
        if (content[0] == '#') {
          return 0;
        }
        return message_search_filter_index_mask(MessageSearchFilter::Url);
      case Type::AutoUrl:
      case Type::EmailAddress:
      case Type::AutoEmailAddress:
        return message_search_filter_index_mask(MessageSearchFilter::Url);
      default:
        return 0;
    }
  }

  RichText clone() const {
    return *this;
  }

  telegram_api::object_ptr<telegram_api::RichText> get_input_rich_text(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const {
    switch (type) {
      case Type::Plain:
        if (content.empty()) {
          return telegram_api::make_object<telegram_api::textEmpty>();
        }
        return telegram_api::make_object<telegram_api::textPlain>(content);
      case Type::Bold:
        return telegram_api::make_object<telegram_api::textBold>(texts[0].get_input_rich_text(td, documents));
      case Type::Italic:
        return telegram_api::make_object<telegram_api::textItalic>(texts[0].get_input_rich_text(td, documents));
      case Type::Underline:
        return telegram_api::make_object<telegram_api::textUnderline>(texts[0].get_input_rich_text(td, documents));
      case Type::Strikethrough:
        return telegram_api::make_object<telegram_api::textStrike>(texts[0].get_input_rich_text(td, documents));
      case Type::Fixed:
        return telegram_api::make_object<telegram_api::textFixed>(texts[0].get_input_rich_text(td, documents));
      case Type::Url:
        return telegram_api::make_object<telegram_api::textUrl>(texts[0].get_input_rich_text(td, documents), content,
                                                                0);
      case Type::EmailAddress:
        return telegram_api::make_object<telegram_api::textEmail>(texts[0].get_input_rich_text(td, documents), content);
      case Type::Concatenation:
        return telegram_api::make_object<telegram_api::textConcat>(get_input_rich_texts(texts, td, documents));
      case Type::Subscript:
        return telegram_api::make_object<telegram_api::textSubscript>(texts[0].get_input_rich_text(td, documents));
      case Type::Superscript:
        return telegram_api::make_object<telegram_api::textSuperscript>(texts[0].get_input_rich_text(td, documents));
      case Type::Marked:
        return telegram_api::make_object<telegram_api::textMarked>(texts[0].get_input_rich_text(td, documents));
      case Type::PhoneNumber:
        return telegram_api::make_object<telegram_api::textPhone>(texts[0].get_input_rich_text(td, documents), content);
      case Type::Icon: {
        auto file_view = td->file_manager_->get_file_view(document_file_id);
        const auto *main_remote_location = file_view.get_main_remote_location();
        if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
          documents.push_back(main_remote_location->as_input_document());

          auto dimensions = to_integer<uint32>(content);
          auto width = static_cast<int32>(dimensions / 65536);
          auto height = static_cast<int32>(dimensions % 65536);
          return telegram_api::make_object<telegram_api::textImage>(main_remote_location->get_id(), width, height);
        }
        LOG(ERROR) << "Can't create textImage for " << document_file_id;
        return telegram_api::make_object<telegram_api::textEmpty>();
      }
      case Type::Anchor:
        return telegram_api::make_object<telegram_api::textAnchor>(texts[0].get_input_rich_text(td, documents),
                                                                   content);
      case Type::Math:
        return telegram_api::make_object<telegram_api::textMath>(content);
      case Type::CustomEmoji:
        return telegram_api::make_object<telegram_api::textCustomEmoji>(custom_emoji_id.get(), content);
      case Type::Spoiler:
        return telegram_api::make_object<telegram_api::textSpoiler>(texts[0].get_input_rich_text(td, documents));
      case Type::Mention:
        return texts[0].get_input_rich_text(td, documents);
      case Type::Hashtag:
        return texts[0].get_input_rich_text(td, documents);
      case Type::Cashtag:
        return texts[0].get_input_rich_text(td, documents);
      case Type::BotCommand:
        return texts[0].get_input_rich_text(td, documents);
      case Type::AutoUrl:
        return texts[0].get_input_rich_text(td, documents);
      case Type::AutoEmailAddress:
        return texts[0].get_input_rich_text(td, documents);
      case Type::AutoPhoneNumber:
        return texts[0].get_input_rich_text(td, documents);
      case Type::FormattedDate:
        return date.get_input_text_date(texts[0].get_input_rich_text(td, documents));
      case Type::BankCardNumber:
        return texts[0].get_input_rich_text(td, documents);
      case Type::MentionName:
        // input users will be added separately
        return telegram_api::make_object<telegram_api::textMentionName>(texts[0].get_input_rich_text(td, documents),
                                                                        user_id.get());
      default:
        UNREACHABLE();
        return nullptr;
    }
  }

  td_api::object_ptr<td_api::RichText> get_rich_text_object(GetWebPageBlockObjectContext *context,
                                                            bool allow_null = false) const {
    switch (type) {
      case RichText::Type::Plain:
        if (allow_null && content.empty()) {
          return nullptr;
        }
        return td_api::make_object<td_api::richTextPlain>(content);
      case RichText::Type::Bold:
        return td_api::make_object<td_api::richTextBold>(texts[0].get_rich_text_object(context));
      case RichText::Type::Italic:
        return td_api::make_object<td_api::richTextItalic>(texts[0].get_rich_text_object(context));
      case RichText::Type::Underline:
        return td_api::make_object<td_api::richTextUnderline>(texts[0].get_rich_text_object(context));
      case RichText::Type::Strikethrough:
        return td_api::make_object<td_api::richTextStrikethrough>(texts[0].get_rich_text_object(context));
      case RichText::Type::Fixed:
        return td_api::make_object<td_api::richTextFixed>(texts[0].get_rich_text_object(context));
      case RichText::Type::Url:
        if (begins_with(content, context->base_url_) && content[context->base_url_.size()] == '#') {
          if (context->is_first_pass_) {
            context->has_anchor_urls_ = true;
          } else {
            auto anchor = Slice(content).substr(context->base_url_.size() + 1);
            // https://html.spec.whatwg.org/multipage/browsing-the-web.html#the-indicated-part-of-the-document
            for (int i = 0; i < 2; i++) {
              string url_decoded_anchor;
              if (i == 1) {  // try to url_decode anchor
                url_decoded_anchor = url_decode(anchor, false);
                anchor = url_decoded_anchor;
              }
              auto it = context->anchors_.find(anchor);
              if (it != context->anchors_.end()) {
                if (it->second == nullptr) {
                  return td_api::make_object<td_api::richTextAnchorLink>(texts[0].get_rich_text_object(context),
                                                                         anchor.str(), content);
                } else {
                  return td_api::make_object<td_api::richTextReferenceLink>(texts[0].get_rich_text_object(context),
                                                                            anchor.str(), content);
                }
              }
            }
          }
        }
        if (!context->real_url_rhash_.empty() && get_url_host(content) == context->real_url_host_) {
          if (context->is_first_pass_) {
            context->has_anchor_urls_ = true;
          } else {
            return td_api::make_object<td_api::richTextUrl>(
                texts[0].get_rich_text_object(context),
                LinkManager::get_instant_view_link(content, context->real_url_rhash_), true);
          }
        }
        return td_api::make_object<td_api::richTextUrl>(texts[0].get_rich_text_object(context), content,
                                                        web_page_id.is_valid());
      case RichText::Type::EmailAddress:
        return td_api::make_object<td_api::richTextEmailAddress>(texts[0].get_rich_text_object(context), content);
      case RichText::Type::Concatenation:
        return td_api::make_object<td_api::richTexts>(get_rich_texts_object(texts, context));
      case RichText::Type::Subscript:
        return td_api::make_object<td_api::richTextSubscript>(texts[0].get_rich_text_object(context));
      case RichText::Type::Superscript:
        return td_api::make_object<td_api::richTextSuperscript>(texts[0].get_rich_text_object(context));
      case RichText::Type::Marked:
        return td_api::make_object<td_api::richTextMarked>(texts[0].get_rich_text_object(context));
      case RichText::Type::PhoneNumber:
        return td_api::make_object<td_api::richTextPhoneNumber>(texts[0].get_rich_text_object(context), content);
      case RichText::Type::Icon: {
        auto dimensions = to_integer<uint32>(content);
        auto width = static_cast<int32>(dimensions / 65536);
        auto height = static_cast<int32>(dimensions % 65536);
        return td_api::make_object<td_api::richTextIcon>(
            context->td_->documents_manager_->get_document_object(document_file_id, PhotoFormat::Jpeg), width, height);
      }
      case RichText::Type::Anchor: {
        if (context->is_first_pass_) {
          context->anchors_.emplace(Slice(content), texts[0].empty() ? nullptr : &texts[0]);
        }
        if (texts[0].empty()) {
          return td_api::make_object<td_api::richTextAnchor>(content);
        }
        return td_api::make_object<td_api::richTextReference>(content, texts[0].get_rich_text_object(context));
      }
      case RichText::Type::Math:
        return td_api::make_object<td_api::richTextMathematicalExpression>(content);
      case RichText::Type::CustomEmoji:
        return td_api::make_object<td_api::richTextCustomEmoji>(custom_emoji_id.get(), content);
      case RichText::Type::Spoiler:
        return td_api::make_object<td_api::richTextSpoiler>(texts[0].get_rich_text_object(context));
      case RichText::Type::Mention:
        return td_api::make_object<td_api::richTextMention>(texts[0].get_rich_text_object(context),
                                                            trim_first(texts[0].get_full_text(), '@'));
      case RichText::Type::Hashtag:
        return td_api::make_object<td_api::richTextHashtag>(texts[0].get_rich_text_object(context),
                                                            trim_first(texts[0].get_full_text(), '#'));
      case RichText::Type::Cashtag:
        return td_api::make_object<td_api::richTextCashtag>(texts[0].get_rich_text_object(context),
                                                            trim_first(texts[0].get_full_text(), '$'));
      case RichText::Type::BotCommand:
        if (context->skip_bot_commands_) {
          return texts[0].get_rich_text_object(context);
        }
        return td_api::make_object<td_api::richTextBotCommand>(texts[0].get_rich_text_object(context),
                                                               trim_first(texts[0].get_full_text(), '/'));
      case RichText::Type::AutoUrl:
        return td_api::make_object<td_api::richTextUrl>(texts[0].get_rich_text_object(context),
                                                        texts[0].get_full_text(), false);
      case RichText::Type::AutoEmailAddress:
        return td_api::make_object<td_api::richTextEmailAddress>(texts[0].get_rich_text_object(context),
                                                                 texts[0].get_full_text());
      case RichText::Type::AutoPhoneNumber:
        return td_api::make_object<td_api::richTextPhoneNumber>(texts[0].get_rich_text_object(context),
                                                                texts[0].get_full_text());
      case RichText::Type::FormattedDate:
        return td_api::make_object<td_api::richTextDateTime>(texts[0].get_rich_text_object(context), date.get_date(),
                                                             date.get_date_time_formatting_type_object());
      case RichText::Type::BankCardNumber:
        return td_api::make_object<td_api::richTextBankCardNumber>(texts[0].get_rich_text_object(context),
                                                                   texts[0].get_full_text());
      case RichText::Type::MentionName:
        return td_api::make_object<td_api::richTextMentionName>(
            texts[0].get_rich_text_object(context),
            context->td_->user_manager_->get_user_id_object(user_id, "richTextMentionName"));
      default:
        UNREACHABLE();
        return nullptr;
    }
  }

  friend bool operator==(const RichText &lhs, const RichText &rhs) {
    return lhs.type == rhs.type && lhs.content == rhs.content && lhs.texts == rhs.texts &&
           lhs.document_file_id == rhs.document_file_id && lhs.custom_emoji_id == rhs.custom_emoji_id &&
           lhs.web_page_id == rhs.web_page_id && lhs.date == rhs.date && lhs.user_id == rhs.user_id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(type, storer);
    store(content, storer);
    store(texts, storer);
    if (type == Type::Icon) {
      storer.context()->td().get_actor_unsafe()->documents_manager_->store_document(document_file_id, storer);
    }
    if (type == Type::Url) {
      store(web_page_id, storer);
    }
    if (type == Type::CustomEmoji) {
      store(custom_emoji_id, storer);
    }
    if (type == Type::FormattedDate) {
      store(date, storer);
    }
    if (type == Type::MentionName) {
      store(user_id, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(type, parser);
    parse(content, parser);
    parse(texts, parser);
    if (type == Type::Icon) {
      document_file_id = parser.context()->td().get_actor_unsafe()->documents_manager_->parse_document(parser);
      if (!document_file_id.is_valid()) {
        LOG(ERROR) << "Failed to load document from database";
        *this = RichText();
      }
    }
    if (type == Type::Url && parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      parse(web_page_id, parser);
    }
    if (type == Type::CustomEmoji) {
      parse(custom_emoji_id, parser);
    }
    if (type == Type::FormattedDate) {
      parse(date, parser);
    }
    if (type == Type::MentionName) {
      parse(user_id, parser);
    }
  }
};

namespace {

class WebPageBlockCaption {
 public:
  RichText text;
  RichText credit;

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const {
    text.append_file_ids(td, file_ids);
    credit.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const {
    text.add_dependencies(dependencies);
    credit.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const {
    text.for_each_rich_text(recurse_text, callback);
    credit.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const {
    return text.get_index_mask() | credit.get_index_mask();
  }

  WebPageBlockCaption clone() const {
    WebPageBlockCaption result;
    result.text = text.clone();
    result.credit = credit.clone();
    return result;
  }

  telegram_api::object_ptr<telegram_api::pageCaption> get_input_page_caption(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const {
    return telegram_api::make_object<telegram_api::pageCaption>(text.get_input_rich_text(td, documents),
                                                                credit.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::pageBlockCaption> get_page_block_caption_object(
      GetWebPageBlockObjectContext *context) const {
    if (text.empty() && credit.empty()) {
      return nullptr;
    }
    return td_api::make_object<td_api::pageBlockCaption>(text.get_rich_text_object(context),
                                                         credit.get_rich_text_object(context, true));
  }

  friend bool operator==(const WebPageBlockCaption &lhs, const WebPageBlockCaption &rhs) {
    return lhs.text == rhs.text && lhs.credit == rhs.credit;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(text, storer);
    store(credit, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(text, parser);
    if (parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      parse(credit, parser);
    } else {
      credit = RichText();
    }
  }
};

class WebPageBlockTableCell {
 public:
  RichText text;
  bool is_header = false;
  bool align_left = false;
  bool align_center = false;
  bool align_right = false;
  bool valign_top = false;
  bool valign_middle = false;
  bool valign_bottom = false;
  int32 colspan = 1;
  int32 rowspan = 1;

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const {
    text.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const {
    text.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const {
    text.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const {
    return text.get_index_mask();
  }

  telegram_api::object_ptr<telegram_api::pageTableCell> get_input_page_table_cell(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const {
    int32 flags = 0;
    if (colspan != 1) {
      flags |= telegram_api::pageTableCell::COLSPAN_MASK;
    }
    if (rowspan != 1) {
      flags |= telegram_api::pageTableCell::ROWSPAN_MASK;
    }
    if (!text.empty()) {
      flags |= telegram_api::pageTableCell::TEXT_MASK;
    }
    return telegram_api::make_object<telegram_api::pageTableCell>(
        flags, is_header, align_center, align_right, valign_middle, valign_bottom,
        text.empty() ? nullptr : text.get_input_rich_text(td, documents), colspan, rowspan);
  }

  td_api::object_ptr<td_api::pageBlockTableCell> get_page_block_table_cell_object(
      GetWebPageBlockObjectContext *context) const {
    auto align = [&]() -> td_api::object_ptr<td_api::PageBlockHorizontalAlignment> {
      if (align_left) {
        return td_api::make_object<td_api::pageBlockHorizontalAlignmentLeft>();
      }
      if (align_center) {
        return td_api::make_object<td_api::pageBlockHorizontalAlignmentCenter>();
      }
      if (align_right) {
        return td_api::make_object<td_api::pageBlockHorizontalAlignmentRight>();
      }
      UNREACHABLE();
      return nullptr;
    }();
    auto valign = [&]() -> td_api::object_ptr<td_api::PageBlockVerticalAlignment> {
      if (valign_top) {
        return td_api::make_object<td_api::pageBlockVerticalAlignmentTop>();
      }
      if (valign_middle) {
        return td_api::make_object<td_api::pageBlockVerticalAlignmentMiddle>();
      }
      if (valign_bottom) {
        return td_api::make_object<td_api::pageBlockVerticalAlignmentBottom>();
      }
      UNREACHABLE();
      return nullptr;
    }();
    return td_api::make_object<td_api::pageBlockTableCell>(text.empty() ? nullptr : text.get_rich_text_object(context),
                                                           is_header, colspan, rowspan, std::move(align),
                                                           std::move(valign));
  }

  friend bool operator==(const WebPageBlockTableCell &lhs, const WebPageBlockTableCell &rhs) {
    return lhs.text == rhs.text && lhs.is_header == rhs.is_header && lhs.align_left == rhs.align_left &&
           lhs.align_center == rhs.align_center && lhs.align_right == rhs.align_right &&
           lhs.valign_top == rhs.valign_top && lhs.valign_middle == rhs.valign_middle &&
           lhs.valign_bottom == rhs.valign_bottom && lhs.colspan == rhs.colspan && lhs.rowspan == rhs.rowspan;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_text = !text.empty();
    bool has_colspan = colspan != 1;
    bool has_rowspan = rowspan != 1;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_header);
    STORE_FLAG(align_left);
    STORE_FLAG(align_center);
    STORE_FLAG(align_right);
    STORE_FLAG(valign_top);
    STORE_FLAG(valign_middle);
    STORE_FLAG(valign_bottom);
    STORE_FLAG(has_text);
    STORE_FLAG(has_colspan);
    STORE_FLAG(has_rowspan);
    END_STORE_FLAGS();
    if (has_text) {
      store(text, storer);
    }
    if (has_colspan) {
      store(colspan, storer);
    }
    if (has_rowspan) {
      store(rowspan, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_text;
    bool has_colspan;
    bool has_rowspan;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_header);
    PARSE_FLAG(align_left);
    PARSE_FLAG(align_center);
    PARSE_FLAG(align_right);
    PARSE_FLAG(valign_top);
    PARSE_FLAG(valign_middle);
    PARSE_FLAG(valign_bottom);
    PARSE_FLAG(has_text);
    PARSE_FLAG(has_colspan);
    PARSE_FLAG(has_rowspan);
    END_PARSE_FLAGS();
    if (has_text) {
      parse(text, parser);
    }
    if (has_colspan) {
      parse(colspan, parser);
    }
    if (has_rowspan) {
      parse(rowspan, parser);
    }
  }
};

class RelatedArticle {
 public:
  string url;
  WebPageId web_page_id;
  string title;
  string description;
  Photo photo;
  string author;
  int32 published_date = 0;

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const {
    photo_append_file_ids(photo, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const {
    dependencies.add(web_page_id);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const {
  }

  td_api::object_ptr<td_api::pageBlockRelatedArticle> get_page_block_related_article_object(
      FileManager *file_manager) const {
    return td_api::make_object<td_api::pageBlockRelatedArticle>(
        url, title, description, get_photo_object(file_manager, photo), author, published_date);
  }

  friend bool operator==(const RelatedArticle &lhs, const RelatedArticle &rhs) {
    return lhs.url == rhs.url && lhs.web_page_id == rhs.web_page_id && lhs.title == rhs.title &&
           lhs.description == rhs.description && lhs.photo == rhs.photo && lhs.author == rhs.author &&
           lhs.published_date == rhs.published_date;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_title = !title.empty();
    bool has_description = !description.empty();
    bool has_photo = !photo.is_empty();
    bool has_author = !author.empty();
    bool has_date = published_date != 0;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_title);
    STORE_FLAG(has_description);
    STORE_FLAG(has_photo);
    STORE_FLAG(has_author);
    STORE_FLAG(has_date);
    END_STORE_FLAGS();
    store(url, storer);
    store(web_page_id, storer);
    if (has_title) {
      store(title, storer);
    }
    if (has_description) {
      store(description, storer);
    }
    if (has_photo) {
      store(photo, storer);
    }
    if (has_author) {
      store(author, storer);
    }
    if (has_date) {
      store(published_date, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_title;
    bool has_description;
    bool has_photo;
    bool has_author;
    bool has_date;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_title);
    PARSE_FLAG(has_description);
    PARSE_FLAG(has_photo);
    PARSE_FLAG(has_author);
    PARSE_FLAG(has_date);
    END_PARSE_FLAGS();
    parse(url, parser);
    parse(web_page_id, parser);
    if (has_title) {
      parse(title, parser);
    }
    if (has_description) {
      parse(description, parser);
    }
    if (has_photo) {
      parse(photo, parser);
    }
    if (has_author) {
      parse(author, parser);
    }
    if (has_date) {
      parse(published_date, parser);
    }
  }
};

class WebPageBlockTitle final : public WebPageBlock {
  RichText title;

 public:
  WebPageBlockTitle() = default;

  explicit WebPageBlockTitle(RichText &&title) : title(std::move(title)) {
  }

  Type get_type() const final {
    return Type::Title;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    title.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    title.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    title.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return title.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockTitle>(title.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockTitle";
    return telegram_api::make_object<telegram_api::pageBlockTitle>(title.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockTitle>(title.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockTitle &lhs, const WebPageBlockTitle &rhs) {
    return lhs.title == rhs.title;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(title, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(title, parser);
  }
};

class WebPageBlockSubtitle final : public WebPageBlock {
  RichText subtitle;

 public:
  WebPageBlockSubtitle() = default;
  explicit WebPageBlockSubtitle(RichText &&subtitle) : subtitle(std::move(subtitle)) {
  }

  Type get_type() const final {
    return Type::Subtitle;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    subtitle.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    subtitle.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    subtitle.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return subtitle.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockSubtitle>(subtitle.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockSubtitle";
    return telegram_api::make_object<telegram_api::pageBlockSubtitle>(subtitle.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockSubtitle>(subtitle.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockSubtitle &lhs, const WebPageBlockSubtitle &rhs) {
    return lhs.subtitle == rhs.subtitle;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(subtitle, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(subtitle, parser);
  }
};

class WebPageBlockAuthorDate final : public WebPageBlock {
  RichText author;
  int32 date = 0;

 public:
  WebPageBlockAuthorDate() = default;
  WebPageBlockAuthorDate(RichText &&author, int32 date) : author(std::move(author)), date(max(date, 0)) {
  }

  Type get_type() const final {
    return Type::AuthorDate;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    author.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    author.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    author.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return author.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockAuthorDate>(author.clone(), date);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockAuthorDate";
    return telegram_api::make_object<telegram_api::pageBlockAuthorDate>(author.get_input_rich_text(td, documents),
                                                                        date);
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockAuthorDate>(author.get_rich_text_object(context), date);
  }

  friend bool operator==(const WebPageBlockAuthorDate &lhs, const WebPageBlockAuthorDate &rhs) {
    return lhs.author == rhs.author && lhs.date == rhs.date;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(author, storer);
    store(date, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(author, parser);
    parse(date, parser);
  }
};

class WebPageBlockHeader final : public WebPageBlock {
  RichText header;

 public:
  WebPageBlockHeader() = default;
  explicit WebPageBlockHeader(RichText &&header) : header(std::move(header)) {
  }

  Type get_type() const final {
    return Type::Header;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    header.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    header.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    header.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return header.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockHeader>(header.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockHeader";
    return telegram_api::make_object<telegram_api::pageBlockHeader>(header.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockHeader>(header.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockHeader &lhs, const WebPageBlockHeader &rhs) {
    return lhs.header == rhs.header;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(header, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(header, parser);
  }
};

class WebPageBlockSubheader final : public WebPageBlock {
  RichText subheader;

 public:
  WebPageBlockSubheader() = default;
  explicit WebPageBlockSubheader(RichText &&subheader) : subheader(std::move(subheader)) {
  }

  Type get_type() const final {
    return Type::Subheader;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    subheader.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    subheader.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    subheader.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return subheader.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockSubheader>(subheader.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockSubheader";
    return telegram_api::make_object<telegram_api::pageBlockSubheader>(subheader.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockSubheader>(subheader.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockSubheader &lhs, const WebPageBlockSubheader &rhs) {
    return lhs.subheader == rhs.subheader;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(subheader, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(subheader, parser);
  }
};

class WebPageBlockHeading final : public WebPageBlock {
  RichText text;
  int32 size = 4;

 public:
  WebPageBlockHeading() = default;
  WebPageBlockHeading(RichText &&text, int32 size) : text(std::move(text)), size(size) {
  }

  Type get_type() const final {
    return Type::Heading;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    text.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    text.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    text.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return text.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockHeading>(text.clone(), size);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto input_rich_text = text.get_input_rich_text(td, documents);
    switch (size) {
      case 1:
        return telegram_api::make_object<telegram_api::pageBlockHeading1>(std::move(input_rich_text));
      case 2:
        return telegram_api::make_object<telegram_api::pageBlockHeading2>(std::move(input_rich_text));
      case 3:
        return telegram_api::make_object<telegram_api::pageBlockHeading3>(std::move(input_rich_text));
      case 4:
        return telegram_api::make_object<telegram_api::pageBlockHeading4>(std::move(input_rich_text));
      case 5:
        return telegram_api::make_object<telegram_api::pageBlockHeading5>(std::move(input_rich_text));
      case 6:
        return telegram_api::make_object<telegram_api::pageBlockHeading6>(std::move(input_rich_text));
      default:
        UNREACHABLE();
        return nullptr;
    }
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockSectionHeading>(text.get_rich_text_object(context), size);
  }

  friend bool operator==(const WebPageBlockHeading &lhs, const WebPageBlockHeading &rhs) {
    return lhs.text == rhs.text && lhs.size == rhs.size;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    END_STORE_FLAGS();
    store(text, storer);
    store(size, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    END_PARSE_FLAGS();
    parse(text, parser);
    parse(size, parser);
    size = clamp(size, 1, 6);
  }
};

class WebPageBlockKicker final : public WebPageBlock {
  RichText kicker;

 public:
  WebPageBlockKicker() = default;
  explicit WebPageBlockKicker(RichText &&kicker) : kicker(std::move(kicker)) {
  }

  Type get_type() const final {
    return Type::Kicker;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    kicker.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    kicker.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    kicker.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return kicker.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockKicker>(kicker.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockKicker";
    return telegram_api::make_object<telegram_api::pageBlockKicker>(kicker.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockKicker>(kicker.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockKicker &lhs, const WebPageBlockKicker &rhs) {
    return lhs.kicker == rhs.kicker;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(kicker, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(kicker, parser);
  }
};

class WebPageBlockParagraph final : public WebPageBlock {
  RichText text;

 public:
  WebPageBlockParagraph() = default;
  explicit WebPageBlockParagraph(RichText &&text) : text(std::move(text)) {
  }

  Type get_type() const final {
    return Type::Paragraph;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    text.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    text.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    text.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return text.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockParagraph>(text.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockParagraph>(text.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockParagraph>(text.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockParagraph &lhs, const WebPageBlockParagraph &rhs) {
    return lhs.text == rhs.text;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(text, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(text, parser);
  }
};

class WebPageBlockPreformatted final : public WebPageBlock {
  RichText text;
  string language;

 public:
  WebPageBlockPreformatted() = default;
  WebPageBlockPreformatted(RichText &&text, string &&language) : text(std::move(text)), language(std::move(language)) {
  }

  Type get_type() const final {
    return Type::Preformatted;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    text.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    text.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    text.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return text.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockPreformatted>(text.clone(), string(language));
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockPreformatted>(text.get_input_rich_text(td, documents),
                                                                          language);
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockPreformatted>(text.get_rich_text_object(context), language);
  }

  friend bool operator==(const WebPageBlockPreformatted &lhs, const WebPageBlockPreformatted &rhs) {
    return lhs.text == rhs.text && lhs.language == rhs.language;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(text, storer);
    store(language, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(text, parser);
    parse(language, parser);
  }
};

class WebPageBlockFooter final : public WebPageBlock {
  RichText footer;

 public:
  WebPageBlockFooter() = default;
  explicit WebPageBlockFooter(RichText &&footer) : footer(std::move(footer)) {
  }

  Type get_type() const final {
    return Type::Footer;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    footer.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    footer.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    footer.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return footer.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockFooter>(footer.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockFooter>(footer.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockFooter>(footer.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockFooter &lhs, const WebPageBlockFooter &rhs) {
    return lhs.footer == rhs.footer;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(footer, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(footer, parser);
  }
};

class WebPageBlockThinking final : public WebPageBlock {
  RichText text;

 public:
  WebPageBlockThinking() = default;
  explicit WebPageBlockThinking(RichText &&text) : text(std::move(text)) {
  }

  Type get_type() const final {
    return Type::Thinking;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    text.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    text.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    text.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return text.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockThinking>(text.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockThinking>(text.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockThinking>(text.get_rich_text_object(context));
  }

  friend bool operator==(const WebPageBlockThinking &lhs, const WebPageBlockThinking &rhs) {
    return lhs.text == rhs.text;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    END_STORE_FLAGS();
    store(text, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    END_PARSE_FLAGS();
    parse(text, parser);
  }
};

class WebPageBlockDivider final : public WebPageBlock {
 public:
  Type get_type() const final {
    return Type::Divider;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
  }

  void add_dependencies(Dependencies &dependencies) const final {
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
  }

  int32 get_index_mask() const final {
    return 0;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockDivider>();
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockDivider>();
  }

  friend bool operator==(const WebPageBlockDivider &lhs, const WebPageBlockDivider &rhs) {
    return true;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
  }

  template <class ParserT>
  void parse(ParserT &parser) {
  }
};

class WebPageBlockMath final : public WebPageBlock {
  string source;

 public:
  WebPageBlockMath() = default;
  explicit WebPageBlockMath(string &&source) : source(std::move(source)) {
  }

  Type get_type() const final {
    return Type::Math;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
  }

  void add_dependencies(Dependencies &dependencies) const final {
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
  }

  int32 get_index_mask() const final {
    return 0;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockMath>(string(source));
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockMath>(source);
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockMathematicalExpression>(source);
  }

  friend bool operator==(const WebPageBlockMath &lhs, const WebPageBlockMath &rhs) {
    return lhs.source == rhs.source;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    END_STORE_FLAGS();
    store(source, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    END_PARSE_FLAGS();
    parse(source, parser);
  }
};

class WebPageBlockAnchor final : public WebPageBlock {
  string name;

 public:
  WebPageBlockAnchor() = default;
  explicit WebPageBlockAnchor(string &&name) : name(std::move(name)) {
  }

  Type get_type() const final {
    return Type::Anchor;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
  }

  void add_dependencies(Dependencies &dependencies) const final {
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
  }

  int32 get_index_mask() const final {
    return 0;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockAnchor>(string(name));
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockAnchor>(name);
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    if (context->is_first_pass_) {
      context->anchors_.emplace(name, nullptr);
    }
    return td_api::make_object<td_api::pageBlockAnchor>(name);
  }

  friend bool operator==(const WebPageBlockAnchor &lhs, const WebPageBlockAnchor &rhs) {
    return lhs.name == rhs.name;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(name, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(name, parser);
  }
};

class WebPageBlockList final : public WebPageBlock {
 public:
  struct Item {
    string label;
    vector<unique_ptr<WebPageBlock>> page_blocks;
    bool has_checkbox = false;
    bool is_checked = false;
    int32 value = -1;
    string type;

    void append_file_ids(const Td *td, vector<FileId> &file_ids) const {
      for (const auto &page_block : page_blocks) {
        page_block->append_file_ids(td, file_ids);
      }
    }

    void add_dependencies(Dependencies &dependencies) const {
      for (const auto &page_block : page_blocks) {
        page_block->add_dependencies(dependencies);
      }
    }

    void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const {
      for (const auto &page_block : page_blocks) {
        page_block->for_each_rich_text(recurse_text, callback);
      }
    }

    bool can_send(const RestrictedRights &rights) const {
      for (const auto &page_block : page_blocks) {
        if (!page_block->can_send(rights)) {
          return false;
        }
      }
      return true;
    }

    int32 get_index_mask() const {
      return get_web_page_blocks_index_mask(page_blocks);
    }

    Item clone() const {
      Item result;
      result.label = label;
      result.page_blocks = clone_web_page_blocks(page_blocks);
      result.has_checkbox = has_checkbox;
      result.is_checked = is_checked;
      result.value = value;
      result.type = type;
      return result;
    }

    telegram_api::object_ptr<telegram_api::PageListItem> get_input_page_list_item(
        const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
        vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const {
      CHECK(label.empty());
      return telegram_api::make_object<telegram_api::pageListItemBlocks>(
          0, has_checkbox, has_checkbox && is_checked, get_input_page_blocks(page_blocks, td, photos, documents));
    }

    telegram_api::object_ptr<telegram_api::PageListOrderedItem> get_input_page_list_ordered_item(
        const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
        vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const {
      CHECK(!label.empty());
      int32 flags = telegram_api::pageListOrderedItemBlocks::NUM_MASK |
                    telegram_api::pageListOrderedItemBlocks::VALUE_MASK |
                    telegram_api::pageListOrderedItemBlocks::TYPE_MASK;
      return telegram_api::make_object<telegram_api::pageListOrderedItemBlocks>(
          flags, has_checkbox, has_checkbox && is_checked, label,
          get_input_page_blocks(page_blocks, td, photos, documents), value, type);
    }

    td_api::object_ptr<td_api::pageBlockListItem> get_page_block_list_item_object(Context *context) const {
      if (label.empty()) {
        // if label is empty, then Bullet U+2022 is used as a label
        return td_api::make_object<td_api::pageBlockListItem>("\xE2\x80\xA2",
                                                              get_page_blocks_object(page_blocks, context),
                                                              has_checkbox, has_checkbox && is_checked, 0, string());
      }
      return td_api::make_object<td_api::pageBlockListItem>(label, get_page_blocks_object(page_blocks, context),
                                                            has_checkbox, has_checkbox && is_checked, value, type);
    }

    friend bool operator==(const Item &lhs, const Item &rhs) {
      return lhs.label == rhs.label && lhs.page_blocks == rhs.page_blocks && lhs.has_checkbox == rhs.has_checkbox &&
             lhs.is_checked == rhs.is_checked && lhs.value == rhs.value && lhs.type == rhs.type;
    }

    template <class StorerT>
    void store(StorerT &storer) const {
      using ::td::store;
      bool has_value = value != -1;
      bool has_type = !type.empty();
      BEGIN_STORE_FLAGS();
      STORE_FLAG(has_checkbox);
      STORE_FLAG(is_checked);
      STORE_FLAG(has_value);
      STORE_FLAG(has_type);
      END_STORE_FLAGS();
      store(label, storer);
      store(page_blocks, storer);
      if (has_value) {
        store(value, storer);
      }
      if (has_type) {
        store(type, storer);
      }
    }

    template <class ParserT>
    void parse(ParserT &parser) {
      using ::td::parse;
      bool has_value = false;
      bool has_type = false;
      if (parser.version() >= static_cast<int32>(Version::SupportRichMessages)) {
        BEGIN_PARSE_FLAGS();
        PARSE_FLAG(has_checkbox);
        PARSE_FLAG(is_checked);
        PARSE_FLAG(has_value);
        PARSE_FLAG(has_type);
        END_PARSE_FLAGS();
      }
      parse(label, parser);
      parse(page_blocks, parser);
      if (has_value) {
        parse(value, parser);
      }
      if (has_type) {
        parse(type, parser);
      }
    }
  };

 private:
  vector<Item> items;
  int32 start = 0;
  bool is_reversed = false;
  string type;

 public:
  WebPageBlockList() = default;
  WebPageBlockList(vector<Item> &&items, int32 start, bool is_reversed, const string &type)
      : items(std::move(items)), start(start), is_reversed(is_reversed), type(type) {
  }

  Type get_type() const final {
    return Type::List;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    for (auto &item : items) {
      item.append_file_ids(td, file_ids);
    }
  }

  void add_dependencies(Dependencies &dependencies) const final {
    for (auto &item : items) {
      item.add_dependencies(dependencies);
    }
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    for (auto &item : items) {
      item.for_each_rich_text(recurse_text, callback);
    }
  }

  bool can_send(const RestrictedRights &rights) const final {
    for (auto &item : items) {
      if (!item.can_send(rights)) {
        return false;
      }
    }
    return true;
  }

  int32 get_index_mask() const final {
    int32 index_mask = 0;
    for (const auto &item : items) {
      index_mask |= item.get_index_mask();
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    auto new_items = transform(items, [](const Item &item) { return item.clone(); });
    return td::make_unique<WebPageBlockList>(std::move(new_items), start, is_reversed, type);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    if (!items.empty() && !items[0].label.empty()) {
      int32 flags = telegram_api::pageBlockOrderedList::START_MASK | telegram_api::pageBlockOrderedList::TYPE_MASK;
      return telegram_api::make_object<telegram_api::pageBlockOrderedList>(
          flags, is_reversed,
          transform(items,
                    [&](const Item &item) { return item.get_input_page_list_ordered_item(td, photos, documents); }),
          start, type);
    } else {
      return telegram_api::make_object<telegram_api::pageBlockList>(
          transform(items, [&](const Item &item) { return item.get_input_page_list_item(td, photos, documents); }));
    }
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockList>(
        transform(items, [context](const Item &item) { return item.get_page_block_list_item_object(context); }));
  }

  friend bool operator==(const WebPageBlockList &lhs, const WebPageBlockList &rhs) {
    return lhs.items == rhs.items && lhs.start == rhs.start && lhs.is_reversed == rhs.is_reversed &&
           lhs.type == rhs.type;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_start = start != 0;
    bool has_type = !type.empty();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_reversed);
    STORE_FLAG(has_start);
    STORE_FLAG(has_type);
    END_STORE_FLAGS();
    store(items, storer);
    if (has_start) {
      store(start, storer);
    }
    if (has_type) {
      store(type, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;

    if (parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      bool has_start = false;
      bool has_type = false;
      if (parser.version() >= static_cast<int32>(Version::SupportRichMessages)) {
        BEGIN_PARSE_FLAGS();
        PARSE_FLAG(is_reversed);
        PARSE_FLAG(has_start);
        PARSE_FLAG(has_type);
        END_PARSE_FLAGS();
      }
      parse(items, parser);
      if (has_start) {
        parse(start, parser);
      }
      if (has_type) {
        parse(type, parser);
      }
    } else {
      vector<RichText> text_items;
      bool is_ordered;

      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(is_ordered);
      END_PARSE_FLAGS();

      parse(text_items, parser);

      int pos = 0;
      items.reserve(text_items.size());
      for (auto &text_item : text_items) {
        Item item;
        if (is_ordered) {
          pos++;
          item.label = (PSTRING() << pos << '.');
        }
        item.page_blocks.push_back(make_unique<WebPageBlockParagraph>(std::move(text_item)));
        items.push_back(std::move(item));
      }
    }
  }
};

class WebPageBlockBlockQuote final : public WebPageBlock {
  RichText text;
  RichText credit;

 public:
  WebPageBlockBlockQuote() = default;
  WebPageBlockBlockQuote(RichText &&text, RichText &&credit) : text(std::move(text)), credit(std::move(credit)) {
  }

  Type get_type() const final {
    return Type::BlockQuote;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    text.append_file_ids(td, file_ids);
    credit.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    text.add_dependencies(dependencies);
    credit.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    text.for_each_rich_text(recurse_text, callback);
    credit.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return text.get_index_mask() | credit.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockBlockQuote>(text.clone(), credit.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockBlockquote>(text.get_input_rich_text(td, documents),
                                                                        credit.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    vector<td_api::object_ptr<td_api::PageBlock>> blocks;
    blocks.push_back(td_api::make_object<td_api::pageBlockParagraph>(text.get_rich_text_object(context)));
    return td_api::make_object<td_api::pageBlockBlockQuote>(std::move(blocks),
                                                            credit.get_rich_text_object(context, true));
  }

  friend bool operator==(const WebPageBlockBlockQuote &lhs, const WebPageBlockBlockQuote &rhs) {
    return lhs.text == rhs.text && lhs.credit == rhs.credit;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(text, storer);
    store(credit, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(text, parser);
    parse(credit, parser);
  }
};

class WebPageBlockPullQuote final : public WebPageBlock {
  RichText text;
  RichText credit;

 public:
  WebPageBlockPullQuote() = default;
  WebPageBlockPullQuote(RichText &&text, RichText &&credit) : text(std::move(text)), credit(std::move(credit)) {
  }

  Type get_type() const final {
    return Type::PullQuote;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    text.append_file_ids(td, file_ids);
    credit.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    text.add_dependencies(dependencies);
    credit.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    text.for_each_rich_text(recurse_text, callback);
    credit.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return text.get_index_mask() | credit.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockPullQuote>(text.clone(), credit.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockPullquote>(text.get_input_rich_text(td, documents),
                                                                       credit.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockPullQuote>(text.get_rich_text_object(context),
                                                           credit.get_rich_text_object(context, true));
  }

  friend bool operator==(const WebPageBlockPullQuote &lhs, const WebPageBlockPullQuote &rhs) {
    return lhs.text == rhs.text && lhs.credit == rhs.credit;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(text, storer);
    store(credit, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(text, parser);
    parse(credit, parser);
  }
};

class WebPageBlockAnimation final : public WebPageBlock {
  FileId animation_file_id;
  WebPageBlockCaption caption;
  bool need_autoplay = false;
  bool has_spoiler = false;

 public:
  WebPageBlockAnimation() = default;
  WebPageBlockAnimation(FileId animation_file_id, WebPageBlockCaption &&caption, bool need_autoplay, bool has_spoiler)
      : animation_file_id(animation_file_id)
      , caption(std::move(caption))
      , need_autoplay(need_autoplay)
      , has_spoiler(has_spoiler) {
  }

  Type get_type() const final {
    return Type::Animation;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    caption.append_file_ids(td, file_ids);
    Document(Document::Type::Animation, animation_file_id).append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return rights.can_send_animations();
  }

  int32 get_index_mask() const final {
    int32 index_mask = caption.get_index_mask();
    if (animation_file_id.is_valid()) {
      return index_mask | message_search_filter_index_mask(MessageSearchFilter::Animation);
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockAnimation>(animation_file_id, caption.clone(), need_autoplay, has_spoiler);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto file_view = td->file_manager_->get_file_view(animation_file_id);
    const auto *main_remote_location = file_view.get_main_remote_location();
    if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
      documents.push_back(main_remote_location->as_input_document());
      return telegram_api::make_object<telegram_api::pageBlockVideo>(0, need_autoplay, true, has_spoiler,
                                                                     main_remote_location->get_id(),
                                                                     caption.get_input_page_caption(td, documents));
    }
    LOG(ERROR) << "Can't create pageBlockVideo for " << animation_file_id;
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockAnimation>(
        context->td_->animations_manager_->get_animation_object(animation_file_id),
        caption.get_page_block_caption_object(context), need_autoplay, has_spoiler);
  }

  friend bool operator==(const WebPageBlockAnimation &lhs, const WebPageBlockAnimation &rhs) {
    return lhs.animation_file_id == rhs.animation_file_id && lhs.caption == rhs.caption &&
           lhs.need_autoplay == rhs.need_autoplay && lhs.has_spoiler == rhs.has_spoiler;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;

    bool has_empty_animation = !animation_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(need_autoplay);
    STORE_FLAG(has_empty_animation);
    STORE_FLAG(has_spoiler);
    END_STORE_FLAGS();

    if (!has_empty_animation) {
      storer.context()->td().get_actor_unsafe()->animations_manager_->store_animation(animation_file_id, storer);
    }
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;

    bool has_empty_animation;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(need_autoplay);
    PARSE_FLAG(has_empty_animation);
    PARSE_FLAG(has_spoiler);
    END_PARSE_FLAGS();

    if (parser.version() >= static_cast<int32>(Version::FixWebPageInstantViewDatabase)) {
      if (!has_empty_animation) {
        animation_file_id = parser.context()->td().get_actor_unsafe()->animations_manager_->parse_animation(parser);
      } else {
        animation_file_id = FileId();
      }
    } else {
      animation_file_id = FileId();
      parser.set_error("Wrong stored object");
    }
    parse(caption, parser);
  }
};

class WebPageBlockPhoto final : public WebPageBlock {
  Photo photo;
  WebPageBlockCaption caption;
  string url;
  WebPageId web_page_id;
  bool has_spoiler = false;

 public:
  WebPageBlockPhoto() = default;
  WebPageBlockPhoto(Photo &&photo, WebPageBlockCaption &&caption, string &&url, WebPageId web_page_id, bool has_spoiler)
      : photo(std::move(photo))
      , caption(std::move(caption))
      , url(std::move(url))
      , web_page_id(web_page_id)
      , has_spoiler(has_spoiler) {
  }

  Type get_type() const final {
    return Type::Photo;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    photo_append_file_ids(photo, file_ids);
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
    dependencies.add(web_page_id);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return rights.can_send_photos();
  }

  int32 get_index_mask() const final {
    int32 index_mask = caption.get_index_mask();
    if (!photo.is_empty()) {
      return index_mask | message_search_filter_index_mask(MessageSearchFilter::Photo) |
             message_search_filter_index_mask(MessageSearchFilter::PhotoAndVideo);
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockPhoto>(Photo(photo), caption.clone(), string(url), web_page_id, has_spoiler);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto file_view = td->file_manager_->get_file_view(get_photo_any_file_id(photo));
    const auto *main_remote_location = file_view.get_main_remote_location();
    if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
      photos.push_back(main_remote_location->as_input_photo());
      return telegram_api::make_object<telegram_api::pageBlockPhoto>(
          0, has_spoiler, main_remote_location->get_id(), caption.get_input_page_caption(td, documents), string(), 0);
    }
    LOG(ERROR) << "Can't create pageBlockPhoto for " << photo;
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockPhoto>(get_photo_object(context->td_->file_manager_.get(), photo),
                                                       caption.get_page_block_caption_object(context), url,
                                                       has_spoiler);
  }

  friend bool operator==(const WebPageBlockPhoto &lhs, const WebPageBlockPhoto &rhs) {
    return lhs.photo == rhs.photo && lhs.caption == rhs.caption && lhs.url == rhs.url &&
           lhs.web_page_id == rhs.web_page_id && lhs.has_spoiler == rhs.has_spoiler;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_spoiler);
    END_STORE_FLAGS();
    store(photo, storer);
    store(caption, storer);
    store(url, storer);
    store(web_page_id, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    if (parser.version() >= static_cast<int32>(Version::SupportRichMessages)) {
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(has_spoiler);
      END_PARSE_FLAGS();
    }
    parse(photo, parser);
    parse(caption, parser);
    if (parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
      parse(url, parser);
      parse(web_page_id, parser);
    } else {
      url.clear();
      web_page_id = WebPageId();
    }
  }
};

class WebPageBlockVideo final : public WebPageBlock {
  FileId video_file_id;
  WebPageBlockCaption caption;
  bool need_autoplay = false;
  bool is_looped = false;
  bool has_spoiler = false;

 public:
  WebPageBlockVideo() = default;
  WebPageBlockVideo(FileId video_file_id, WebPageBlockCaption &&caption, bool need_autoplay, bool is_looped,
                    bool has_spoiler)
      : video_file_id(video_file_id)
      , caption(std::move(caption))
      , need_autoplay(need_autoplay)
      , is_looped(is_looped)
      , has_spoiler(has_spoiler) {
  }

  Type get_type() const final {
    return Type::Video;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    caption.append_file_ids(td, file_ids);
    Document(Document::Type::Video, video_file_id).append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return rights.can_send_videos();
  }

  int32 get_index_mask() const final {
    int32 index_mask = caption.get_index_mask();
    if (video_file_id.is_valid()) {
      return index_mask | message_search_filter_index_mask(MessageSearchFilter::Video) |
             message_search_filter_index_mask(MessageSearchFilter::PhotoAndVideo);
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockVideo>(video_file_id, caption.clone(), need_autoplay, is_looped, has_spoiler);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto file_view = td->file_manager_->get_file_view(video_file_id);
    const auto *main_remote_location = file_view.get_main_remote_location();
    if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
      documents.push_back(main_remote_location->as_input_document());
      return telegram_api::make_object<telegram_api::pageBlockVideo>(0, need_autoplay, is_looped, has_spoiler,
                                                                     main_remote_location->get_id(),
                                                                     caption.get_input_page_caption(td, documents));
    }
    LOG(ERROR) << "Can't create pageBlockVideo for " << video_file_id;
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockVideo>(context->td_->videos_manager_->get_video_object(video_file_id),
                                                       caption.get_page_block_caption_object(context), need_autoplay,
                                                       is_looped, has_spoiler);
  }

  friend bool operator==(const WebPageBlockVideo &lhs, const WebPageBlockVideo &rhs) {
    return lhs.video_file_id == rhs.video_file_id && lhs.caption == rhs.caption &&
           lhs.need_autoplay == rhs.need_autoplay && lhs.is_looped == rhs.is_looped &&
           lhs.has_spoiler == rhs.has_spoiler;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;

    bool has_empty_video = !video_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(need_autoplay);
    STORE_FLAG(is_looped);
    STORE_FLAG(has_empty_video);
    STORE_FLAG(has_spoiler);
    END_STORE_FLAGS();

    if (!has_empty_video) {
      storer.context()->td().get_actor_unsafe()->videos_manager_->store_video(video_file_id, storer);
    }
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;

    bool has_empty_video;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(need_autoplay);
    PARSE_FLAG(is_looped);
    PARSE_FLAG(has_empty_video);
    PARSE_FLAG(has_spoiler);
    END_PARSE_FLAGS();

    if (parser.version() >= static_cast<int32>(Version::FixWebPageInstantViewDatabase)) {
      if (!has_empty_video) {
        video_file_id = parser.context()->td().get_actor_unsafe()->videos_manager_->parse_video(parser);
      } else {
        video_file_id = FileId();
      }
    } else {
      video_file_id = FileId();
      parser.set_error("Wrong stored object");
    }
    parse(caption, parser);
  }
};

class WebPageBlockCover final : public WebPageBlock {
  unique_ptr<WebPageBlock> cover;

 public:
  WebPageBlockCover() = default;
  explicit WebPageBlockCover(unique_ptr<WebPageBlock> &&cover) : cover(std::move(cover)) {
  }

  Type get_type() const final {
    return Type::Cover;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    cover->append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    cover->add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    cover->for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return cover->can_send(rights);
  }

  int32 get_index_mask() const final {
    return cover->get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockCover>(cover->clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockCover";
    return telegram_api::make_object<telegram_api::pageBlockCover>(cover->get_input_page_block(td, photos, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockCover>(cover->get_page_block_object(context));
  }

  friend bool operator==(const WebPageBlockCover &lhs, const WebPageBlockCover &rhs) {
    return lhs.cover == rhs.cover;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(cover, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(cover, parser);
  }
};

class WebPageBlockEmbedded final : public WebPageBlock {
  string url;
  string html;
  Photo poster_photo;
  Dimensions dimensions;
  WebPageBlockCaption caption;
  bool is_full_width = false;
  bool allow_scrolling = false;

 public:
  WebPageBlockEmbedded() = default;
  WebPageBlockEmbedded(string &&url, string &&html, Photo &&poster_photo, Dimensions dimensions,
                       WebPageBlockCaption &&caption, bool is_full_width, bool allow_scrolling)
      : url(std::move(url))
      , html(std::move(html))
      , poster_photo(std::move(poster_photo))
      , dimensions(dimensions)
      , caption(std::move(caption))
      , is_full_width(is_full_width)
      , allow_scrolling(allow_scrolling) {
  }

  Type get_type() const final {
    return Type::Embedded;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    photo_append_file_ids(poster_photo, file_ids);
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return poster_photo.is_empty() || rights.can_send_photos();
  }

  int32 get_index_mask() const final {
    return caption.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockEmbedded>(string(url), string(html), Photo(poster_photo), dimensions,
                                                 caption.clone(), is_full_width, allow_scrolling);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockEmbedded";
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockEmbedded>(
        url, html, get_photo_object(context->td_->file_manager_.get(), poster_photo), dimensions.width,
        dimensions.height, caption.get_page_block_caption_object(context), is_full_width, allow_scrolling);
  }

  friend bool operator==(const WebPageBlockEmbedded &lhs, const WebPageBlockEmbedded &rhs) {
    return lhs.url == rhs.url && lhs.html == rhs.html && lhs.poster_photo == rhs.poster_photo &&
           lhs.dimensions == rhs.dimensions && lhs.caption == rhs.caption && lhs.is_full_width == rhs.is_full_width &&
           lhs.allow_scrolling == rhs.allow_scrolling;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_full_width);
    STORE_FLAG(allow_scrolling);
    END_STORE_FLAGS();

    store(url, storer);
    store(html, storer);
    store(poster_photo, storer);
    store(dimensions, storer);
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_full_width);
    PARSE_FLAG(allow_scrolling);
    END_PARSE_FLAGS();

    parse(url, parser);
    parse(html, parser);
    parse(poster_photo, parser);
    parse(dimensions, parser);
    parse(caption, parser);
  }
};

class WebPageBlockEmbeddedPost final : public WebPageBlock {
  string url;
  string author;
  Photo author_photo;
  int32 date = 0;
  vector<unique_ptr<WebPageBlock>> page_blocks;
  WebPageBlockCaption caption;

 public:
  WebPageBlockEmbeddedPost() = default;
  WebPageBlockEmbeddedPost(string &&url, string &&author, Photo &&author_photo, int32 date,
                           vector<unique_ptr<WebPageBlock>> &&page_blocks, WebPageBlockCaption &&caption)
      : url(std::move(url))
      , author(std::move(author))
      , author_photo(std::move(author_photo))
      , date(max(date, 0))
      , page_blocks(std::move(page_blocks))
      , caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::EmbeddedPost;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    photo_append_file_ids(author_photo, file_ids);
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(td, file_ids);
    }
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    for (auto &page_block : page_blocks) {
      page_block->add_dependencies(dependencies);
    }
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    for (auto &page_block : page_blocks) {
      page_block->for_each_rich_text(recurse_text, callback);
    }
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return author_photo.is_empty() || rights.can_send_photos();
  }

  int32 get_index_mask() const final {
    return caption.get_index_mask() | get_web_page_blocks_index_mask(page_blocks);
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockEmbeddedPost>(string(url), string(author), Photo(author_photo), date,
                                                     clone_web_page_blocks(page_blocks), caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockEmbeddedPost";
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockEmbeddedPost>(
        url, author, get_photo_object(context->td_->file_manager_.get(), author_photo), date,
        get_page_blocks_object(page_blocks, context), caption.get_page_block_caption_object(context));
  }

  friend bool operator==(const WebPageBlockEmbeddedPost &lhs, const WebPageBlockEmbeddedPost &rhs) {
    return lhs.url == rhs.url && lhs.author == rhs.author && lhs.author_photo == rhs.author_photo &&
           lhs.date == rhs.date && lhs.page_blocks == rhs.page_blocks && lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(url, storer);
    store(author, storer);
    store(author_photo, storer);
    store(date, storer);
    store(page_blocks, storer);
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(url, parser);
    parse(author, parser);
    parse(author_photo, parser);
    parse(date, parser);
    parse(page_blocks, parser);
    parse(caption, parser);
  }
};

class WebPageBlockCollage final : public WebPageBlock {
  vector<unique_ptr<WebPageBlock>> page_blocks;
  WebPageBlockCaption caption;

 public:
  WebPageBlockCollage() = default;
  WebPageBlockCollage(vector<unique_ptr<WebPageBlock>> &&page_blocks, WebPageBlockCaption &&caption)
      : page_blocks(std::move(page_blocks)), caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::Collage;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(td, file_ids);
    }
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    for (auto &page_block : page_blocks) {
      page_block->add_dependencies(dependencies);
    }
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    for (auto &page_block : page_blocks) {
      page_block->for_each_rich_text(recurse_text, callback);
    }
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    for (const auto &page_block : page_blocks) {
      if (!page_block->can_send(rights)) {
        return false;
      }
    }
    return true;
  }

  int32 get_index_mask() const final {
    return get_web_page_blocks_index_mask(page_blocks) | caption.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockCollage>(clone_web_page_blocks(page_blocks), caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockCollage>(
        get_input_page_blocks(page_blocks, td, photos, documents), caption.get_input_page_caption(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockCollage>(get_page_blocks_object(page_blocks, context),
                                                         caption.get_page_block_caption_object(context));
  }

  friend bool operator==(const WebPageBlockCollage &lhs, const WebPageBlockCollage &rhs) {
    return lhs.page_blocks == rhs.page_blocks && lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(page_blocks, storer);
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(page_blocks, parser);
    parse(caption, parser);
  }
};

class WebPageBlockSlideshow final : public WebPageBlock {
  vector<unique_ptr<WebPageBlock>> page_blocks;
  WebPageBlockCaption caption;

 public:
  WebPageBlockSlideshow() = default;
  WebPageBlockSlideshow(vector<unique_ptr<WebPageBlock>> &&page_blocks, WebPageBlockCaption &&caption)
      : page_blocks(std::move(page_blocks)), caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::Slideshow;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(td, file_ids);
    }
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    for (auto &page_block : page_blocks) {
      page_block->add_dependencies(dependencies);
    }
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    for (auto &page_block : page_blocks) {
      page_block->for_each_rich_text(recurse_text, callback);
    }
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    for (const auto &page_block : page_blocks) {
      if (!page_block->can_send(rights)) {
        return false;
      }
    }
    return true;
  }

  int32 get_index_mask() const final {
    return get_web_page_blocks_index_mask(page_blocks) | caption.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockSlideshow>(clone_web_page_blocks(page_blocks), caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockSlideshow>(
        get_input_page_blocks(page_blocks, td, photos, documents), caption.get_input_page_caption(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockSlideshow>(get_page_blocks_object(page_blocks, context),
                                                           caption.get_page_block_caption_object(context));
  }

  friend bool operator==(const WebPageBlockSlideshow &lhs, const WebPageBlockSlideshow &rhs) {
    return lhs.page_blocks == rhs.page_blocks && lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(page_blocks, storer);
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(page_blocks, parser);
    parse(caption, parser);
  }
};

class WebPageBlockChatLink final : public WebPageBlock {
  string title;
  DialogPhoto photo;
  string username;
  AccentColorId accent_color_id;
  ChannelId channel_id;  // the channel itself may be unknown

 public:
  WebPageBlockChatLink() = default;
  WebPageBlockChatLink(string &&title, DialogPhoto photo, string &&username, AccentColorId accent_color_id,
                       ChannelId channel_id)
      : title(std::move(title))
      , photo(std::move(photo))
      , username(std::move(username))
      , accent_color_id(accent_color_id)
      , channel_id(channel_id) {
  }

  Type get_type() const final {
    return Type::ChatLink;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    append(file_ids, dialog_photo_get_file_ids(photo));
  }

  void add_dependencies(Dependencies &dependencies) const final {
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
  }

  int32 get_index_mask() const final {
    return 0;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockChatLink>(string(title), photo, string(username), accent_color_id, channel_id);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockChatLink";
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockChatLink>(
        title, get_chat_photo_info_object(context->td_->file_manager_.get(), &photo),
        context->td_->theme_manager_->get_accent_color_id_object(accent_color_id, AccentColorId(channel_id)), username);
  }

  friend bool operator==(const WebPageBlockChatLink &lhs, const WebPageBlockChatLink &rhs) {
    return lhs.title == rhs.title && lhs.photo == rhs.photo && lhs.username == rhs.username &&
           lhs.accent_color_id == rhs.accent_color_id && lhs.channel_id == rhs.channel_id;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    bool has_title = !title.empty();
    bool has_photo = photo.small_file_id.is_valid();
    bool has_username = !username.empty();
    bool has_accent_color_id = true;
    bool has_channel_id = channel_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_title);
    STORE_FLAG(has_photo);
    STORE_FLAG(has_username);
    STORE_FLAG(has_accent_color_id);
    STORE_FLAG(has_channel_id);
    END_STORE_FLAGS();
    if (has_title) {
      store(title, storer);
    }
    if (has_photo) {
      store(photo, storer);
    }
    if (has_username) {
      store(username, storer);
    }
    store(accent_color_id, storer);
    if (has_channel_id) {
      store(channel_id, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    bool has_title;
    bool has_photo;
    bool has_username;
    bool has_accent_color_id = false;
    bool has_channel_id = false;
    if (parser.version() >= static_cast<int32>(Version::AddPageBlockChatLinkFlags)) {
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(has_title);
      PARSE_FLAG(has_photo);
      PARSE_FLAG(has_username);
      PARSE_FLAG(has_accent_color_id);
      PARSE_FLAG(has_channel_id);
      END_PARSE_FLAGS();
    } else {
      has_title = true;
      has_photo = true;
      has_username = true;
    }
    if (has_title) {
      parse(title, parser);
    }
    if (has_photo) {
      parse(photo, parser);
    }
    if (has_username) {
      parse(username, parser);
    }
    if (has_accent_color_id) {
      parse(accent_color_id, parser);
    } else {
      accent_color_id = AccentColorId(5);  // blue
    }
    if (has_channel_id) {
      parse(channel_id, parser);
    } else {
      channel_id = ChannelId(static_cast<int64>(5));  // blue
    }
  }
};

class WebPageBlockAudio final : public WebPageBlock {
  FileId audio_file_id;
  WebPageBlockCaption caption;

 public:
  WebPageBlockAudio() = default;
  WebPageBlockAudio(FileId audio_file_id, WebPageBlockCaption &&caption)
      : audio_file_id(audio_file_id), caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::Audio;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    Document(Document::Type::Audio, audio_file_id).append_file_ids(td, file_ids);
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return rights.can_send_audios();
  }

  int32 get_index_mask() const final {
    int32 index_mask = caption.get_index_mask();
    if (audio_file_id.is_valid()) {
      return index_mask | message_search_filter_index_mask(MessageSearchFilter::Audio);
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockAudio>(audio_file_id, caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto file_view = td->file_manager_->get_file_view(audio_file_id);
    const auto *main_remote_location = file_view.get_main_remote_location();
    if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
      documents.push_back(main_remote_location->as_input_document());
      return telegram_api::make_object<telegram_api::pageBlockAudio>(main_remote_location->get_id(),
                                                                     caption.get_input_page_caption(td, documents));
    }
    LOG(ERROR) << "Can't create pageBlockAudio for " << audio_file_id;
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockAudio>(context->td_->audios_manager_->get_audio_object(audio_file_id),
                                                       caption.get_page_block_caption_object(context));
  }

  friend bool operator==(const WebPageBlockAudio &lhs, const WebPageBlockAudio &rhs) {
    return lhs.audio_file_id == rhs.audio_file_id && lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;

    bool has_empty_audio = !audio_file_id.is_valid();
    bool is_voice_note_repaired = true;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_empty_audio);
    STORE_FLAG(is_voice_note_repaired);
    END_STORE_FLAGS();

    if (!has_empty_audio) {
      storer.context()->td().get_actor_unsafe()->audios_manager_->store_audio(audio_file_id, storer);
    }
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;

    bool has_empty_audio;
    bool is_voice_note_repaired;
    if (parser.version() >= static_cast<int32>(Version::FixPageBlockAudioEmptyFile)) {
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(has_empty_audio);
      PARSE_FLAG(is_voice_note_repaired);
      END_PARSE_FLAGS();
    } else {
      has_empty_audio = false;
      is_voice_note_repaired = false;
    }

    if (!has_empty_audio) {
      audio_file_id = parser.context()->td().get_actor_unsafe()->audios_manager_->parse_audio(parser);
    } else {
      if (!is_voice_note_repaired) {
        parser.set_error("Trying to repair WebPageBlockVoiceNote");
      }
      audio_file_id = FileId();
    }
    parse(caption, parser);
  }
};

class WebPageBlockTable final : public WebPageBlock {
  RichText title;
  vector<vector<WebPageBlockTableCell>> cells;
  bool is_bordered = false;
  bool is_striped = false;

 public:
  WebPageBlockTable() = default;
  WebPageBlockTable(RichText &&title, vector<vector<WebPageBlockTableCell>> &&cells, bool is_bordered, bool is_striped)
      : title(std::move(title)), cells(std::move(cells)), is_bordered(is_bordered), is_striped(is_striped) {
  }

  Type get_type() const final {
    return Type::Table;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    title.append_file_ids(td, file_ids);
    for (auto &row : cells) {
      for (auto &cell : row) {
        cell.append_file_ids(td, file_ids);
      }
    }
  }

  void add_dependencies(Dependencies &dependencies) const final {
    title.add_dependencies(dependencies);
    for (auto &row : cells) {
      for (auto &cell : row) {
        cell.add_dependencies(dependencies);
      }
    }
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    title.for_each_rich_text(recurse_text, callback);
    for (auto &row : cells) {
      for (auto &cell : row) {
        cell.for_each_rich_text(recurse_text, callback);
      }
    }
  }

  int32 get_index_mask() const final {
    int32 index_mask = title.get_index_mask();
    for (auto &row : cells) {
      for (auto &cell : row) {
        index_mask |= cell.get_index_mask();
      }
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockTable>(title.clone(), vector<vector<WebPageBlockTableCell>>(cells), is_bordered,
                                              is_striped);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto cell_objects = transform(cells, [&](const vector<WebPageBlockTableCell> &row) {
      return telegram_api::make_object<telegram_api::pageTableRow>(transform(
          row, [&](const WebPageBlockTableCell &cell) { return cell.get_input_page_table_cell(td, documents); }));
    });
    return telegram_api::make_object<telegram_api::pageBlockTable>(
        0, is_bordered, is_striped, title.get_input_rich_text(td, documents), std::move(cell_objects));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    auto cell_objects = transform(cells, [&](const vector<WebPageBlockTableCell> &row) {
      return transform(
          row, [&](const WebPageBlockTableCell &cell) { return cell.get_page_block_table_cell_object(context); });
    });

    return td_api::make_object<td_api::pageBlockTable>(title.get_rich_text_object(context, true),
                                                       std::move(cell_objects), is_bordered, is_striped);
  }

  friend bool operator==(const WebPageBlockTable &lhs, const WebPageBlockTable &rhs) {
    return lhs.title == rhs.title && lhs.cells == rhs.cells && lhs.is_bordered == rhs.is_bordered &&
           lhs.is_striped == rhs.is_striped;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_bordered);
    STORE_FLAG(is_striped);
    END_STORE_FLAGS();
    store(title, storer);
    store(cells, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_bordered);
    PARSE_FLAG(is_striped);
    END_PARSE_FLAGS();
    parse(title, parser);
    parse(cells, parser);
  }
};

class WebPageBlockDetails final : public WebPageBlock {
  RichText header;
  vector<unique_ptr<WebPageBlock>> page_blocks;
  bool is_open = false;

 public:
  WebPageBlockDetails() = default;
  WebPageBlockDetails(RichText &&header, vector<unique_ptr<WebPageBlock>> &&page_blocks, bool is_open)
      : header(std::move(header)), page_blocks(std::move(page_blocks)), is_open(is_open) {
  }

  Type get_type() const final {
    return Type::Details;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    header.append_file_ids(td, file_ids);
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(td, file_ids);
    }
  }

  void add_dependencies(Dependencies &dependencies) const final {
    header.add_dependencies(dependencies);
    for (auto &page_block : page_blocks) {
      page_block->add_dependencies(dependencies);
    }
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    header.for_each_rich_text(recurse_text, callback);
    for (auto &page_block : page_blocks) {
      page_block->for_each_rich_text(recurse_text, callback);
    }
  }

  bool can_send(const RestrictedRights &rights) const final {
    for (const auto &page_block : page_blocks) {
      if (!page_block->can_send(rights)) {
        return false;
      }
    }
    return true;
  }

  int32 get_index_mask() const final {
    return header.get_index_mask() | get_web_page_blocks_index_mask(page_blocks);
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockDetails>(header.clone(), clone_web_page_blocks(page_blocks), is_open);
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockDetails>(
        0, is_open, get_input_page_blocks(page_blocks, td, photos, documents),
        header.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockDetails>(header.get_rich_text_object(context),
                                                         get_page_blocks_object(page_blocks, context), is_open);
  }

  friend bool operator==(const WebPageBlockDetails &lhs, const WebPageBlockDetails &rhs) {
    return lhs.header == rhs.header && lhs.page_blocks == rhs.page_blocks && lhs.is_open == rhs.is_open;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(is_open);
    END_STORE_FLAGS();
    store(header, storer);
    store(page_blocks, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(is_open);
    END_PARSE_FLAGS();
    parse(header, parser);
    parse(page_blocks, parser);
  }
};

class WebPageBlockBlockQuoteBlocks final : public WebPageBlock {
  vector<unique_ptr<WebPageBlock>> page_blocks;
  RichText caption;

 public:
  WebPageBlockBlockQuoteBlocks() = default;
  WebPageBlockBlockQuoteBlocks(vector<unique_ptr<WebPageBlock>> &&page_blocks, RichText &&caption)
      : page_blocks(std::move(page_blocks)), caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::BlockQuoteBlocks;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    for (auto &page_block : page_blocks) {
      page_block->append_file_ids(td, file_ids);
    }
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    for (auto &page_block : page_blocks) {
      page_block->add_dependencies(dependencies);
    }
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    for (auto &page_block : page_blocks) {
      page_block->for_each_rich_text(recurse_text, callback);
    }
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    for (const auto &page_block : page_blocks) {
      if (!page_block->can_send(rights)) {
        return false;
      }
    }
    return true;
  }

  int32 get_index_mask() const final {
    return get_web_page_blocks_index_mask(page_blocks) | caption.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockBlockQuoteBlocks>(clone_web_page_blocks(page_blocks), caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::pageBlockBlockquoteBlocks>(
        get_input_page_blocks(page_blocks, td, photos, documents), caption.get_input_rich_text(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockBlockQuote>(get_page_blocks_object(page_blocks, context),
                                                            caption.get_rich_text_object(context, true));
  }

  friend bool operator==(const WebPageBlockBlockQuoteBlocks &lhs, const WebPageBlockBlockQuoteBlocks &rhs) {
    return lhs.page_blocks == rhs.page_blocks && lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    BEGIN_STORE_FLAGS();
    END_STORE_FLAGS();
    store(caption, storer);
    store(page_blocks, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    BEGIN_PARSE_FLAGS();
    END_PARSE_FLAGS();
    parse(caption, parser);
    parse(page_blocks, parser);
  }
};

class WebPageBlockRelatedArticles final : public WebPageBlock {
  RichText header;
  vector<RelatedArticle> related_articles;

 public:
  WebPageBlockRelatedArticles() = default;
  WebPageBlockRelatedArticles(RichText &&header, vector<RelatedArticle> &&related_articles)
      : header(std::move(header)), related_articles(std::move(related_articles)) {
  }

  Type get_type() const final {
    return Type::RelatedArticles;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    header.append_file_ids(td, file_ids);
    for (const auto &article : related_articles) {
      article.append_file_ids(td, file_ids);
    }
  }

  void add_dependencies(Dependencies &dependencies) const final {
    header.add_dependencies(dependencies);
    for (const auto &article : related_articles) {
      article.add_dependencies(dependencies);
    }
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    header.for_each_rich_text(recurse_text, callback);
    for (const auto &article : related_articles) {
      article.for_each_rich_text(recurse_text, callback);
    }
  }

  int32 get_index_mask() const final {
    return header.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockRelatedArticles>(header.clone(), vector<RelatedArticle>(related_articles));
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    LOG(ERROR) << "Have pageBlockRelatedArticles";
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    auto related_article_objects = transform(related_articles, [context](const RelatedArticle &article) {
      return article.get_page_block_related_article_object(context->td_->file_manager_.get());
    });
    return td_api::make_object<td_api::pageBlockRelatedArticles>(header.get_rich_text_object(context),
                                                                 std::move(related_article_objects));
  }

  friend bool operator==(const WebPageBlockRelatedArticles &lhs, const WebPageBlockRelatedArticles &rhs) {
    return lhs.header == rhs.header && lhs.related_articles == rhs.related_articles;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(header, storer);
    store(related_articles, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(header, parser);
    parse(related_articles, parser);
  }
};

class WebPageBlockMap final : public WebPageBlock {
  Location location;
  int32 zoom = 0;
  Dimensions dimensions;
  WebPageBlockCaption caption;

 public:
  WebPageBlockMap() = default;
  WebPageBlockMap(Location location, int32 zoom, Dimensions dimensions, WebPageBlockCaption &&caption)
      : location(std::move(location)), zoom(zoom), dimensions(dimensions), caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::Map;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  int32 get_index_mask() const final {
    return caption.get_index_mask();
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockMap>(location, zoom, dimensions, caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    return telegram_api::make_object<telegram_api::inputPageBlockMap>(location.get_input_geo_point(), zoom,
                                                                      dimensions.width, dimensions.height,
                                                                      caption.get_input_page_caption(td, documents));
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockMap>(location.get_location_object(), zoom, dimensions.width,
                                                     dimensions.height, caption.get_page_block_caption_object(context));
  }

  friend bool operator==(const WebPageBlockMap &lhs, const WebPageBlockMap &rhs) {
    return lhs.location == rhs.location && lhs.zoom == rhs.zoom && lhs.dimensions == rhs.dimensions &&
           lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;
    store(location, storer);
    store(zoom, storer);
    store(dimensions, storer);
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;
    parse(location, parser);
    parse(zoom, parser);
    parse(dimensions, parser);
    parse(caption, parser);
  }
};

class WebPageBlockVoiceNote final : public WebPageBlock {
  FileId voice_note_file_id;
  WebPageBlockCaption caption;

 public:
  WebPageBlockVoiceNote() = default;
  WebPageBlockVoiceNote(FileId voice_note_file_id, WebPageBlockCaption &&caption)
      : voice_note_file_id(voice_note_file_id), caption(std::move(caption)) {
  }

  Type get_type() const final {
    return Type::VoiceNote;
  }

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const final {
    Document(Document::Type::VoiceNote, voice_note_file_id).append_file_ids(td, file_ids);
    caption.append_file_ids(td, file_ids);
  }

  void add_dependencies(Dependencies &dependencies) const final {
    caption.add_dependencies(dependencies);
  }

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText *text)> &callback) const final {
    caption.for_each_rich_text(recurse_text, callback);
  }

  bool can_send(const RestrictedRights &rights) const final {
    return rights.can_send_voice_notes();
  }

  int32 get_index_mask() const final {
    int32 index_mask = caption.get_index_mask();
    if (voice_note_file_id.is_valid()) {
      return index_mask | message_search_filter_index_mask(MessageSearchFilter::VoiceNote) |
             message_search_filter_index_mask(MessageSearchFilter::VoiceAndVideoNote);
    }
    return index_mask;
  }

  unique_ptr<WebPageBlock> clone() const final {
    return td::make_unique<WebPageBlockVoiceNote>(voice_note_file_id, caption.clone());
  }

  telegram_api::object_ptr<telegram_api::PageBlock> get_input_page_block(
      const Td *td, vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
      vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) const final {
    auto file_view = td->file_manager_->get_file_view(voice_note_file_id);
    const auto *main_remote_location = file_view.get_main_remote_location();
    if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
      documents.push_back(main_remote_location->as_input_document());
      return telegram_api::make_object<telegram_api::pageBlockAudio>(main_remote_location->get_id(),
                                                                     caption.get_input_page_caption(td, documents));
    }
    LOG(ERROR) << "Can't create pageBlockAudio for " << voice_note_file_id;
    return telegram_api::make_object<telegram_api::pageBlockDivider>();
  }

  td_api::object_ptr<td_api::PageBlock> get_page_block_object(Context *context) const final {
    return td_api::make_object<td_api::pageBlockVoiceNote>(
        context->td_->voice_notes_manager_->get_voice_note_object(voice_note_file_id),
        caption.get_page_block_caption_object(context));
  }

  friend bool operator==(const WebPageBlockVoiceNote &lhs, const WebPageBlockVoiceNote &rhs) {
    return lhs.voice_note_file_id == rhs.voice_note_file_id && lhs.caption == rhs.caption;
  }

  template <class StorerT>
  void store(StorerT &storer) const {
    using ::td::store;

    bool has_empty_voice_note = !voice_note_file_id.is_valid();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(has_empty_voice_note);
    END_STORE_FLAGS();

    if (!has_empty_voice_note) {
      storer.context()->td().get_actor_unsafe()->voice_notes_manager_->store_voice_note(voice_note_file_id, storer);
    }
    store(caption, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using ::td::parse;

    bool has_empty_voice_note;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(has_empty_voice_note);
    END_PARSE_FLAGS();

    if (!has_empty_voice_note) {
      voice_note_file_id = parser.context()->td().get_actor_unsafe()->voice_notes_manager_->parse_voice_note(parser);
    } else {
      voice_note_file_id = FileId();
    }
    parse(caption, parser);
  }
};

vector<RichText> get_rich_texts(vector<tl_object_ptr<telegram_api::RichText>> &&rich_text_ptrs,
                                const FlatHashMap<int64, FileId> &documents);

RichText get_rich_text(tl_object_ptr<telegram_api::RichText> &&rich_text_ptr,
                       const FlatHashMap<int64, FileId> &documents) {
  CHECK(rich_text_ptr != nullptr);

  RichText result;
  switch (rich_text_ptr->get_id()) {
    case telegram_api::textEmpty::ID:
      break;
    case telegram_api::textPlain::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textPlain>(rich_text_ptr);
      result.content = std::move(rich_text->text_);
      break;
    }
    case telegram_api::textBold::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textBold>(rich_text_ptr);
      result.type = RichText::Type::Bold;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textItalic::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textItalic>(rich_text_ptr);
      result.type = RichText::Type::Italic;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textUnderline::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textUnderline>(rich_text_ptr);
      result.type = RichText::Type::Underline;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textStrike::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textStrike>(rich_text_ptr);
      result.type = RichText::Type::Strikethrough;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textFixed::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textFixed>(rich_text_ptr);
      result.type = RichText::Type::Fixed;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textUrl::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textUrl>(rich_text_ptr);
      result.type = RichText::Type::Url;
      result.content = std::move(rich_text->url_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      result.web_page_id = WebPageId(rich_text->webpage_id_);
      break;
    }
    case telegram_api::textEmail::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textEmail>(rich_text_ptr);
      result.type = RichText::Type::EmailAddress;
      result.content = std::move(rich_text->email_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textConcat::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textConcat>(rich_text_ptr);
      result.type = RichText::Type::Concatenation;
      result.texts = get_rich_texts(std::move(rich_text->texts_), documents);
      break;
    }
    case telegram_api::textSubscript::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textSubscript>(rich_text_ptr);
      result.type = RichText::Type::Subscript;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textSuperscript::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textSuperscript>(rich_text_ptr);
      result.type = RichText::Type::Superscript;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textMarked::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMarked>(rich_text_ptr);
      result.type = RichText::Type::Marked;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textPhone::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textPhone>(rich_text_ptr);
      result.type = RichText::Type::PhoneNumber;
      result.content = std::move(rich_text->phone_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textImage::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textImage>(rich_text_ptr);
      auto it = documents.find(rich_text->document_id_);
      if (it != documents.end()) {
        result.type = RichText::Type::Icon;
        result.document_file_id = it->second;
        Dimensions dimensions = get_dimensions(rich_text->w_, rich_text->h_, "textImage");
        result.content = PSTRING() << (dimensions.width * static_cast<uint32>(65536) + dimensions.height);
      } else {
        LOG(ERROR) << "Can't find document " << rich_text->document_id_;
      }
      break;
    }
    case telegram_api::textAnchor::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAnchor>(rich_text_ptr);
      result.type = RichText::Type::Anchor;
      result.content = std::move(rich_text->name_);
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textMath::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMath>(rich_text_ptr);
      result.type = RichText::Type::Math;
      result.content = std::move(rich_text->source_);
      break;
    }
    case telegram_api::textCustomEmoji::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textCustomEmoji>(rich_text_ptr);
      result.type = RichText::Type::CustomEmoji;
      result.custom_emoji_id = CustomEmojiId(rich_text->document_id_);
      result.content = std::move(rich_text->alt_);
      break;
    }
    case telegram_api::textSpoiler::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textSpoiler>(rich_text_ptr);
      result.type = RichText::Type::Spoiler;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textMention::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMention>(rich_text_ptr);
      result.type = RichText::Type::Mention;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textHashtag::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textHashtag>(rich_text_ptr);
      result.type = RichText::Type::Hashtag;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textBotCommand::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textBotCommand>(rich_text_ptr);
      result.type = RichText::Type::BotCommand;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textCashtag::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textCashtag>(rich_text_ptr);
      result.type = RichText::Type::Cashtag;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textAutoUrl::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAutoUrl>(rich_text_ptr);
      result.type = RichText::Type::AutoUrl;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textAutoEmail::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAutoEmail>(rich_text_ptr);
      result.type = RichText::Type::AutoEmailAddress;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textAutoPhone::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAutoPhone>(rich_text_ptr);
      result.type = RichText::Type::AutoPhoneNumber;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textBankCard::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textBankCard>(rich_text_ptr);
      result.type = RichText::Type::BankCardNumber;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      break;
    }
    case telegram_api::textMentionName::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMentionName>(rich_text_ptr);
      result.type = RichText::Type::MentionName;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      result.user_id = UserId(rich_text->user_id_);
      if (!result.user_id.is_valid()) {
        LOG(ERROR) << "Receive " << result.user_id;
        result.user_id = UserId();
      }
      break;
    }
    case telegram_api::textDate::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textDate>(rich_text_ptr);
      auto date = FormattedDate(rich_text->date_, rich_text->relative_, rich_text->short_time_, rich_text->long_time_,
                                rich_text->short_date_, rich_text->long_date_, rich_text->day_of_week_);
      if (!date.is_valid()) {
        break;
      }
      result.type = RichText::Type::FormattedDate;
      result.texts.push_back(get_rich_text(std::move(rich_text->text_), documents));
      result.date = std::move(date);
      break;
    }
    default:
      UNREACHABLE();
  }
  return result;
}

vector<RichText> get_rich_texts(vector<tl_object_ptr<telegram_api::RichText>> &&rich_text_ptrs,
                                const FlatHashMap<int64, FileId> &documents) {
  return transform(std::move(rich_text_ptrs), [&documents](tl_object_ptr<telegram_api::RichText> &&rich_text) {
    return get_rich_text(std::move(rich_text), documents);
  });
}

WebPageBlockCaption get_page_block_caption(tl_object_ptr<telegram_api::pageCaption> &&page_caption,
                                           const FlatHashMap<int64, FileId> &documents) {
  CHECK(page_caption != nullptr);
  WebPageBlockCaption result;
  result.text = get_rich_text(std::move(page_caption->text_), documents);
  result.credit = get_rich_text(std::move(page_caption->credit_), documents);
  return result;
}

static string get_ordered_list_type(const string &type) {
  if (type.size() != 1u || Slice("aAiI1").find(type[0]) == Slice::npos) {
    return "1";
  }
  return type;
}

static string get_ordered_list_label(int32 num, const string &type) {
  if ((type == "a" || type == "A") && num > 0) {
    string result;
    while (num > 0) {
      num--;
      result = static_cast<char>('A' + num % 26) + result;
      num /= 26;
    };
    if (type == "a") {
      to_lower_inplace(result);
    }
    result += '.';
    return result;
  }
  if ((type == "i" || type == "I") && num > 0 && num < 4000) {
    string result;
    while (num >= 1000) {
      num -= 1000;
      result += 'M';
    }
    if (num >= 900) {
      result += "CM";
      num -= 900;
    } else if (num >= 500) {
      result += 'D';
      num -= 500;
    } else if (num >= 400) {
      result += "CD";
      num -= 400;
    }
    while (num >= 100) {
      result += 'C';
      num -= 100;
    }
    if (num >= 90) {
      result += "XC";
      num -= 90;
    } else if (num >= 50) {
      result += 'L';
      num -= 50;
    } else if (num >= 40) {
      result += "XL";
      num -= 40;
    }
    while (num >= 10) {
      result += 'X';
      num -= 10;
    }
    if (num >= 9) {
      result += "IX";
      num -= 9;
    } else if (num >= 5) {
      result += 'V';
      num -= 5;
    } else if (num >= 4) {
      result += "IV";
      num -= 4;
    }
    while (num >= 1) {
      result += 'I';
      num -= 1;
    }
    if (type == "i") {
      to_lower_inplace(result);
    }
    result += '.';
    return result;
  }
  return PSTRING() << num << '.';
}

unique_ptr<WebPageBlock> get_web_page_block(Td *td, tl_object_ptr<telegram_api::PageBlock> page_block_ptr,
                                            const FlatHashMap<int64, FileId> &animations,
                                            const FlatHashMap<int64, FileId> &audios,
                                            const FlatHashMap<int64, FileId> &documents,
                                            const FlatHashMap<int64, unique_ptr<Photo>> &photos,
                                            const FlatHashMap<int64, FileId> &videos,
                                            const FlatHashMap<int64, FileId> &voice_notes) {
  CHECK(page_block_ptr != nullptr);
  switch (page_block_ptr->get_id()) {
    case telegram_api::pageBlockUnsupported::ID:
      return nullptr;
    case telegram_api::pageBlockTitle::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockTitle>(page_block_ptr);
      return make_unique<WebPageBlockTitle>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockSubtitle::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockSubtitle>(page_block_ptr);
      return make_unique<WebPageBlockSubtitle>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockAuthorDate::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockAuthorDate>(page_block_ptr);
      return make_unique<WebPageBlockAuthorDate>(get_rich_text(std::move(page_block->author_), documents),
                                                 page_block->published_date_);
    }
    case telegram_api::pageBlockHeader::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeader>(page_block_ptr);
      return make_unique<WebPageBlockHeader>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockSubheader::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockSubheader>(page_block_ptr);
      return make_unique<WebPageBlockSubheader>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockKicker::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockKicker>(page_block_ptr);
      return make_unique<WebPageBlockKicker>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockParagraph::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockParagraph>(page_block_ptr);
      return make_unique<WebPageBlockParagraph>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockPreformatted::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockPreformatted>(page_block_ptr);
      return td::make_unique<WebPageBlockPreformatted>(get_rich_text(std::move(page_block->text_), documents),
                                                       std::move(page_block->language_));
    }
    case telegram_api::pageBlockFooter::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockFooter>(page_block_ptr);
      return make_unique<WebPageBlockFooter>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::pageBlockDivider::ID:
      return make_unique<WebPageBlockDivider>();
    case telegram_api::pageBlockAnchor::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockAnchor>(page_block_ptr);
      return td::make_unique<WebPageBlockAnchor>(std::move(page_block->name_));
    }
    case telegram_api::pageBlockList::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockList>(page_block_ptr);
      return td::make_unique<WebPageBlockList>(
          transform(std::move(page_block->items_),
                    [&](auto &&list_item_ptr) {
                      WebPageBlockList::Item item;
                      CHECK(list_item_ptr != nullptr);
                      switch (list_item_ptr->get_id()) {
                        case telegram_api::pageListItemText::ID: {
                          auto list_item = telegram_api::move_object_as<telegram_api::pageListItemText>(list_item_ptr);
                          item.page_blocks.push_back(make_unique<WebPageBlockParagraph>(
                              get_rich_text(std::move(list_item->text_), documents)));
                          item.has_checkbox = list_item->checkbox_;
                          item.is_checked = list_item->checked_;
                          break;
                        }
                        case telegram_api::pageListItemBlocks::ID: {
                          auto list_item =
                              telegram_api::move_object_as<telegram_api::pageListItemBlocks>(list_item_ptr);
                          item.page_blocks = get_web_page_blocks(td, std::move(list_item->blocks_), animations, audios,
                                                                 documents, photos, videos, voice_notes);
                          item.has_checkbox = list_item->checkbox_;
                          item.is_checked = list_item->checked_;
                          break;
                        }
                      }
                      if (item.page_blocks.empty()) {
                        item.page_blocks.push_back(make_unique<WebPageBlockParagraph>(RichText()));
                      }
                      return item;
                    }),
          0, false, string());
    }
    case telegram_api::pageBlockOrderedList::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockOrderedList>(page_block_ptr);
      int32 next_value = 1;
      if ((page_block->flags_ & telegram_api::pageBlockOrderedList::START_MASK) != 0) {
        next_value = page_block->start_;
      }
      auto base_type = get_ordered_list_type(page_block->type_);
      return td::make_unique<WebPageBlockList>(
          transform(std::move(page_block->items_),
                    [&](auto &&list_item_ptr) {
                      WebPageBlockList::Item item;
                      CHECK(list_item_ptr != nullptr);
                      switch (list_item_ptr->get_id()) {
                        case telegram_api::pageListOrderedItemText::ID: {
                          auto list_item =
                              telegram_api::move_object_as<telegram_api::pageListOrderedItemText>(list_item_ptr);
                          item.label = std::move(list_item->num_);
                          item.page_blocks.push_back(make_unique<WebPageBlockParagraph>(
                              get_rich_text(std::move(list_item->text_), documents)));
                          item.has_checkbox = list_item->checkbox_;
                          item.is_checked = list_item->checked_;
                          if ((list_item->flags_ & telegram_api::pageListOrderedItemText::VALUE_MASK) != 0) {
                            item.value = list_item->value_;
                          } else {
                            item.value = next_value;
                          }
                          item.type = list_item->type_.empty() ? base_type : get_ordered_list_type(list_item->type_);
                          break;
                        }
                        case telegram_api::pageListOrderedItemBlocks::ID: {
                          auto list_item =
                              telegram_api::move_object_as<telegram_api::pageListOrderedItemBlocks>(list_item_ptr);
                          item.label = std::move(list_item->num_);
                          item.page_blocks = get_web_page_blocks(td, std::move(list_item->blocks_), animations, audios,
                                                                 documents, photos, videos, voice_notes);
                          item.has_checkbox = list_item->checkbox_;
                          item.is_checked = list_item->checked_;
                          if ((list_item->flags_ & telegram_api::pageListOrderedItemBlocks::VALUE_MASK) != 0) {
                            item.value = list_item->value_;
                          } else {
                            item.value = next_value;
                          }
                          item.type = list_item->type_.empty() ? base_type : get_ordered_list_type(list_item->type_);
                          break;
                        }
                        default:
                          UNREACHABLE();
                      }
                      if (item.page_blocks.empty()) {
                        item.page_blocks.push_back(make_unique<WebPageBlockParagraph>(RichText()));
                      }
                      if (item.label.empty()) {
                        item.label = get_ordered_list_label(item.value, item.type);
                      } else {
                        item.label += '.';
                      }
                      next_value = item.value + (page_block->reversed_ ? -1 : 1);
                      return item;
                    }),
          page_block->start_, page_block->reversed_, base_type);
    }
    case telegram_api::pageBlockBlockquote::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockBlockquote>(page_block_ptr);
      return make_unique<WebPageBlockBlockQuote>(get_rich_text(std::move(page_block->text_), documents),
                                                 get_rich_text(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockPullquote::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockPullquote>(page_block_ptr);
      return make_unique<WebPageBlockPullQuote>(get_rich_text(std::move(page_block->text_), documents),
                                                get_rich_text(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockPhoto::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockPhoto>(page_block_ptr);
      auto it = photos.find(page_block->photo_id_);
      Photo photo;
      if (it != photos.end()) {
        photo = *it->second;
      }
      return td::make_unique<WebPageBlockPhoto>(
          std::move(photo), get_page_block_caption(std::move(page_block->caption_), documents),
          std::move(page_block->url_), WebPageId(page_block->webpage_id_), page_block->spoiler_);
    }
    case telegram_api::pageBlockVideo::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockVideo>(page_block_ptr);
      bool need_autoplay = page_block->autoplay_;
      bool is_looped = page_block->loop_;
      auto animations_it = animations.find(page_block->video_id_);
      if (animations_it != animations.end()) {
        return make_unique<WebPageBlockAnimation>(animations_it->second,
                                                  get_page_block_caption(std::move(page_block->caption_), documents),
                                                  need_autoplay, page_block->spoiler_);
      }

      auto it = videos.find(page_block->video_id_);
      FileId video_file_id;
      if (it != videos.end()) {
        video_file_id = it->second;
      }
      return make_unique<WebPageBlockVideo>(video_file_id,
                                            get_page_block_caption(std::move(page_block->caption_), documents),
                                            need_autoplay, is_looped, page_block->spoiler_);
    }
    case telegram_api::pageBlockCover::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockCover>(page_block_ptr);
      auto cover = get_web_page_block(td, std::move(page_block->cover_), animations, audios, documents, photos, videos,
                                      voice_notes);
      if (cover == nullptr) {
        return nullptr;
      }
      return make_unique<WebPageBlockCover>(std::move(cover));
    }
    case telegram_api::pageBlockEmbed::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockEmbed>(page_block_ptr);
      bool is_full_width = page_block->full_width_;
      bool allow_scrolling = page_block->allow_scrolling_;
      Photo poster_photo;
      auto it = photos.find(page_block->poster_photo_id_);
      if (it != photos.end()) {
        poster_photo = *it->second;
      }
      auto dimensions = get_dimensions(page_block->w_, page_block->h_, "pageBlockEmbed");
      return td::make_unique<WebPageBlockEmbedded>(
          std::move(page_block->url_), std::move(page_block->html_), std::move(poster_photo), dimensions,
          get_page_block_caption(std::move(page_block->caption_), documents), is_full_width, allow_scrolling);
    }
    case telegram_api::pageBlockEmbedPost::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockEmbedPost>(page_block_ptr);
      auto it = photos.find(page_block->author_photo_id_);
      Photo author_photo;
      if (it != photos.end()) {
        author_photo = *it->second;
      }
      return td::make_unique<WebPageBlockEmbeddedPost>(
          std::move(page_block->url_), std::move(page_block->author_), std::move(author_photo), page_block->date_,
          get_web_page_blocks(td, std::move(page_block->blocks_), animations, audios, documents, photos, videos,
                              voice_notes),
          get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockCollage::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockCollage>(page_block_ptr);
      return td::make_unique<WebPageBlockCollage>(get_web_page_blocks(td, std::move(page_block->items_), animations,
                                                                      audios, documents, photos, videos, voice_notes),
                                                  get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockSlideshow::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockSlideshow>(page_block_ptr);
      return td::make_unique<WebPageBlockSlideshow>(get_web_page_blocks(td, std::move(page_block->items_), animations,
                                                                        audios, documents, photos, videos, voice_notes),
                                                    get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockChannel::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockChannel>(page_block_ptr);
      CHECK(page_block->channel_ != nullptr);
      if (page_block->channel_->get_id() == telegram_api::channel::ID) {
        auto channel = static_cast<telegram_api::channel *>(page_block->channel_.get());
        ChannelId channel_id(channel->id_);
        if (!channel_id.is_valid()) {
          LOG(ERROR) << "Receive invalid " << channel_id;
          return nullptr;
        }

        if (td->chat_manager_->have_channel_force(channel_id, "pageBlockChannel")) {
          td->chat_manager_->on_get_chat(std::move(page_block->channel_), "pageBlockChannel");
          LOG(INFO) << "Receive known min " << channel_id;
          return td::make_unique<WebPageBlockChatLink>(td->chat_manager_->get_channel_title(channel_id),
                                                       *td->chat_manager_->get_channel_dialog_photo(channel_id),
                                                       td->chat_manager_->get_channel_first_username(channel_id).str(),
                                                       td->chat_manager_->get_channel_accent_color_id(channel_id),
                                                       channel_id);
        } else {
          PeerColor peer_color(channel->color_);
          return td::make_unique<WebPageBlockChatLink>(
              std::move(channel->title_),
              get_dialog_photo(td->file_manager_.get(), DialogId(channel_id), channel->access_hash_,
                               std::move(channel->photo_)),
              std::move(channel->username_), peer_color.accent_color_id_, channel_id);
        }
      } else {
        LOG(ERROR) << "Receive wrong channel " << to_string(page_block->channel_);
        return nullptr;
      }
    }
    case telegram_api::pageBlockAudio::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockAudio>(page_block_ptr);
      auto voice_note_it = voice_notes.find(page_block->audio_id_);
      if (voice_note_it != voice_notes.end()) {
        return make_unique<WebPageBlockVoiceNote>(voice_note_it->second,
                                                  get_page_block_caption(std::move(page_block->caption_), documents));
      }

      auto it = audios.find(page_block->audio_id_);
      FileId audio_file_id;
      if (it != audios.end()) {
        audio_file_id = it->second;
      }
      return make_unique<WebPageBlockAudio>(audio_file_id,
                                            get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockTable::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockTable>(page_block_ptr);
      auto is_bordered = page_block->bordered_;
      auto is_striped = page_block->striped_;
      auto cells = transform(std::move(page_block->rows_), [&](tl_object_ptr<telegram_api::pageTableRow> &&row) {
        return transform(std::move(row->cells_), [&](tl_object_ptr<telegram_api::pageTableCell> &&table_cell) {
          WebPageBlockTableCell cell;
          cell.is_header = table_cell->header_;
          cell.align_center = table_cell->align_center_;
          if (!cell.align_center) {
            cell.align_right = table_cell->align_right_;
            if (!cell.align_right) {
              cell.align_left = true;
            }
          }
          cell.valign_middle = table_cell->valign_middle_;
          if (!cell.valign_middle) {
            cell.valign_bottom = table_cell->valign_bottom_;
            if (!cell.valign_bottom) {
              cell.valign_top = true;
            }
          }
          if (table_cell->text_ != nullptr) {
            cell.text = get_rich_text(std::move(table_cell->text_), documents);
          }
          cell.colspan = max(table_cell->colspan_, 1);
          cell.rowspan = max(table_cell->rowspan_, 1);
          return cell;
        });
      });
      return td::make_unique<WebPageBlockTable>(get_rich_text(std::move(page_block->title_), documents),
                                                std::move(cells), is_bordered, is_striped);
    }
    case telegram_api::pageBlockDetails::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockDetails>(page_block_ptr);
      auto is_open = page_block->open_;
      return td::make_unique<WebPageBlockDetails>(get_rich_text(std::move(page_block->title_), documents),
                                                  get_web_page_blocks(td, std::move(page_block->blocks_), animations,
                                                                      audios, documents, photos, videos, voice_notes),
                                                  is_open);
    }
    case telegram_api::pageBlockRelatedArticles::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockRelatedArticles>(page_block_ptr);
      auto articles = transform(std::move(page_block->articles_),
                                [&](tl_object_ptr<telegram_api::pageRelatedArticle> &&related_article) {
                                  RelatedArticle article;
                                  article.url = std::move(related_article->url_);
                                  article.web_page_id = WebPageId(related_article->webpage_id_);
                                  article.title = std::move(related_article->title_);
                                  article.description = std::move(related_article->description_);
                                  auto it = photos.find(related_article->photo_id_);
                                  if (it != photos.end()) {
                                    article.photo = *it->second;
                                  }
                                  article.author = std::move(related_article->author_);
                                  if (related_article->published_date_ > 0) {
                                    article.published_date = related_article->published_date_;
                                  }
                                  return article;
                                });
      return td::make_unique<WebPageBlockRelatedArticles>(get_rich_text(std::move(page_block->title_), documents),
                                                          std::move(articles));
    }
    case telegram_api::pageBlockMap::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockMap>(page_block_ptr);
      Location location(td, page_block->geo_);
      auto zoom = page_block->zoom_;
      Dimensions dimensions = get_dimensions(page_block->w_, page_block->h_, "pageBlockMap");
      if (location.empty()) {
        LOG(ERROR) << "Receive invalid map location";
        break;
      }
      if (zoom <= 0 || zoom > 30) {
        LOG(ERROR) << "Receive invalid map zoom " << zoom;
        break;
      }
      if (dimensions.width == 0) {
        LOG(ERROR) << "Receive invalid map dimensions " << page_block->w_ << " " << page_block->h_;
        break;
      }
      return make_unique<WebPageBlockMap>(std::move(location), zoom, dimensions,
                                          get_page_block_caption(std::move(page_block->caption_), documents));
    }
    case telegram_api::pageBlockHeading1::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeading1>(page_block_ptr);
      return make_unique<WebPageBlockHeading>(get_rich_text(std::move(page_block->text_), documents), 1);
    }
    case telegram_api::pageBlockHeading2::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeading2>(page_block_ptr);
      return make_unique<WebPageBlockHeading>(get_rich_text(std::move(page_block->text_), documents), 2);
    }
    case telegram_api::pageBlockHeading3::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeading3>(page_block_ptr);
      return make_unique<WebPageBlockHeading>(get_rich_text(std::move(page_block->text_), documents), 3);
    }
    case telegram_api::pageBlockHeading4::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeading4>(page_block_ptr);
      return make_unique<WebPageBlockHeading>(get_rich_text(std::move(page_block->text_), documents), 4);
    }
    case telegram_api::pageBlockHeading5::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeading5>(page_block_ptr);
      return make_unique<WebPageBlockHeading>(get_rich_text(std::move(page_block->text_), documents), 5);
    }
    case telegram_api::pageBlockHeading6::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockHeading6>(page_block_ptr);
      return make_unique<WebPageBlockHeading>(get_rich_text(std::move(page_block->text_), documents), 6);
    }
    case telegram_api::pageBlockMath::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockMath>(page_block_ptr);
      return td::make_unique<WebPageBlockMath>(std::move(page_block->source_));
    }
    case telegram_api::pageBlockThinking::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockThinking>(page_block_ptr);
      return td::make_unique<WebPageBlockThinking>(get_rich_text(std::move(page_block->text_), documents));
    }
    case telegram_api::inputPageBlockMap::ID:
      return nullptr;
    case telegram_api::pageBlockBlockquoteBlocks::ID: {
      auto page_block = telegram_api::move_object_as<telegram_api::pageBlockBlockquoteBlocks>(page_block_ptr);
      return td::make_unique<WebPageBlockBlockQuoteBlocks>(
          get_web_page_blocks(td, std::move(page_block->blocks_), animations, audios, documents, photos, videos,
                              voice_notes),
          get_rich_text(std::move(page_block->caption_), documents));
    }
    default:
      UNREACHABLE();
  }
  return nullptr;
}

}  // namespace

void WebPageBlock::for_each_text(const std::function<void(Slice text)> &callback) const {
  for_each_rich_text(false, [&](const RichText *text) { callback(text->get_full_text()); });
}

void WebPageBlock::append_user_ids(vector<UserId> &user_ids) const {
  for_each_rich_text(true, [&](const RichText *text) {
    if (text->user_id.is_valid()) {
      user_ids.push_back(text->user_id);
    }
  });
}

bool WebPageBlock::has_bot_commands() const {
  bool result = false;
  for_each_rich_text(true, [&](const RichText *text) {
    if (text->type == RichText::Type::BotCommand) {
      result = true;
    }
  });
  return result;
}

vector<string> WebPageBlock::get_hashtags() const {
  vector<string> result;
  for_each_rich_text(true, [&](const RichText *text) {
    if (text->type == RichText::Type::Hashtag) {
      auto hashtag = text->get_full_text();
      if (!hashtag.empty()) {
        if (hashtag[0] == '#') {
          hashtag = hashtag.substr(1);
        }
        if (!hashtag.empty()) {
          result.push_back(hashtag);
        }
      }
    }
  });
  return result;
}

vector<CustomEmojiId> WebPageBlock::get_custom_emoji_ids() const {
  vector<CustomEmojiId> custom_emoji_ids;
  for_each_rich_text(true, [&](const RichText *text) {
    if (text->custom_emoji_id.is_valid()) {
      custom_emoji_ids.push_back(text->custom_emoji_id);
    }
  });
  return custom_emoji_ids;
}

template <class F>
void WebPageBlock::call_impl(Type type, const WebPageBlock *ptr, F &&f) {
  switch (type) {
    case Type::Title:
      return f(static_cast<const WebPageBlockTitle *>(ptr));
    case Type::Subtitle:
      return f(static_cast<const WebPageBlockSubtitle *>(ptr));
    case Type::AuthorDate:
      return f(static_cast<const WebPageBlockAuthorDate *>(ptr));
    case Type::Header:
      return f(static_cast<const WebPageBlockHeader *>(ptr));
    case Type::Subheader:
      return f(static_cast<const WebPageBlockSubheader *>(ptr));
    case Type::Kicker:
      return f(static_cast<const WebPageBlockKicker *>(ptr));
    case Type::Paragraph:
      return f(static_cast<const WebPageBlockParagraph *>(ptr));
    case Type::Preformatted:
      return f(static_cast<const WebPageBlockPreformatted *>(ptr));
    case Type::Footer:
      return f(static_cast<const WebPageBlockFooter *>(ptr));
    case Type::Divider:
      return f(static_cast<const WebPageBlockDivider *>(ptr));
    case Type::Anchor:
      return f(static_cast<const WebPageBlockAnchor *>(ptr));
    case Type::List:
      return f(static_cast<const WebPageBlockList *>(ptr));
    case Type::BlockQuote:
      return f(static_cast<const WebPageBlockBlockQuote *>(ptr));
    case Type::PullQuote:
      return f(static_cast<const WebPageBlockPullQuote *>(ptr));
    case Type::Animation:
      return f(static_cast<const WebPageBlockAnimation *>(ptr));
    case Type::Photo:
      return f(static_cast<const WebPageBlockPhoto *>(ptr));
    case Type::Video:
      return f(static_cast<const WebPageBlockVideo *>(ptr));
    case Type::Cover:
      return f(static_cast<const WebPageBlockCover *>(ptr));
    case Type::Embedded:
      return f(static_cast<const WebPageBlockEmbedded *>(ptr));
    case Type::EmbeddedPost:
      return f(static_cast<const WebPageBlockEmbeddedPost *>(ptr));
    case Type::Collage:
      return f(static_cast<const WebPageBlockCollage *>(ptr));
    case Type::Slideshow:
      return f(static_cast<const WebPageBlockSlideshow *>(ptr));
    case Type::ChatLink:
      return f(static_cast<const WebPageBlockChatLink *>(ptr));
    case Type::Audio:
      return f(static_cast<const WebPageBlockAudio *>(ptr));
    case Type::Table:
      return f(static_cast<const WebPageBlockTable *>(ptr));
    case Type::Details:
      return f(static_cast<const WebPageBlockDetails *>(ptr));
    case Type::RelatedArticles:
      return f(static_cast<const WebPageBlockRelatedArticles *>(ptr));
    case Type::Map:
      return f(static_cast<const WebPageBlockMap *>(ptr));
    case Type::VoiceNote:
      return f(static_cast<const WebPageBlockVoiceNote *>(ptr));
    case Type::Heading:
      return f(static_cast<const WebPageBlockHeading *>(ptr));
    case Type::Math:
      return f(static_cast<const WebPageBlockMath *>(ptr));
    case Type::Thinking:
      return f(static_cast<const WebPageBlockThinking *>(ptr));
    case Type::BlockQuoteBlocks:
      return f(static_cast<const WebPageBlockBlockQuoteBlocks *>(ptr));
    default:
      UNREACHABLE();
  }
}

template <class StorerT>
void WebPageBlock::store(StorerT &storer) const {
  Type type = get_type();
  td::store(type, storer);
  call_impl(type, this, [&](const auto *object) { td::store(*object, storer); });
}

template <class ParserT>
unique_ptr<WebPageBlock> WebPageBlock::parse(ParserT &parser) {
  Type type;
  td::parse(type, parser);
  if (static_cast<int32>(type) < 0 || static_cast<int32>(type) >= static_cast<int32>(Type::Size)) {
    parser.set_error(PSTRING() << "Can't parse unknown BlockType " << static_cast<int32>(type));
    return nullptr;
  }
  unique_ptr<WebPageBlock> res;
  call_impl(type, nullptr, [&](const auto *ptr) {
    using ObjT = std::decay_t<decltype(*ptr)>;
    auto object = make_unique<ObjT>();
    td::parse(*object, parser);
    res = std::move(object);
  });
  return res;
}

template <class StorerT>
void store_web_page_block(const unique_ptr<WebPageBlock> &block, StorerT &storer) {
  block->store(storer);
}

template <class ParserT>
void parse_web_page_block(unique_ptr<WebPageBlock> &block, ParserT &parser) {
  block = WebPageBlock::parse(parser);
}

void store(const unique_ptr<WebPageBlock> &block, LogEventStorerCalcLength &storer) {
  store_web_page_block(block, storer);
}

void store(const unique_ptr<WebPageBlock> &block, LogEventStorerUnsafe &storer) {
  store_web_page_block(block, storer);
}

void parse(unique_ptr<WebPageBlock> &block, LogEventParser &parser) {
  parse_web_page_block(block, parser);
}

vector<unique_ptr<WebPageBlock>> get_web_page_blocks(
    Td *td, vector<tl_object_ptr<telegram_api::PageBlock>> page_block_ptrs,
    const FlatHashMap<int64, FileId> &animations, const FlatHashMap<int64, FileId> &audios,
    const FlatHashMap<int64, FileId> &documents, const FlatHashMap<int64, unique_ptr<Photo>> &photos,
    const FlatHashMap<int64, FileId> &videos, const FlatHashMap<int64, FileId> &voice_notes) {
  vector<unique_ptr<WebPageBlock>> result;
  result.reserve(page_block_ptrs.size());
  for (auto &page_block_ptr : page_block_ptrs) {
    auto page_block =
        get_web_page_block(td, std::move(page_block_ptr), animations, audios, documents, photos, videos, voice_notes);
    if (page_block != nullptr) {
      result.push_back(std::move(page_block));
    }
  }
  return result;
}

int32 get_web_page_blocks_index_mask(const vector<unique_ptr<WebPageBlock>> &page_blocks) {
  int32 index_mask = 0;
  for (const auto &page_block : page_blocks) {
    index_mask |= page_block->get_index_mask();
  }
  return index_mask;
}

vector<unique_ptr<WebPageBlock>> clone_web_page_blocks(const vector<unique_ptr<WebPageBlock>> &page_blocks) {
  return transform(page_blocks, [](const unique_ptr<WebPageBlock> &page_block) { return page_block->clone(); });
}

vector<telegram_api::object_ptr<telegram_api::PageBlock>> get_input_page_blocks(
    const vector<unique_ptr<WebPageBlock>> &page_blocks, const Td *td,
    vector<telegram_api::object_ptr<telegram_api::InputPhoto>> &photos,
    vector<telegram_api::object_ptr<telegram_api::InputDocument>> &documents) {
  return transform(page_blocks, [&](const unique_ptr<WebPageBlock> &page_block) {
    return page_block->get_input_page_block(td, photos, documents);
  });
}

vector<td_api::object_ptr<td_api::PageBlock>> get_page_blocks_object(
    const vector<unique_ptr<WebPageBlock>> &page_blocks, Td *td, Slice base_url, Slice real_url,
    bool skip_bot_commands) {
  GetWebPageBlockObjectContext context;
  context.td_ = td;
  context.base_url_ = base_url;
  context.real_url_rhash_ = LinkManager::get_instant_view_link_rhash(real_url);
  if (!context.real_url_rhash_.empty()) {
    context.real_url_host_ = get_url_host(LinkManager::get_instant_view_link_url(real_url));
    if (context.real_url_host_.empty()) {
      context.real_url_rhash_ = string();
    }
  }
  context.skip_bot_commands_ = skip_bot_commands;
  auto blocks = get_page_blocks_object(page_blocks, &context);
  if (!context.has_anchor_urls_) {
    return blocks;
  }

  context.is_first_pass_ = false;
  context.anchors_.emplace(Slice(), nullptr);  // back to top
  return get_page_blocks_object(page_blocks, &context);
}

bool WebPageBlock::are_allowed_album_block_types(const vector<unique_ptr<WebPageBlock>> &page_blocks) {
  for (const auto &block : page_blocks) {
    switch (block->get_type()) {
      case Type::Title:
      case Type::AuthorDate:
      case Type::Collage:
      case Type::Slideshow:
        continue;
      default:
        return false;
    }
  }
  return true;
}

bool operator==(const WebPageBlock &lhs, const WebPageBlock &rhs) {
  auto type = lhs.get_type();
  if (rhs.get_type() != type) {
    return false;
  }
  bool result = false;
  WebPageBlock::call_impl(type, &lhs, [&](const auto *object) {
    using ObjT = std::decay_t<decltype(*object)>;
    result = (*object == static_cast<const ObjT &>(rhs));
  });
  return result;
}

}  // namespace td
