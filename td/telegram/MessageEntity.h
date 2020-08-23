//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/Dependencies.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include <unordered_set>
#include <utility>

namespace td {

class ContactsManager;

class MessageEntity {
 public:
  // don't forget to update get_type_priority()
  enum class Type : int32 {
    Mention,
    Hashtag,
    BotCommand,
    Url,
    EmailAddress,
    Bold,
    Italic,
    Code,
    Pre,
    PreCode,
    TextUrl,
    MentionName,
    Cashtag,
    PhoneNumber,
    Underline,
    Strikethrough,
    BlockQuote,
    BankCardNumber,
    Size
  };
  Type type;
  int32 offset;
  int32 length;
  string argument;
  UserId user_id;

  MessageEntity() = default;

  MessageEntity(Type type, int32 offset, int32 length, string argument = "")
      : type(type), offset(offset), length(length), argument(std::move(argument)), user_id() {
  }
  MessageEntity(int32 offset, int32 length, UserId user_id)
      : type(Type::MentionName), offset(offset), length(length), argument(), user_id(user_id) {
  }

  tl_object_ptr<td_api::textEntity> get_text_entity_object() const;

  bool operator==(const MessageEntity &other) const {
    return offset == other.offset && length == other.length && type == other.type && argument == other.argument &&
           user_id == other.user_id;
  }

  bool operator<(const MessageEntity &other) const {
    if (offset != other.offset) {
      return offset < other.offset;
    }
    if (length != other.length) {
      return length > other.length;
    }
    auto priority = get_type_priority(type);
    auto other_priority = get_type_priority(other.type);
    return priority < other_priority;
  }

  bool operator!=(const MessageEntity &rhs) const {
    return !(*this == rhs);
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);

 private:
  tl_object_ptr<td_api::TextEntityType> get_text_entity_type_object() const;

  static int get_type_priority(Type type);
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageEntity::Type &message_entity_type);

StringBuilder &operator<<(StringBuilder &string_builder, const MessageEntity &message_entity);

struct FormattedText {
  string text;
  vector<MessageEntity> entities;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

StringBuilder &operator<<(StringBuilder &string_builder, const FormattedText &text);

inline bool operator==(const FormattedText &lhs, const FormattedText &rhs) {
  return lhs.text == rhs.text && lhs.entities == rhs.entities;
}

inline bool operator!=(const FormattedText &lhs, const FormattedText &rhs) {
  return !(lhs == rhs);
}

const std::unordered_set<Slice, SliceHash> &get_valid_short_usernames();

Result<vector<MessageEntity>> get_message_entities(const ContactsManager *contacts_manager,
                                                   vector<tl_object_ptr<td_api::textEntity>> &&input_entities,
                                                   bool allow_all = false);

vector<tl_object_ptr<td_api::textEntity>> get_text_entities_object(const vector<MessageEntity> &entities);

td_api::object_ptr<td_api::formattedText> get_formatted_text_object(const FormattedText &text);

vector<MessageEntity> find_entities(Slice text, bool skip_bot_commands, bool only_urls = false);

vector<Slice> find_mentions(Slice str);
vector<Slice> find_bot_commands(Slice str);
vector<Slice> find_hashtags(Slice str);
vector<Slice> find_cashtags(Slice str);
vector<Slice> find_bank_card_numbers(Slice str);
bool is_email_address(Slice str);
vector<std::pair<Slice, bool>> find_urls(Slice str);  // slice + is_email_address

string get_first_url(Slice text, const vector<MessageEntity> &entities);

Result<vector<MessageEntity>> parse_markdown(string &text);

Result<vector<MessageEntity>> parse_markdown_v2(string &text);

FormattedText parse_markdown_v3(FormattedText text);

FormattedText get_markdown_v3(FormattedText text);

Result<vector<MessageEntity>> parse_html(string &text);

vector<tl_object_ptr<telegram_api::MessageEntity>> get_input_message_entities(const ContactsManager *contacts_manager,
                                                                              const vector<MessageEntity> &entities,
                                                                              const char *source);

vector<tl_object_ptr<telegram_api::MessageEntity>> get_input_message_entities(const ContactsManager *contacts_manager,
                                                                              const FormattedText *text,
                                                                              const char *source);

vector<tl_object_ptr<secret_api::MessageEntity>> get_input_secret_message_entities(
    const vector<MessageEntity> &entities, int32 layer);

vector<MessageEntity> get_message_entities(const ContactsManager *contacts_manager,
                                           vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities,
                                           const char *source);

vector<MessageEntity> get_message_entities(vector<tl_object_ptr<secret_api::MessageEntity>> &&secret_entities);

// like clean_input_string but also validates entities
Status fix_formatted_text(string &text, vector<MessageEntity> &entities, bool allow_empty, bool skip_new_entities,
                          bool skip_bot_commands, bool for_draft) TD_WARN_UNUSED_RESULT;

FormattedText get_message_text(const ContactsManager *contacts_manager, string message_text,
                               vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities,
                               bool skip_new_entities, int32 send_date, bool from_album, const char *source);

td_api::object_ptr<td_api::formattedText> extract_input_caption(
    tl_object_ptr<td_api::InputMessageContent> &input_message_content);

Result<FormattedText> process_input_caption(const ContactsManager *contacts_manager, DialogId dialog_id,
                                            tl_object_ptr<td_api::formattedText> &&caption, bool is_bot);

void add_formatted_text_dependencies(Dependencies &dependencies, const FormattedText *text);

bool need_skip_bot_commands(const ContactsManager *contacts_manager, DialogId dialog_id, bool is_bot);

}  // namespace td
