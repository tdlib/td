//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RichText.h"

#include "td/telegram/Dependencies.h"
#include "td/telegram/Dimensions.h"
#include "td/telegram/Document.h"
#include "td/telegram/DocumentsManager.h"
#include "td/telegram/files/FileManager.h"
#include "td/telegram/LinkManager.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/misc.h"
#include "td/telegram/PhotoFormat.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"
#include "td/telegram/WebPageBlock.h"

#include "td/utils/algorithm.h"
#include "td/utils/HttpUrl.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"

namespace td {

static vector<td_api::object_ptr<td_api::RichText>> get_rich_texts_object(const vector<RichText> &rich_texts,
                                                                          GetWebPageBlockObjectContext *context) {
  return transform(rich_texts,
                   [context](const RichText &rich_text) { return rich_text.get_rich_text_object(context); });
}

static vector<telegram_api::object_ptr<telegram_api::RichText>> get_input_rich_texts(
    const vector<RichText> &rich_texts, GetInputPageBlockContext &context) {
  return transform(rich_texts, [&](const RichText &rich_text) { return rich_text.get_input_rich_text(context); });
}

static string trim_first(const string &str, char c) {
  if (!str.empty() && str[0] == c) {
    return str.substr(1);
  }
  LOG(ERROR) << "Expected string starting with '" << c << "', but received \"" << str << '"';
  return str;
}

static vector<RichText> get_rich_texts(vector<tl_object_ptr<telegram_api::RichText>> &&rich_text_ptrs,
                                       const FlatHashMap<int64, FileId> &documents) {
  return transform(std::move(rich_text_ptrs), [&documents](tl_object_ptr<telegram_api::RichText> &&rich_text) {
    return RichText(std::move(rich_text), documents);
  });
}

RichText::RichText(telegram_api::object_ptr<telegram_api::RichText> &&rich_text_ptr,
                   const FlatHashMap<int64, FileId> &documents) {
  CHECK(rich_text_ptr != nullptr);

  switch (rich_text_ptr->get_id()) {
    case telegram_api::textEmpty::ID:
      break;
    case telegram_api::textPlain::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textPlain>(rich_text_ptr);
      content_ = std::move(rich_text->text_);
      break;
    }
    case telegram_api::textBold::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textBold>(rich_text_ptr);
      type_ = Type::Bold;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textItalic::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textItalic>(rich_text_ptr);
      type_ = Type::Italic;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textUnderline::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textUnderline>(rich_text_ptr);
      type_ = Type::Underline;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textStrike::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textStrike>(rich_text_ptr);
      type_ = Type::Strikethrough;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textFixed::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textFixed>(rich_text_ptr);
      type_ = Type::Fixed;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textUrl::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textUrl>(rich_text_ptr);
      type_ = Type::Url;
      content_ = std::move(rich_text->url_);
      texts_.emplace_back(std::move(rich_text->text_), documents);
      web_page_id_ = WebPageId(rich_text->webpage_id_);
      break;
    }
    case telegram_api::textEmail::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textEmail>(rich_text_ptr);
      type_ = Type::EmailAddress;
      content_ = std::move(rich_text->email_);
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textConcat::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textConcat>(rich_text_ptr);
      type_ = Type::Concatenation;
      texts_ = get_rich_texts(std::move(rich_text->texts_), documents);
      break;
    }
    case telegram_api::textSubscript::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textSubscript>(rich_text_ptr);
      type_ = Type::Subscript;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textSuperscript::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textSuperscript>(rich_text_ptr);
      type_ = Type::Superscript;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textMarked::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMarked>(rich_text_ptr);
      type_ = Type::Marked;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textPhone::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textPhone>(rich_text_ptr);
      type_ = Type::PhoneNumber;
      content_ = std::move(rich_text->phone_);
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textImage::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textImage>(rich_text_ptr);
      auto it = documents.find(rich_text->document_id_);
      if (it != documents.end()) {
        type_ = Type::Icon;
        document_file_id_ = it->second;
        Dimensions dimensions = get_dimensions(rich_text->w_, rich_text->h_, "textImage");
        content_ = PSTRING() << (dimensions.width * static_cast<uint32>(65536) + dimensions.height);
      } else {
        LOG(ERROR) << "Can't find document " << rich_text->document_id_;
      }
      break;
    }
    case telegram_api::textAnchor::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAnchor>(rich_text_ptr);
      type_ = Type::Anchor;
      content_ = std::move(rich_text->name_);
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textMath::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMath>(rich_text_ptr);
      type_ = Type::Math;
      content_ = std::move(rich_text->source_);
      break;
    }
    case telegram_api::textCustomEmoji::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textCustomEmoji>(rich_text_ptr);
      type_ = Type::CustomEmoji;
      custom_emoji_id_ = CustomEmojiId(rich_text->document_id_);
      content_ = std::move(rich_text->alt_);
      break;
    }
    case telegram_api::textSpoiler::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textSpoiler>(rich_text_ptr);
      type_ = Type::Spoiler;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textMention::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMention>(rich_text_ptr);
      type_ = Type::Mention;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textHashtag::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textHashtag>(rich_text_ptr);
      type_ = Type::Hashtag;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textBotCommand::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textBotCommand>(rich_text_ptr);
      type_ = Type::BotCommand;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textCashtag::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textCashtag>(rich_text_ptr);
      type_ = Type::Cashtag;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textAutoUrl::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAutoUrl>(rich_text_ptr);
      type_ = Type::AutoUrl;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textAutoEmail::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAutoEmail>(rich_text_ptr);
      type_ = Type::AutoEmailAddress;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textAutoPhone::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textAutoPhone>(rich_text_ptr);
      type_ = Type::AutoPhoneNumber;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textBankCard::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textBankCard>(rich_text_ptr);
      type_ = Type::BankCardNumber;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    case telegram_api::textMentionName::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textMentionName>(rich_text_ptr);
      type_ = Type::MentionName;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      user_id_ = UserId(rich_text->user_id_);
      if (!user_id_.is_valid()) {
        LOG(ERROR) << "Receive " << user_id_;
        user_id_ = UserId();
      }
      break;
    }
    case telegram_api::textDate::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textDate>(rich_text_ptr);
      auto formatted_date =
          FormattedDate(rich_text->date_, rich_text->relative_, rich_text->short_time_, rich_text->long_time_,
                        rich_text->short_date_, rich_text->long_date_, rich_text->day_of_week_);
      if (!formatted_date.is_valid()) {
        break;
      }
      type_ = Type::FormattedDate;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      date_ = std::move(formatted_date);
      break;
    }
    case telegram_api::textDiff::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textDiff>(rich_text_ptr);
      type_ = Type::Diff;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      texts_.emplace_back(std::move(rich_text->old_text_), documents);
      break;
    }
    case telegram_api::textButton::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textButton>(rich_text_ptr);
      type_ = Type::Button;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      button_style_ = RichButtonStyle(std::move(rich_text->style_));
      button_ = make_unique<InlineKeyboardButton>(
          telegram_api::make_object<telegram_api::keyboardInlineButton>(0, nullptr, "1", std::move(rich_text->type_)));
      break;
    }
    case telegram_api::textTonAddress::ID: {
      auto rich_text = telegram_api::move_object_as<telegram_api::textTonAddress>(rich_text_ptr);
      type_ = Type::TonAddress;
      texts_.emplace_back(std::move(rich_text->text_), documents);
      break;
    }
    default:
      UNREACHABLE();
  }
}

