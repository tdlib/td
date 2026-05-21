//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/WebBrowserSettings.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"

namespace td {

WebBrowserSettings::WebBrowserSettings(
    telegram_api::object_ptr<telegram_api::account_WebBrowserSettings> &&web_browser_settings_ptr) {
  switch (web_browser_settings_ptr->get_id()) {
    case telegram_api::account_webBrowserSettingsNotModified::ID:
      LOG(ERROR) << "Receive " << to_string(web_browser_settings_ptr);
      break;
    case telegram_api::account_webBrowserSettings::ID: {
      auto settings = telegram_api::move_object_as<telegram_api::account_webBrowserSettings>(web_browser_settings_ptr);
      open_external_browser_ = settings->open_external_browser_;
      display_close_button_ = settings->display_close_button_;
      external_exceptions_ = WebDomainException::get_web_domain_exceptions(std::move(settings->external_exceptions_));
      inapp_exceptions_ = WebDomainException::get_web_domain_exceptions(std::move(settings->inapp_exceptions_));
      hash_ = settings->hash_;
      break;
    }
    default:
      UNREACHABLE();
  }
}

td_api::object_ptr<td_api::webBrowserSettings> WebBrowserSettings::get_web_browser_settings_object() const {
  return td_api::make_object<td_api::webBrowserSettings>(
      open_external_browser_, WebDomainException::get_web_domain_exceptions_object(external_exceptions_),
      WebDomainException::get_web_domain_exceptions_object(inapp_exceptions_), display_close_button_);
}

bool operator==(const WebBrowserSettings &lhs, const WebBrowserSettings &rhs) {
  return lhs.external_exceptions_ == rhs.external_exceptions_ && lhs.inapp_exceptions_ == rhs.inapp_exceptions_ &&
         lhs.open_external_browser_ == rhs.open_external_browser_ &&
         lhs.display_close_button_ == rhs.display_close_button_ && lhs.hash_ == rhs.hash_;
}

bool operator!=(const WebBrowserSettings &lhs, const WebBrowserSettings &rhs) {
  return !(lhs == rhs);
}

StringBuilder &operator<<(StringBuilder &string_builder, const WebBrowserSettings &web_browser_settings) {
  return string_builder << "WebBrowserSettings[open external browser = " << web_browser_settings.open_external_browser_
                        << ", display close button = " << web_browser_settings.display_close_button_
                        << ", external exceptions = " << web_browser_settings.external_exceptions_
                        << ", in-app exceptions = " << web_browser_settings.inapp_exceptions_ << ']';
}

}  // namespace td
