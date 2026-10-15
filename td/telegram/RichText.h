//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/FormattedDate.h"
#include "td/telegram/InlineKeyboardButton.h"
#include "td/telegram/RichButtonStyle.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"
#include "td/telegram/WebPageId.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Status.h"

#include <functional>

namespace td {

struct CloneWebPageBlockContext;
class Dependencies;
struct GetInputPageBlockContext;
struct GetWebPageBlockObjectContext;
class Td;

class RichText {
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
    MentionName,
    Diff,
    Button,
    TonAddress
  };
  Type type_ = Type::Plain;
  string content_;
  vector<RichText> texts_;
  FileId document_file_id_;
  CustomEmojiId custom_emoji_id_;
  WebPageId web_page_id_;
  FormattedDate date_;
  UserId user_id_;
  RichButtonStyle button_style_;
  unique_ptr<InlineKeyboardButton> button_;

  RichText get_quote_fragment_impl(size_t &pos, size_t begin, size_t end) const;

 public:
  RichText() = default;
  RichText(const RichText &) = delete;
  RichText &operator=(const RichText &) = delete;
  RichText(RichText &&) = default;
  RichText &operator=(RichText &&) = default;
  ~RichText() = default;

  RichText(telegram_api::object_ptr<telegram_api::RichText> &&rich_text_ptr,
           const FlatHashMap<int64, FileId> &documents);

  static Result<RichText> get_rich_text(const Td *td, td_api::object_ptr<td_api::RichText> &&rich_text);

  static RichText create_plain(string text);

  static RichText create_math(string source);

  bool empty() const;

  void append_file_ids(const Td *td, vector<FileId> &file_ids) const;

  void add_dependencies(Dependencies &dependencies) const;

  void for_each_rich_text(bool recurse_text, const std::function<void(const RichText &text)> &callback) const;

  string get_full_text() const;

  // returns text of the rich text as used in quotes
  string get_quote_text() const;

  // returns part of the rich text between begin and end UTF-8 characters of its quote text, which can be used in a quote
  RichText get_quote_fragment(size_t begin, size_t end) const;

  int32 get_index_mask() const;

  void append_user_ids(vector<UserId> &user_ids) const;

  void append_custom_emoji_ids(vector<CustomEmojiId> &custom_emoji_ids) const;

  bool is_bot_command() const;

  bool is_hashtag() const;

  RichText clone(CloneWebPageBlockContext &context, bool skip_texts = false) const;

  telegram_api::object_ptr<telegram_api::RichText> get_input_rich_text(GetInputPageBlockContext &context) const;

  td_api::object_ptr<td_api::RichText> get_rich_text_object(GetWebPageBlockObjectContext *context,
                                                            bool allow_null = false) const;

  friend bool operator==(const RichText &lhs, const RichText &rhs);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const RichText &lhs, const RichText &rhs);

inline bool operator!=(const RichText &lhs, const RichText &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
