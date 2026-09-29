//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ReplyMarkup.h"

#include "td/utils/common.h"

namespace td {

class MessageContent;

struct InlineMessageContent {
  unique_ptr<MessageContent> message_content;
  unique_ptr<ReplyMarkup> message_reply_markup;
  bool disable_web_page_preview = false;
  bool invert_media = false;
};

}  // namespace td
