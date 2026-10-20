//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageContentDupType.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/RichText.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class Dependencies;
class RichMessage;
class Td;

class MessageQuote {
  FormattedText text_;
  vector<RichText> rich_texts_;
  int32 position_ = 0;
  bool is_manual_ = true;

  vector<td_api::object_ptr<td_api::RichText>> get_rich_texts_object(Td *td) const;

  friend bool operator==(const MessageQuote &lhs, const MessageQuote &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageQuote &quote);

 public:
  MessageQuote() = default;
  MessageQuote(const MessageQuote &) = delete;
  MessageQuote &operator=(const MessageQuote &) = delete;
  MessageQuote(MessageQuote &&) = default;
  MessageQuote &operator=(MessageQuote &&) = default;
  ~MessageQuote();

  MessageQuote(FormattedText &&text, vector<RichText> &&rich_texts, int32 position, bool is_manual = true)
      : text_(std::move(text)), rich_texts_(std::move(rich_texts)), position_(max(0, position)), is_manual_(is_manual) {
    remove_unallowed_quote_entities(text_);
  }

  MessageQuote(Td *td, telegram_api::object_ptr<telegram_api::inputReplyToMessage> &input_reply_to_message);

  MessageQuote(Td *td, telegram_api::object_ptr<telegram_api::messageReplyHeader> &reply_header);

  MessageQuote(Td *td, td_api::object_ptr<td_api::inputTextQuote> quote);

  static MessageQuote create_automatic_quote(Td *td, FormattedText &&text);

  static MessageQuote create_automatic_quote(Td *td, const RichMessage &rich_message);

  static int need_quote_changed_warning(const MessageQuote &old_quote, const MessageQuote &new_quote);

  static int32 search_quote(FormattedText &&text, FormattedText &&quote, int32 quote_position);

  bool is_empty() const {
    return text_.is_empty() && rich_texts_.empty();
  }

  MessageQuote clone(Td *td, DialogId dialog_id, MessageContentDupType dup_type, bool ignore_is_manual = false) const;

  void add_dependencies(Dependencies &dependencies) const;

  void update_input_reply_to_message(Td *td, telegram_api::inputReplyToMessage *input_reply_to_message) const;

  td_api::object_ptr<td_api::inputTextQuote> get_input_text_quote_object(Td *td) const;

  td_api::object_ptr<td_api::textQuote> get_text_quote_object(Td *td) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const MessageQuote &lhs, const MessageQuote &rhs);

bool operator!=(const MessageQuote &lhs, const MessageQuote &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const MessageQuote &quote);

}  // namespace td