Result<RichText> RichText::get_rich_text(const Td *td, td_api::object_ptr<td_api::RichText> &&rich_text) {
  RichText result;
  if (rich_text == nullptr) {
    return std::move(result);
  }
  switch (rich_text->get_id()) {
    case td_api::richTextPlain::ID: {
      auto text = td_api::move_object_as<td_api::richTextPlain>(rich_text);
      if (!clean_input_string(text->text_)) {
        return Status::Error(400, "Rich text must be encoded in UTF-8");
      }
      result.type_ = Type::Plain;
      result.content_ = std::move(text->text_);
      break;
    }
    case td_api::richTextBold::ID: {
      auto text = td_api::move_object_as<td_api::richTextBold>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Bold;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextItalic::ID: {
      auto text = td_api::move_object_as<td_api::richTextItalic>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Italic;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextUnderline::ID: {
      auto text = td_api::move_object_as<td_api::richTextUnderline>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Underline;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextStrikethrough::ID: {
      auto text = td_api::move_object_as<td_api::richTextStrikethrough>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Strikethrough;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextSpoiler::ID: {
      auto text = td_api::move_object_as<td_api::richTextSpoiler>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Spoiler;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextSubscript::ID: {
      auto text = td_api::move_object_as<td_api::richTextSubscript>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Subscript;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextSuperscript::ID: {
      auto text = td_api::move_object_as<td_api::richTextSuperscript>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Superscript;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextMarked::ID: {
      auto text = td_api::move_object_as<td_api::richTextMarked>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Marked;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextDateTime::ID: {
      auto text = td_api::move_object_as<td_api::richTextDateTime>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      TRY_RESULT(formatted_date, FormattedDate::get_formatted_date(text->unix_time_, text->formatting_type_));
      result.type_ = Type::FormattedDate;
      result.texts_.push_back(std::move(t));
      result.date_ = std::move(formatted_date);
      break;
    }
    case td_api::richTextMention::ID: {
      auto text = td_api::move_object_as<td_api::richTextMention>(rich_text);
      return get_rich_text(td, std::move(text->text_));
    }
    case td_api::richTextHashtag::ID: {
      auto text = td_api::move_object_as<td_api::richTextHashtag>(rich_text);
      return get_rich_text(td, std::move(text->text_));
    }
    case td_api::richTextCashtag::ID: {
      auto text = td_api::move_object_as<td_api::richTextCashtag>(rich_text);
      return get_rich_text(td, std::move(text->text_));
    }
    case td_api::richTextBankCardNumber::ID: {
      auto text = td_api::move_object_as<td_api::richTextBankCardNumber>(rich_text);
      return get_rich_text(td, std::move(text->text_));
    }
    case td_api::richTextTonAddress::ID: {
      auto text = td_api::move_object_as<td_api::richTextTonAddress>(rich_text);
      return get_rich_text(td, std::move(text->text_));
    }
    case td_api::richTextBotCommand::ID: {
      auto text = td_api::move_object_as<td_api::richTextBotCommand>(rich_text);
      return get_rich_text(td, std::move(text->text_));
    }
    case td_api::richTextFixed::ID: {
      auto text = td_api::move_object_as<td_api::richTextFixed>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Fixed;
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextMentionName::ID: {
      auto text = td_api::move_object_as<td_api::richTextMentionName>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      auto user_id = UserId(text->user_id_);
      TRY_STATUS(td->user_manager_->get_input_user(user_id));
      result.type_ = Type::MentionName;
      result.texts_.push_back(std::move(t));
      result.user_id_ = user_id;
      break;
    }
    case td_api::richTextUrl::ID: {
      auto text = td_api::move_object_as<td_api::richTextUrl>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      if (!clean_input_string(text->url_)) {
        return Status::Error(400, "Rich text URL must be encoded in UTF-8");
      }
      result.type_ = Type::Url;
      result.content_ = std::move(text->url_);
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextEmailAddress::ID: {
      auto text = td_api::move_object_as<td_api::richTextEmailAddress>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      if (!clean_input_string(text->email_address_)) {
        return Status::Error(400, "Rich text email address must be encoded in UTF-8");
      }
      result.type_ = Type::EmailAddress;
      result.content_ = std::move(text->email_address_);
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextPhoneNumber::ID: {
      auto text = td_api::move_object_as<td_api::richTextPhoneNumber>(rich_text);
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      if (!clean_input_string(text->phone_number_)) {
        return Status::Error(400, "Rich text phone number must be encoded in UTF-8");
      }
      result.type_ = Type::PhoneNumber;
      result.content_ = std::move(text->phone_number_);
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextCustomEmoji::ID: {
      auto text = td_api::move_object_as<td_api::richTextCustomEmoji>(rich_text);
      if (!clean_input_string(text->alternative_text_)) {
        return Status::Error(400, "Custom emoji alternative text must be encoded in UTF-8");
      }
      auto custom_emoji_id_ = CustomEmojiId(text->custom_emoji_id_);
      if (!custom_emoji_id_.is_valid()) {
        return Status::Error(400, "Invalid custom emoji identifier specified");
      }
      result.type_ = Type::CustomEmoji;
      result.content_ = std::move(text->alternative_text_);
      result.custom_emoji_id_ = custom_emoji_id_;
      break;
    }
    case td_api::richTextIcon::ID:
      return Status::Error(400, "Block is not allowed");
    case td_api::richTextMathematicalExpression::ID: {
      auto text = td_api::move_object_as<td_api::richTextMathematicalExpression>(rich_text);
      if (!clean_input_string(text->expression_)) {
        return Status::Error(400, "Mathematical expression must be encoded in UTF-8");
      }
      result.type_ = Type::Math;
      result.content_ = std::move(text->expression_);
      break;
    }
    case td_api::richTextButton::ID: {
      auto text = td_api::move_object_as<td_api::richTextButton>(rich_text);
      if (text->button_ == nullptr) {
        return Status::Error(400, "Button must be non-empty");
      }
      TRY_RESULT(t, get_rich_text(td, std::move(text->button_->text_)));
      TRY_RESULT(button_,
                 InlineKeyboardButton::get_inline_keyboard_button(td_api::make_object<td_api::inlineKeyboardButton>(
                                                                      "1", 0, nullptr, std::move(text->button_->type_)),
                                                                  true));
      result.type_ = Type::Button;
      result.texts_.push_back(std::move(t));
      result.button_style_ = RichButtonStyle(std::move(text->button_->style_));
      result.button_ = make_unique<InlineKeyboardButton>(std::move(button_));
      break;
    }
    case td_api::richTextReference::ID: {
      auto text = td_api::move_object_as<td_api::richTextReference>(rich_text);
      if (!clean_input_string(text->name_)) {
        return Status::Error(400, "Reference name must be encoded in UTF-8");
      }
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Anchor;
      result.content_ = std::move(text->name_);
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextReferenceLink::ID: {
      auto text = td_api::move_object_as<td_api::richTextReferenceLink>(rich_text);
      if (!clean_input_string(text->reference_name_)) {
        return Status::Error(400, "Reference name must be encoded in UTF-8");
      }
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Url;
      result.content_ = PSTRING() << '#' << url_encode(text->reference_name_);
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTextAnchor::ID: {
      auto text = td_api::move_object_as<td_api::richTextAnchor>(rich_text);
      if (!clean_input_string(text->name_)) {
        return Status::Error(400, "Anchor name must be encoded in UTF-8");
      }
      result.type_ = Type::Anchor;
      result.content_ = std::move(text->name_);
      result.texts_.push_back(RichText());
      break;
    }
    case td_api::richTextAnchorLink::ID: {
      auto text = td_api::move_object_as<td_api::richTextAnchorLink>(rich_text);
      if (!clean_input_string(text->anchor_name_)) {
        return Status::Error(400, "Anchor name must be encoded in UTF-8");
      }
      TRY_RESULT(t, get_rich_text(td, std::move(text->text_)));
      result.type_ = Type::Url;
      result.content_ = PSTRING() << '#' << url_encode(text->anchor_name_);
      result.texts_.push_back(std::move(t));
      break;
    }
    case td_api::richTexts::ID: {
      auto text = td_api::move_object_as<td_api::richTexts>(rich_text);
      if (text->texts_.empty()) {
        break;
      }
      vector<RichText> texts_;
      for (auto &subtext : text->texts_) {
        TRY_RESULT(t, get_rich_text(td, std::move(subtext)));
        texts_.push_back(std::move(t));
      }
      result.type_ = Type::Concatenation;
      result.texts_ = std::move(texts_);
      break;
    }
    case td_api::richTextDiff::ID:
      return Status::Error(400, "Block is not allowed");
    default:
      UNREACHABLE();
      break;
  }
  return std::move(result);
}

RichText RichText::create_plain(string text) {
  RichText result;
  result.content_ = std::move(text);
  return result;
}

RichText RichText::create_math(string source) {
  RichText result;
  result.type_ = Type::Math;
  result.content_ = std::move(source);
  return result;
}

bool RichText::empty() const {
  return type_ == Type::Plain && content_.empty();
}

void RichText::append_file_ids(const Td *td, vector<FileId> &file_ids) const {
  if (type_ == Type::Icon) {
    CHECK(document_file_id_.is_valid());
    Document(Document::Type::General, document_file_id_).append_file_ids(td, file_ids);
  } else {
    for (auto &text : texts_) {
      text.append_file_ids(td, file_ids);
    }
  }
}

void RichText::add_dependencies(Dependencies &dependencies) const {
  for (auto &text : texts_) {
    text.add_dependencies(dependencies);
  }
  dependencies.add(web_page_id_);
  dependencies.add(user_id_);
  if (button_ != nullptr) {
    button_->add_dependencies(dependencies);
  }
}

void RichText::for_each_rich_text(bool recurse_text, const std::function<void(const RichText &text)> &callback) const {
  if (recurse_text) {
    for (auto &text : texts_) {
      text.for_each_rich_text(recurse_text, callback);
    }
  }
  callback(*this);
}

string RichText::get_full_text() const {
  string result;
  for_each_rich_text(true, [&](const RichText &text) {
    if (text.type_ == Type::Plain || text.type_ == Type::Math || text.type_ == Type::CustomEmoji) {
      result += text.content_;
    }
  });
  return result;
}

string RichText::get_quote_text() const {
  switch (type_) {
    case Type::Plain:
    case Type::Math:
    case Type::CustomEmoji:
      return content_;
    case Type::Icon:
      return string();
    case Type::Diff:
      // only the new text can be quoted
      CHECK(!texts_.empty());
      return texts_[0].get_quote_text();
    default: {
      string result;
      for (auto &text : texts_) {
        result += text.get_quote_text();
      }
      return result;
    }
  }
}

RichText RichText::get_quote_fragment(size_t begin, size_t end) const {
  size_t pos = 0;
  return get_quote_fragment_impl(pos, begin, end);
}

RichText RichText::get_quote_fragment_impl(size_t &pos, size_t begin, size_t end) const {
  switch (type_) {
    case Type::Plain: {
      auto size = utf8_length(content_);
      auto from = max(begin, pos);
      auto to = min(end, pos + size);
      RichText result;
      if (from < to) {
        result.content_ = utf8_substr(Slice(content_), from - pos, to - from).str();
      }
      pos += size;
      return result;
    }
    case Type::Math:
    case Type::CustomEmoji: {
      auto size = utf8_length(content_);
      auto is_included = begin <= pos && pos + size <= end;
      pos += size;
      if (!is_included) {
        return {};
      }
      CloneWebPageBlockContext context;
      return clone(context);
    }
    case Type::Icon:
      // icons can't be quoted
      return {};
    case Type::Concatenation: {
      vector<RichText> texts;
      for (auto &text : texts_) {
        auto fragment = text.get_quote_fragment_impl(pos, begin, end);
        if (!fragment.empty()) {
          texts.push_back(std::move(fragment));
        }
      }
      if (texts.empty()) {
        return {};
      }
      RichText result;
      result.type_ = Type::Concatenation;
      result.texts_ = std::move(texts);
      return result;
    }
    case Type::Fixed:
    case Type::Url:
    case Type::EmailAddress:
    case Type::Marked:
    case Type::PhoneNumber:
    case Type::Anchor:
    case Type::Mention:
    case Type::Hashtag:
    case Type::Cashtag:
    case Type::BotCommand:
    case Type::AutoUrl:
    case Type::AutoEmailAddress:
    case Type::AutoPhoneNumber:
    case Type::BankCardNumber:
    case Type::MentionName:
    case Type::Button:
    case Type::TonAddress:
    case Type::Diff:
      // the formatting isn't supported in quotes, so only the text is quoted
      CHECK(!texts_.empty());
      return texts_[0].get_quote_fragment_impl(pos, begin, end);
    default: {
      if (texts_.size() != 1u) {
        LOG(ERROR) << "Receive rich text of type " << static_cast<int32>(type_) << " with " << texts_.size()
                   << " texts";
        for (auto &text : texts_) {
          pos += utf8_length(text.get_quote_text());
        }
        return {};
      }
      auto fragment = texts_[0].get_quote_fragment_impl(pos, begin, end);
      if (fragment.empty()) {
        return {};
      }
      CloneWebPageBlockContext context;
      auto result = clone(context, true);
      result.texts_.push_back(std::move(fragment));
      return result;
    }
  }
}

int32 RichText::get_index_mask() const {
  switch (type_) {
    case Type::Url:
      if (content_[0] == '#') {
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

void RichText::append_user_ids(vector<UserId> &user_ids) const {
  if (user_id_.is_valid()) {
    user_ids.push_back(user_id_);
  }
  if (button_ != nullptr) {
    auto button_user_id = button_->get_user_id();
    if (button_user_id.is_valid()) {
      user_ids.push_back(button_user_id);
    }
  }
}

void RichText::append_custom_emoji_ids(vector<CustomEmojiId> &custom_emoji_ids) const {
  if (custom_emoji_id_.is_valid()) {
    custom_emoji_ids.push_back(custom_emoji_id_);
  }
}

bool RichText::is_bot_command() const {
  return type_ == Type::BotCommand;
}

bool RichText::is_hashtag() const {
  return type_ == Type::Hashtag;
}

RichText RichText::clone(CloneWebPageBlockContext &context, bool skip_texts) const {
  RichText result;
  result.type_ = type_;
  result.content_ = content_;
  if (!skip_texts) {
    for (auto &text : texts_) {
      result.texts_.push_back(text.clone(context));
    }
  }
  result.document_file_id_ = document_file_id_;
  result.custom_emoji_id_ = custom_emoji_id_;
  result.web_page_id_ = web_page_id_;
  result.date_ = date_;
  result.user_id_ = user_id_;
  result.button_style_ = button_style_;
  if (button_ != nullptr) {
    if (context.dup_type_ == MessageContentDupType::Forward && !button_->get_forward_text().empty()) {
      CHECK(result.texts_.size() == 1u);
      result.texts_[0] = create_plain(button_->get_forward_text());
    }
    result.button_ = make_unique<InlineKeyboardButton>(
        button_->clone(context.dialog_id_, context.dup_type_, context.is_via_bot_, true));
  }
  return result;
}

telegram_api::object_ptr<telegram_api::RichText> RichText::get_input_rich_text(
    GetInputPageBlockContext &context) const {
  switch (type_) {
    case Type::Plain:
      if (content_.empty()) {
        return telegram_api::make_object<telegram_api::textEmpty>();
      }
      return telegram_api::make_object<telegram_api::textPlain>(content_);
    case Type::Bold:
      return telegram_api::make_object<telegram_api::textBold>(texts_[0].get_input_rich_text(context));
    case Type::Italic:
      return telegram_api::make_object<telegram_api::textItalic>(texts_[0].get_input_rich_text(context));
    case Type::Underline:
      return telegram_api::make_object<telegram_api::textUnderline>(texts_[0].get_input_rich_text(context));
    case Type::Strikethrough:
      return telegram_api::make_object<telegram_api::textStrike>(texts_[0].get_input_rich_text(context));
    case Type::Fixed:
      return telegram_api::make_object<telegram_api::textFixed>(texts_[0].get_input_rich_text(context));
    case Type::Url:
      return telegram_api::make_object<telegram_api::textUrl>(texts_[0].get_input_rich_text(context), content_, 0);
    case Type::EmailAddress:
      return telegram_api::make_object<telegram_api::textEmail>(texts_[0].get_input_rich_text(context), content_);
    case Type::Concatenation:
      return telegram_api::make_object<telegram_api::textConcat>(get_input_rich_texts(texts_, context));
    case Type::Subscript:
      return telegram_api::make_object<telegram_api::textSubscript>(texts_[0].get_input_rich_text(context));
    case Type::Superscript:
      return telegram_api::make_object<telegram_api::textSuperscript>(texts_[0].get_input_rich_text(context));
    case Type::Marked:
      return telegram_api::make_object<telegram_api::textMarked>(texts_[0].get_input_rich_text(context));
    case Type::PhoneNumber:
      return telegram_api::make_object<telegram_api::textPhone>(texts_[0].get_input_rich_text(context), content_);
    case Type::Icon: {
      auto file_view = context.td_->file_manager_->get_file_view(document_file_id_);
      const auto *main_remote_location = file_view.get_main_remote_location();
      if (!file_view.is_encrypted() && main_remote_location != nullptr && !main_remote_location->is_web()) {
        context.documents_.push_back(main_remote_location->as_input_document());

        auto dimensions = to_integer<uint32>(content_);
        auto width = static_cast<int32>(dimensions / 65536);
        auto height = static_cast<int32>(dimensions % 65536);
        return telegram_api::make_object<telegram_api::textImage>(main_remote_location->get_id(), width, height);
      }
      LOG(ERROR) << "Can't create textImage for icon " << document_file_id_;
      return telegram_api::make_object<telegram_api::textEmpty>();
    }
    case Type::Anchor:
      return telegram_api::make_object<telegram_api::textAnchor>(texts_[0].get_input_rich_text(context), content_);
    case Type::Math:
      return telegram_api::make_object<telegram_api::textMath>(content_);
    case Type::CustomEmoji:
      return telegram_api::make_object<telegram_api::textCustomEmoji>(custom_emoji_id_.get(), content_);
    case Type::Spoiler:
      return telegram_api::make_object<telegram_api::textSpoiler>(texts_[0].get_input_rich_text(context));
    case Type::Mention:
      return texts_[0].get_input_rich_text(context);
    case Type::Hashtag:
      return texts_[0].get_input_rich_text(context);
    case Type::Cashtag:
      return texts_[0].get_input_rich_text(context);
    case Type::BotCommand:
      return texts_[0].get_input_rich_text(context);
    case Type::AutoUrl:
      return texts_[0].get_input_rich_text(context);
    case Type::AutoEmailAddress:
      return texts_[0].get_input_rich_text(context);
    case Type::AutoPhoneNumber:
      return texts_[0].get_input_rich_text(context);
    case Type::FormattedDate:
      return date_.get_input_text_date(texts_[0].get_input_rich_text(context));
    case Type::BankCardNumber:
      return texts_[0].get_input_rich_text(context);
    case Type::MentionName:
      // input users will be added separately
      return telegram_api::make_object<telegram_api::textMentionName>(texts_[0].get_input_rich_text(context),
                                                                      user_id_.get());
    case Type::Diff:
      CHECK(texts_.size() == 2u);
      return telegram_api::make_object<telegram_api::textDiff>(texts_[0].get_input_rich_text(context),
                                                               texts_[1].get_input_rich_text(context));
    case Type::Button: {
      auto style = button_style_.get_input_rich_button_style();
      CHECK(button_ != nullptr);
      auto input_button = button_->get_input_keyboard_inline_button(context.td_->user_manager_.get());
      return telegram_api::make_object<telegram_api::textButton>(0, texts_[0].get_input_rich_text(context),
                                                                 std::move(input_button->type_), std::move(style));
    }
    case Type::TonAddress:
      return texts_[0].get_input_rich_text(context);
    default:
      UNREACHABLE();
      return nullptr;
  }
}

td_api::object_ptr<td_api::RichText> RichText::get_rich_text_object(GetWebPageBlockObjectContext *context,
                                                                    bool allow_null) const {
  switch (type_) {
    case Type::Plain:
      if (allow_null && content_.empty()) {
        return nullptr;
      }
      return td_api::make_object<td_api::richTextPlain>(content_);
    case Type::Bold:
      return td_api::make_object<td_api::richTextBold>(texts_[0].get_rich_text_object(context));
    case Type::Italic:
      return td_api::make_object<td_api::richTextItalic>(texts_[0].get_rich_text_object(context));
    case Type::Underline:
      return td_api::make_object<td_api::richTextUnderline>(texts_[0].get_rich_text_object(context));
    case Type::Strikethrough:
      return td_api::make_object<td_api::richTextStrikethrough>(texts_[0].get_rich_text_object(context));
    case Type::Fixed:
      return td_api::make_object<td_api::richTextFixed>(texts_[0].get_rich_text_object(context));
    case Type::Url:
      if (begins_with(content_, context->base_url_) && content_[context->base_url_.size()] == '#') {
        if (context->is_first_pass_) {
          context->has_anchor_urls_ = true;
        } else {
          auto anchor = Slice(content_).substr(context->base_url_.size() + 1);
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
                return td_api::make_object<td_api::richTextAnchorLink>(texts_[0].get_rich_text_object(context),
                                                                       anchor.str(), content_);
              } else {
                return td_api::make_object<td_api::richTextReferenceLink>(texts_[0].get_rich_text_object(context),
                                                                          anchor.str(), content_);
              }
            }
          }
        }
      }
      if (!context->real_url_rhash_.empty() && get_url_host(content_) == context->real_url_host_) {
        if (context->is_first_pass_) {
          context->has_anchor_urls_ = true;
        } else {
          return td_api::make_object<td_api::richTextUrl>(
              texts_[0].get_rich_text_object(context),
              LinkManager::get_instant_view_link(content_, context->real_url_rhash_), true);
        }
      }
      return td_api::make_object<td_api::richTextUrl>(texts_[0].get_rich_text_object(context), content_,
                                                      web_page_id_.is_valid());
    case Type::EmailAddress:
      return td_api::make_object<td_api::richTextEmailAddress>(texts_[0].get_rich_text_object(context), content_);
    case Type::Concatenation:
      return td_api::make_object<td_api::richTexts>(get_rich_texts_object(texts_, context));
    case Type::Subscript:
      return td_api::make_object<td_api::richTextSubscript>(texts_[0].get_rich_text_object(context));
    case Type::Superscript:
      return td_api::make_object<td_api::richTextSuperscript>(texts_[0].get_rich_text_object(context));
    case Type::Marked:
      return td_api::make_object<td_api::richTextMarked>(texts_[0].get_rich_text_object(context));
    case Type::PhoneNumber:
      return td_api::make_object<td_api::richTextPhoneNumber>(texts_[0].get_rich_text_object(context), content_);
    case Type::Icon: {
      auto dimensions = to_integer<uint32>(content_);
      auto width = static_cast<int32>(dimensions / 65536);
      auto height = static_cast<int32>(dimensions % 65536);
      return td_api::make_object<td_api::richTextIcon>(
          context->td_->documents_manager_->get_document_object(document_file_id_, PhotoFormat::Jpeg), width, height);
    }
    case Type::Anchor: {
      if (context->is_first_pass_) {
        context->anchors_.emplace(Slice(content_), texts_[0].empty() ? nullptr : &texts_[0]);
      }
      if (texts_[0].empty()) {
        return td_api::make_object<td_api::richTextAnchor>(content_);
      }
      return td_api::make_object<td_api::richTextReference>(content_, texts_[0].get_rich_text_object(context));
    }
    case Type::Math:
      return td_api::make_object<td_api::richTextMathematicalExpression>(content_);
    case Type::CustomEmoji:
      return td_api::make_object<td_api::richTextCustomEmoji>(custom_emoji_id_.get(), content_);
    case Type::Spoiler:
      return td_api::make_object<td_api::richTextSpoiler>(texts_[0].get_rich_text_object(context));
    case Type::Mention:
      return td_api::make_object<td_api::richTextMention>(texts_[0].get_rich_text_object(context),
                                                          trim_first(texts_[0].get_full_text(), '@'));
    case Type::Hashtag:
      return td_api::make_object<td_api::richTextHashtag>(texts_[0].get_rich_text_object(context),
                                                          trim_first(texts_[0].get_full_text(), '#'));
    case Type::Cashtag:
      return td_api::make_object<td_api::richTextCashtag>(texts_[0].get_rich_text_object(context),
                                                          trim_first(texts_[0].get_full_text(), '$'));
    case Type::BotCommand:
      if (context->skip_bot_commands_) {
        return texts_[0].get_rich_text_object(context);
      }
      return td_api::make_object<td_api::richTextBotCommand>(texts_[0].get_rich_text_object(context),
                                                             trim_first(texts_[0].get_full_text(), '/'));
    case Type::AutoUrl:
      return td_api::make_object<td_api::richTextUrl>(texts_[0].get_rich_text_object(context),
                                                      texts_[0].get_full_text(), false);
    case Type::AutoEmailAddress:
      return td_api::make_object<td_api::richTextEmailAddress>(texts_[0].get_rich_text_object(context),
                                                               texts_[0].get_full_text());
    case Type::AutoPhoneNumber:
      return td_api::make_object<td_api::richTextPhoneNumber>(texts_[0].get_rich_text_object(context),
                                                              texts_[0].get_full_text());
    case Type::FormattedDate:
      return td_api::make_object<td_api::richTextDateTime>(texts_[0].get_rich_text_object(context), date_.get_date(),
                                                           date_.get_date_time_formatting_type_object());
    case Type::BankCardNumber:
      return td_api::make_object<td_api::richTextBankCardNumber>(texts_[0].get_rich_text_object(context),
                                                                 texts_[0].get_full_text());
    case Type::MentionName:
      return td_api::make_object<td_api::richTextMentionName>(
          texts_[0].get_rich_text_object(context),
          context->td_->user_manager_->get_user_id_object(user_id_, "richTextMentionName"));
    case Type::Diff:
      CHECK(texts_.size() == 2u);
      return td_api::make_object<td_api::richTextDiff>(texts_[0].get_rich_text_object(context),
                                                       texts_[1].get_rich_text_object(context));
    case Type::Button: {
      CHECK(button_ != nullptr);
      auto button_object = button_->get_inline_keyboard_button_object(context->td_->user_manager_.get());
      return td_api::make_object<td_api::richTextButton>(td_api::make_object<td_api::inlineButton>(
          texts_[0].get_rich_text_object(context), button_style_.get_button_style_object(),
          std::move(button_object->type_)));
    }
    case Type::TonAddress:
      return td_api::make_object<td_api::richTextTonAddress>(texts_[0].get_rich_text_object(context),
                                                             texts_[0].get_full_text());
    default:
      UNREACHABLE();
      return nullptr;
  }
}

bool operator==(const RichText &lhs, const RichText &rhs) {
  return lhs.type_ == rhs.type_ && lhs.content_ == rhs.content_ && lhs.texts_ == rhs.texts_ &&
         lhs.document_file_id_ == rhs.document_file_id_ && lhs.custom_emoji_id_ == rhs.custom_emoji_id_ &&
         lhs.web_page_id_ == rhs.web_page_id_ && lhs.date_ == rhs.date_ && lhs.user_id_ == rhs.user_id_ &&
         lhs.button_style_ == rhs.button_style_ && lhs.button_ == rhs.button_;
}

}  // namespace td
