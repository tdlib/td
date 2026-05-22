//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class WebDomainException {
  string domain_;
  string url_;
  string title_;
  CustomEmojiId favicon_custom_emoji_id_;

  friend bool operator==(const WebDomainException &lhs, const WebDomainException &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const WebDomainException &web_domain_exception);

 public:
  WebDomainException() = default;

  explicit WebDomainException(telegram_api::object_ptr<telegram_api::webDomainException> &&web_domain_exception);

  bool has_domain(const string &domain) const {
    return domain_ == domain;
  }

  td_api::object_ptr<td_api::webDomainException> get_web_domain_exception_object() const;

  static vector<WebDomainException> get_web_domain_exceptions(
      vector<telegram_api::object_ptr<telegram_api::webDomainException>> &&web_domain_exceptions);

  static vector<td_api::object_ptr<td_api::webDomainException>> get_web_domain_exceptions_object(
      const vector<WebDomainException> &web_domain_exceptions);

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const WebDomainException &lhs, const WebDomainException &rhs);

bool operator!=(const WebDomainException &lhs, const WebDomainException &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const WebDomainException &web_domain_exception);

}  // namespace td
