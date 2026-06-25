//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/WebBrowserSettings.h"
#include "td/telegram/WebDomainException.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void WebBrowserSettings::store(StorerT &storer) const {
  bool has_external_exceptions = !external_exceptions_.empty();
  bool has_inapp_exceptions = !inapp_exceptions_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(open_external_browser_);
  STORE_FLAG(display_close_button_);
  STORE_FLAG(has_external_exceptions);
  STORE_FLAG(has_inapp_exceptions);
  END_STORE_FLAGS();
  if (has_external_exceptions) {
    td::store(external_exceptions_, storer);
  }
  if (has_inapp_exceptions) {
    td::store(inapp_exceptions_, storer);
  }
  td::store(hash_, storer);
}

template <class ParserT>
void WebBrowserSettings::parse(ParserT &parser) {
  bool has_external_exceptions;
  bool has_inapp_exceptions;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(open_external_browser_);
  PARSE_FLAG(display_close_button_);
  PARSE_FLAG(has_external_exceptions);
  PARSE_FLAG(has_inapp_exceptions);
  END_PARSE_FLAGS();
  if (has_external_exceptions) {
    td::parse(external_exceptions_, parser);
  }
  if (has_inapp_exceptions) {
    td::parse(inapp_exceptions_, parser);
  }
  td::parse(hash_, parser);
}

}  // namespace td
