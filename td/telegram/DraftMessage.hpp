//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DraftMessage.h"

#include "td/telegram/InputMessageText.hpp"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void store(const DraftMessage &draft_message, StorerT &storer) {
  store(draft_message.date, storer);
  store(draft_message.reply_to_message_id, storer);
  store(draft_message.input_message_text, storer);
}

template <class ParserT>
void parse(DraftMessage &draft_message, ParserT &parser) {
  parse(draft_message.date, parser);
  parse(draft_message.reply_to_message_id, parser);
  parse(draft_message.input_message_text, parser);
}

}  // namespace td
