//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"
#include "td/utils/StringBuilder.h"

#include "td/telegram/secret_api.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include <tuple>
#include <unordered_set>
#include <utility>

namespace td {

class ContactsManager;

class MessageEntity {
  tl_object_ptr<td_api::TextEntityType> get_text_entity_type_object() const;

 public:
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
    PhoneNumber
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
    return std::tie(offset, length, type) < std::tie(other.offset, other.length, other.type);
  }

  bool operator!=(const MessageEntity &rhs) const {
    return !(*this == rhs);
  }

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageEntity &message_entity);

struct FormattedText {
  string text;
  vector<MessageEntity> entities;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

inline bool operator==(const FormattedText &lhs, const FormattedText &rhs) {
  return lhs.text == rhs.text && lhs.entities == rhs.entities;
}

inline bool operator!=(const FormattedText &lhs, const FormattedText &rhs) {
  return !(lhs == rhs);
}

const std::unordered_set<Slice, SliceHash> &get_valid_short_usernames();

Result<vector<MessageEntity>> get_message_entities(const ContactsManager *contacts_manager,
                                                   const vector<tl_object_ptr<td_api::textEntity>> &input_entities);

vector<tl_object_ptr<td_api::textEntity>> get_text_entities_object(const vector<MessageEntity> &entities);

td_api::object_ptr<td_api::formattedText> get_formatted_text_object(const FormattedText &text);

vector<MessageEntity> find_entities(Slice text, bool skip_bot_commands, bool only_urls = false);

vector<Slice> find_mentions(Slice str);
vector<Slice> find_bot_commands(Slice str);
vector<Slice> find_hashtags(Slice str);
vector<Slice> find_cashtags(Slice str);
bool is_email_address(Slice str);
vector<std::pair<Slice, bool>> find_urls(Slice str);  // slice + is_email_address

string get_first_url(Slice text, const vector<MessageEntity> &entities);

Result<vector<MessageEntity>> parse_markdown(string &text);

Result<vector<MessageEntity>> parse_html(string &text);

vector<tl_object_ptr<telegram_api::MessageEntity>> get_input_message_entities(const ContactsManager *contacts_manager,
                                                                              const vector<MessageEntity> &entities,
                                                                              const char *source);

vector<tl_object_ptr<secret_api::MessageEntity>> get_input_secret_message_entities(
    const vector<MessageEntity> &entities);

vector<MessageEntity> get_message_entities(const ContactsManager *contacts_manager,
                                           vector<tl_object_ptr<telegram_api::MessageEntity>> &&server_entities,
                                           const char *source);

vector<MessageEntity> get_message_entities(vector<tl_object_ptr<secret_api::MessageEntity>> &&secret_entities);

// like clean_input_string but also validates entities
Status fix_formatted_text(string &text, vector<MessageEntity> &entities, bool allow_empty, bool skip_new_entities,
                          bool skip_bot_commands, bool for_draft) TD_WARN_UNUSED_RESULT;

}  // namespace td
