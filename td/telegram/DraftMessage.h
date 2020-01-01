//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/InputMessageText.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class ContactsManager;

class DraftMessage {
 public:
  int32 date;
  MessageId reply_to_message_id;
  InputMessageText input_message_text;
};

td_api::object_ptr<td_api::draftMessage> get_draft_message_object(const unique_ptr<DraftMessage> &draft_message);

unique_ptr<DraftMessage> get_draft_message(ContactsManager *contacts_manager,
                                           tl_object_ptr<telegram_api::DraftMessage> &&draft_message_ptr);

}  // namespace td
