//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebDomainException.h"

#include "td/utils/algorithm.h"
#include "td/utils/logging.h"

namespace td {

WebDomainException::WebDomainException(
    telegram_api::object_ptr<telegram_api::webDomainException> &&web_domain_exception)
    : domain_(std::move(web_domain_exception->domain_))
    , url_(std::move(web_domain_exception->url_))
    , title_(std::move(web_domain_exception->title_))
    , favicon_custom_emoji_id_(web_domain_exception->favicon_) {
  if (!favicon_custom_emoji_id_.is_valid() && favicon_custom_emoji_id_ != CustomEmojiId()) {
    LOG(ERROR) << "Receive favicon " << favicon_custom_emoji_id_;
    favicon_custom_emoji_id_ = {};
  }
}

td_api::object_ptr<td_api::webDomainException> WebDomainException::get_web_domain_exception_object() const {
  return td_api::make_object<td_api::webDomainException>(url_, domain_, title_, favicon_custom_emoji_id_.get());
}

vector<WebDomainException> WebDomainException::get_web_domain_exceptions(
    vector<telegram_api::object_ptr<telegram_api::webDomainException>> &&web_domain_exceptions) {
  return transform(std::move(web_domain_exceptions),
                   [](telegram_api::object_ptr<telegram_api::webDomainException> &&web_domain_exception) {
                     return WebDomainException(std::move(web_domain_exception));
                   });
}

bool operator==(const WebDomainException &lhs, const WebDomainException &rhs) {
  return lhs.domain_ == rhs.domain_ && lhs.url_ == rhs.url_ && lhs.title_ == rhs.title_ &&
         lhs.favicon_custom_emoji_id_ == rhs.favicon_custom_emoji_id_;
}

bool operator!=(const WebDomainException &lhs, const WebDomainException &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const WebDomainException &web_domain_exception) {
  return string_builder << "WebDomainException[domain = " << web_domain_exception.domain_
                        << ", URL = " << web_domain_exception.url_ << ", title = " << web_domain_exception.title_
                        << ", favicon = " << web_domain_exception.favicon_custom_emoji_id_ << ']';
}

}  // namespace td
