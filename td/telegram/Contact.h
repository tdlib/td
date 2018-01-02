//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/SecretInputMedia.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"
#include "td/utils/tl_helpers.h"

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include <functional>
#include <tuple>

namespace td {

class Contact {
  string phone_number_;
  string first_name_;
  string last_name_;
  UserId user_id_;

  friend bool operator==(const Contact &lhs, const Contact &rhs);
  friend bool operator!=(const Contact &lhs, const Contact &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const Contact &contact);

  friend struct ContactEqual;
  friend struct ContactHash;

 public:
  Contact() = default;

  Contact(string phone_number, string first_name, string last_name, int32 user_id);

  void set_user_id(UserId user_id);

  UserId get_user_id() const;

  string get_phone_number() const;

  tl_object_ptr<td_api::contact> get_contact_object() const;

  tl_object_ptr<telegram_api::inputMediaContact> get_input_media_contact() const;

  SecretInputMedia get_secret_input_media_contact() const;

  tl_object_ptr<telegram_api::inputPhoneContact> get_input_phone_contact(int64 client_id) const;

  // TODO very strange function
  tl_object_ptr<telegram_api::inputBotInlineMessageMediaContact> get_input_bot_inline_message_media_contact(
      int32 flags, tl_object_ptr<telegram_api::ReplyMarkup> &&reply_markup) const;

  template <class StorerT>
  void store(StorerT &storer) const {
    using td::store;
    store(phone_number_, storer);
    store(first_name_, storer);
    store(last_name_, storer);
    store(user_id_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    using td::parse;
    parse(phone_number_, parser);
    parse(first_name_, parser);
    parse(last_name_, parser);
    parse(user_id_, parser);
  }
};

bool operator==(const Contact &lhs, const Contact &rhs);
bool operator!=(const Contact &lhs, const Contact &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const Contact &contact);

struct ContactEqual {
  std::size_t operator()(const Contact &lhs, const Contact &rhs) const {
    return std::tie(lhs.phone_number_, lhs.first_name_, lhs.last_name_) ==
           std::tie(rhs.phone_number_, rhs.first_name_, rhs.last_name_);
  }
};

struct ContactHash {
  std::size_t operator()(const Contact &contact) const {
    return (std::hash<std::string>()(contact.phone_number_) * 2023654985u +
            std::hash<std::string>()(contact.first_name_)) *
               2023654985u +
           std::hash<std::string>()(contact.last_name_);
  }
};

}  // namespace td
