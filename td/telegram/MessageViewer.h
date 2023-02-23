//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/utils/common.h"
#include "td/utils/StringBuilder.h"

namespace td {

class ContactsManager;

class MessageViewer {
  UserId user_id_;
  int32 date_ = 0;

  friend StringBuilder &operator<<(StringBuilder &string_builder, const MessageViewer &viewer);

 public:
  explicit MessageViewer(telegram_api::object_ptr<telegram_api::readParticipantDate> &&read_date);

  UserId get_user_id() const {
    return user_id_;
  }

  td_api::object_ptr<td_api::messageViewer> get_message_viewer_object(ContactsManager *contacts_manager) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageViewer &viewer);

struct MessageViewers {
  vector<MessageViewer> message_viewers_;

  explicit MessageViewers(vector<telegram_api::object_ptr<telegram_api::readParticipantDate>> &&read_dates);

  td_api::object_ptr<td_api::messageViewers> get_message_viewers_object(ContactsManager *contacts_manager) const;
};

StringBuilder &operator<<(StringBuilder &string_builder, const MessageViewers &viewers);

}  // namespace td
