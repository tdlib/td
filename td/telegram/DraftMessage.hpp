//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
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
void DraftMessage::store(StorerT &storer) const {
  td::store(date_, storer);
  td::store(reply_to_message_id_, storer);
  td::store(input_message_text_, storer);
}

template <class ParserT>
void DraftMessage::parse(ParserT &parser) {
  td::parse(date_, parser);
  td::parse(reply_to_message_id_, parser);
  td::parse(input_message_text_, parser);
}

}  // namespace td
