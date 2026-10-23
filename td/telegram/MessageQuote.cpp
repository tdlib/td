//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageQuote.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/RichMessage.h"
#include "td/telegram/Td.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/WebPageBlock.h"

#include "td/utils/algorithm.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Slice.h"
#include "td/utils/utf8.h"

namespace td {

MessageQuote::~MessageQuote() = default;

static vector<RichText> get_quote_rich_texts(
    vector<telegram_api::object_ptr<telegram_api::RichText>> &&rich_text_ptrs) {
  vector<RichText> result;
  for (auto &rich_text_ptr : rich_text_ptrs) {
    RichText rich_text(std::move(rich_text_ptr), FlatHashMap<int64, FileId>());
    if (rich_text.remove_unallowed_quote_entities()) {
      LOG(ERROR) << "Receive a rich quote with unexpected entities";
    }
    if (!rich_text.empty()) {
      result.push_back(std::move(rich_text));
    }
  }
  return result;
}

MessageQuote::MessageQuote(Td *td,
                           telegram_api::object_ptr<telegram_api::inputReplyToMessage> &input_reply_to_message) {
  CHECK(input_reply_to_message != nullptr);
  rich_texts_ = get_quote_rich_texts(std::move(input_reply_to_message->quote_rich_));
  if (input_reply_to_message->quote_text_.empty() && rich_texts_.empty()) {
    return;
  }
  text_ = get_formatted_text(td->user_manager_.get(), std::move(input_reply_to_message->quote_text_),
                             std::move(input_reply_to_message->quote_entities_), true, false, "inputReplyToMessage");
  if (remove_unallowed_quote_entities(text_)) {
    LOG(ERROR) << "Receive a quote with unexpected entities";
  }
  position_ = max(0, input_reply_to_message->quote_offset_);
}

MessageQuote::MessageQuote(Td *td, telegram_api::object_ptr<telegram_api::messageReplyHeader> &reply_header) {
  CHECK(reply_header != nullptr);
  rich_texts_ = get_quote_rich_texts(std::move(reply_header->quote_rich_));
  if (reply_header->quote_text_.empty() && rich_texts_.empty()) {
    return;
  }
  text_ = get_formatted_text(td->user_manager_.get(), std::move(reply_header->quote_text_),
                             std::move(reply_header->quote_entities_), true, false, "messageReplyHeader");
  if (remove_unallowed_quote_entities(text_)) {
    LOG(ERROR) << "Receive a quote with unexpected entities";
  }
  position_ = max(0, reply_header->quote_offset_);
  is_manual_ = reply_header->quote_;
}

MessageQuote::MessageQuote(Td *td, td_api::object_ptr<td_api::inputTextQuote> quote) {
  if (quote == nullptr) {
    return;
  }
  if (!quote->rich_texts_.empty()) {
    for (auto &rich_text_object : quote->rich_texts_) {
      auto r_rich_text = RichText::get_rich_text(td, std::move(rich_text_object));
      if (r_rich_text.is_error()) {
        rich_texts_.clear();
        return;
      }
      auto rich_text = r_rich_text.move_as_ok();
      rich_text = rich_text.get_quote_fragment(0, 1000000000);
      if (!rich_text.empty()) {
        rich_texts_.push_back(std::move(rich_text));
      }
    }
    if (rich_texts_.empty()) {
      return;
    }
    position_ = quote->position_;
    if (position_ < 0 || position_ > 1000000) {  // some unreasonably big bound
      position_ = 0;
    }
    return;
  }

  int32 ltrim_count = 0;
  auto r_text = get_formatted_text(td, td->dialog_manager_->get_my_dialog_id(), std::move(quote->text_),
                                   td->auth_manager_->is_bot(), true, true, false, &ltrim_count);
  if (!r_text.is_ok() || r_text.ok().is_empty()) {
    return;
  }
  text_ = r_text.move_as_ok();
  position_ = quote->position_;
  if (0 <= position_ && position_ <= 1000000) {  // some unreasonably big bound
    position_ += ltrim_count;
  } else {
    position_ = 0;
  }
}

bool MessageQuote::remove_unallowed_quote_entities_from_rich_texts(vector<RichText> &rich_texts) {
  bool is_changed = false;
  for (auto &rich_text : rich_texts) {
    if (rich_text.remove_unallowed_quote_entities()) {
      is_changed = true;
    }
  }
  if (td::remove_if(rich_texts, [](const RichText &rich_text) { return rich_text.empty(); })) {
    is_changed = true;
  }
  return is_changed;
}

MessageQuote MessageQuote::clone(Td *td, DialogId dialog_id, MessageContentDupType dup_type,
                                 bool ignore_is_manual) const {
  CloneWebPageBlockContext context;
  context.td_ = td;
  context.dialog_id_ = dialog_id;
  context.dup_type_ = dup_type;
  auto rich_texts = transform(rich_texts_, [&context](const RichText &rich_text) { return rich_text.clone(context); });
  return MessageQuote(FormattedText(text_), std::move(rich_texts), position_, ignore_is_manual ? true : is_manual_);
}

MessageQuote MessageQuote::create_automatic_quote(Td *td, FormattedText &&text) {
  truncate_formatted_text(
      text, static_cast<size_t>(td->option_manager_->get_option_integer("message_reply_quote_length_max")));
  return MessageQuote(std::move(text), {}, 0, false);
}

MessageQuote MessageQuote::create_automatic_quote(Td *td, const RichMessage &rich_message) {
  constexpr size_t MAX_QUOTE_RICH_TEXTS = 32;  // server-side limit
  auto max_length = static_cast<size_t>(td->option_manager_->get_option_integer("message_reply_quote_length_max"));
  vector<RichText> rich_texts;
  size_t length = 0;
  bool is_finished = false;
  rich_message.for_each_quote_text([&](const RichText &rich_text) {
    if (is_finished) {
      return;
    }
    if (rich_texts.size() >= MAX_QUOTE_RICH_TEXTS || length >= max_length) {
      is_finished = true;
      return;
    }
    auto text = rich_text.get_quote_text();
    if (text.empty()) {
      return;
    }
    auto end = utf8_length(text);
    if (length + end > max_length) {
      end = max_length - length;
      is_finished = true;
    }
    auto fragment = rich_text.get_quote_fragment(0, end);
    if (fragment.empty()) {
      return;
    }
    length += utf8_length(fragment.get_quote_text());
    rich_texts.push_back(std::move(fragment));
  });
  return MessageQuote(FormattedText(), std::move(rich_texts), 0, false);
}

size_t MessageQuote::get_text_size() const {
  auto size = text_.text.size();
  for (auto &rich_text : rich_texts_) {
    size += rich_text.get_quote_text().size();
  }
  return size;
}

int MessageQuote::need_quote_changed_warning(const MessageQuote &old_quote, const MessageQuote &new_quote) {
  if (old_quote.position_ != new_quote.position_ &&
      max(old_quote.position_, new_quote.position_) <
          static_cast<int32>(min(old_quote.get_text_size(), new_quote.get_text_size()))) {
    // quote position can't change
    return 1;
  }
  if (old_quote.is_manual_ != new_quote.is_manual_) {
    // quote manual property can't change
    return 1;
  }
  if (old_quote.text_ != new_quote.text_ || old_quote.rich_texts_ != new_quote.rich_texts_) {
    if (old_quote.is_manual_) {
      return 1;
    }
    // automatic quote can change if the original message was edited
    return -1;
  }
  return 0;
}

void MessageQuote::add_dependencies(Dependencies &dependencies) const {
  add_formatted_text_dependencies(dependencies, &text_);  // just in case
  for (auto &rich_text : rich_texts_) {
    rich_text.add_dependencies(dependencies);
  }
}

void MessageQuote::update_input_reply_to_message(Td *td,
                                                 telegram_api::inputReplyToMessage *input_reply_to_message) const {
  CHECK(input_reply_to_message != nullptr);
  if (is_empty()) {
    return;
  }
  CHECK(is_manual_);
  if (rich_texts_.empty()) {
    input_reply_to_message->quote_text_ = text_.text;
    input_reply_to_message->quote_entities_ =
        get_input_message_entities(td->user_manager_.get(), text_.entities, "update_input_reply_to_message");
  } else {
    GetInputPageBlockContext context;
    context.td_ = td;
    input_reply_to_message->quote_rich_ = transform(
        rich_texts_, [&context](const RichText &rich_text) { return rich_text.get_input_rich_text(context); });
  }
  if (position_ != 0) {
    input_reply_to_message->flags_ |= telegram_api::inputReplyToMessage::QUOTE_OFFSET_MASK;
    input_reply_to_message->quote_offset_ = position_;
  }
}

// only for draft messages
td_api::object_ptr<td_api::inputTextQuote> MessageQuote::get_input_text_quote_object(Td *td) const {
  if (is_empty()) {
    return nullptr;
  }
  CHECK(is_manual_);
  return td_api::make_object<td_api::inputTextQuote>(
      get_formatted_text_object(td->user_manager_.get(), text_, true, -1), get_rich_texts_object(td), position_);
}

td_api::object_ptr<td_api::textQuote> MessageQuote::get_text_quote_object(Td *td) const {
  if (is_empty()) {
    return nullptr;
  }
  return td_api::make_object<td_api::textQuote>(get_formatted_text_object(td->user_manager_.get(), text_, true, -1),
                                                get_rich_texts_object(td), position_, is_manual_);
}

vector<td_api::object_ptr<td_api::RichText>> MessageQuote::get_rich_texts_object(Td *td) const {
  GetWebPageBlockObjectContext context;
  context.td_ = td;
  context.skip_bot_commands_ = true;
  return transform(rich_texts_,
                   [&context](const RichText &rich_text) { return rich_text.get_rich_text_object(&context); });
}

bool operator==(const MessageQuote &lhs, const MessageQuote &rhs) {
  return lhs.text_ == rhs.text_ && lhs.rich_texts_ == rhs.rich_texts_ && lhs.position_ == rhs.position_ &&
         lhs.is_manual_ == rhs.is_manual_;
}

bool operator!=(const MessageQuote &lhs, const MessageQuote &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageQuote &quote) {
  if (!quote.is_empty()) {
    string_builder << " with " << quote.text_.text.size() << (!quote.is_manual_ ? " automatically" : "")
                   << " quoted bytes";
    if (!quote.rich_texts_.empty()) {
      string_builder << " and " << quote.rich_texts_.size() << " quoted rich texts";
    }
    if (quote.position_ != 0) {
      string_builder << " at position " << quote.position_;
    }
  }
  return string_builder;
}

int32 MessageQuote::search_quote(FormattedText &&text, FormattedText &&quote, int32 quote_position) {
  auto process_quote_entities = [](FormattedText &text, int32 length) {
    remove_unallowed_quote_entities(text);
    td::remove_if(text.entities, [length](const MessageEntity &entity) {
      if (entity.offset < 0 || entity.offset >= length) {
        return true;
      }
      if (entity.length <= 0 || entity.length > length - entity.offset) {
        return true;
      }
      return false;
    });
    remove_empty_entities(text.entities);
    fix_entities(text.entities);
    remove_empty_entities(text.entities);
  };
  auto length = narrow_cast<int32>(utf8_utf16_length(text.text));
  auto quote_length = narrow_cast<int32>(utf8_utf16_length(quote.text));
  if (quote_length == 0 || quote_length > length) {
    return -1;
  }
  process_quote_entities(text, length);
  process_quote_entities(quote, quote_length);

  quote_position = clamp(quote_position, 0, length - 1);
  vector<size_t> byte_positions;
  byte_positions.reserve(length);
  for (size_t i = 0; i < text.text.size(); i++) {
    auto c = static_cast<unsigned char>(text.text[i]);
    if (is_utf8_character_first_code_unit(c)) {
      byte_positions.push_back(i);
      if (c >= 0xf0) {  // >= 4 bytes in symbol => surrogate pair
        byte_positions.push_back(string::npos);
      }
    }
  }
  CHECK(byte_positions.size() == static_cast<size_t>(length));
  auto check_position = [&text, &quote, &byte_positions, length, quote_length](int32 position) {
    if (position < 0 || position > length - quote_length) {
      return false;
    }
    auto byte_position = byte_positions[position];
    if (byte_position == string::npos || text.text[byte_position] != quote.text[0] ||
        Slice(text.text).substr(byte_position, quote.text.size()) != quote.text) {
      return false;
    }
    return true;
  };
  for (int32 i = 0; quote_position - i >= 0 || quote_position + i + 1 <= length - quote_length; i++) {
    if (check_position(quote_position - i)) {
      return quote_position - i;
    }
    if (check_position(quote_position + i + 1)) {
      return quote_position + i + 1;
    }
  }
  return -1;
}

}  // namespace td
