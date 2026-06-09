//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/WebDomainException.h"

#include "td/utils/common.h"
#include "td/utils/Slice.h"
#include "td/utils/StringBuilder.h"

namespace td {

class WebBrowserSettings {
  vector<WebDomainException> external_exceptions_;
  vector<WebDomainException> inapp_exceptions_;
  bool open_external_browser_ = false;
  bool display_close_button_ = false;
  int64 hash_ = 0;

  friend bool operator==(const WebBrowserSettings &lhs, const WebBrowserSettings &rhs);

  friend StringBuilder &operator<<(StringBuilder &string_builder, const WebBrowserSettings &web_browser_settings);

 public:
  WebBrowserSettings() = default;

  explicit WebBrowserSettings(
      telegram_api::object_ptr<telegram_api::account_WebBrowserSettings> &&web_browser_settings_ptr);

  td_api::object_ptr<td_api::webBrowserSettings> get_web_browser_settings_object() const;

  int64 get_hash() const {
    return hash_;
  }

  bool update_from(telegram_api::object_ptr<telegram_api::updateWebBrowserSettings> &&update);

  bool update_from(telegram_api::object_ptr<telegram_api::updateWebBrowserException> &&update);

  bool get_open_external_browser(Slice domain) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const WebBrowserSettings &lhs, const WebBrowserSettings &rhs);

bool operator!=(const WebBrowserSettings &lhs, const WebBrowserSettings &rhs);

StringBuilder &operator<<(StringBuilder &string_builder, const WebBrowserSettings &web_browser_settings);

}  // namespace td
