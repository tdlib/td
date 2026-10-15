//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DocumentsManager.h"
#include "td/telegram/DocumentsManager.hpp"
#include "td/telegram/FormattedDate.hpp"
#include "td/telegram/InlineKeyboardButton.hpp"
#include "td/telegram/RichButtonStyle.hpp"
#include "td/telegram/RichText.h"
#include "td/telegram/Td.h"
#include "td/telegram/Version.h"

#include "td/utils/common.h"
#include "td/utils/logging.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void RichText::store(StorerT &storer) const {
  using ::td::store;
  store(type_, storer);
  store(content_, storer);
  store(texts_, storer);
  if (type_ == Type::Icon) {
    storer.context()->td().get_actor_unsafe()->documents_manager_->store_document(document_file_id_, storer);
  }
  if (type_ == Type::Url) {
    store(web_page_id_, storer);
  }
  if (type_ == Type::CustomEmoji) {
    store(custom_emoji_id_, storer);
  }
  if (type_ == Type::FormattedDate) {
    store(date_, storer);
  }
  if (type_ == Type::MentionName) {
    store(user_id_, storer);
  }
  if (type_ == Type::Button) {
    store(button_style_, storer);
    store(button_, storer);
  }
}

template <class ParserT>
void RichText::parse(ParserT &parser) {
  using ::td::parse;
  parse(type_, parser);
  parse(content_, parser);
  parse(texts_, parser);
  if (type_ == Type::Icon) {
    document_file_id_ = parser.context()->td().get_actor_unsafe()->documents_manager_->parse_document(parser);
    if (!document_file_id_.is_valid()) {
      LOG(ERROR) << "Failed to load document from database";
      *this = RichText();
    }
  }
  if (type_ == Type::Url && parser.version() >= static_cast<int32>(Version::SupportInstantView2_0)) {
    parse(web_page_id_, parser);
  }
  if (type_ == Type::CustomEmoji) {
    parse(custom_emoji_id_, parser);
  }
  if (type_ == Type::FormattedDate) {
    parse(date_, parser);
  }
  if (type_ == Type::MentionName) {
    parse(user_id_, parser);
  }
  if (type_ == Type::Button) {
    parse(button_style_, parser);
    parse(button_, parser);
  }
}

}  // namespace td
