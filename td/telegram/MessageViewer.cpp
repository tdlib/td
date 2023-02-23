//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessageViewer.h"

#include "td/telegram/ContactsManager.h"

#include "td/utils/algorithm.h"

namespace td {

MessageViewer::MessageViewer(telegram_api::object_ptr<telegram_api::readParticipantDate> &&read_date)
    : user_id_(read_date->user_id_), date_(read_date->date_) {
}

td_api::object_ptr<td_api::messageViewer> MessageViewer::get_message_viewer_object(
    ContactsManager *contacts_manager) const {
  return td_api::make_object<td_api::messageViewer>(
      contacts_manager->get_user_id_object(user_id_, "get_message_viewer_object"), date_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageViewer &viewer) {
  return string_builder << '[' << viewer.user_id_ << " at " << viewer.date_ << ']';
}

MessageViewers::MessageViewers(vector<telegram_api::object_ptr<telegram_api::readParticipantDate>> &&read_dates)
    : message_viewers_(
          transform(std::move(read_dates), [](telegram_api::object_ptr<telegram_api::readParticipantDate> &&read_date) {
            return MessageViewer(std::move(read_date));
          })) {
}

td_api::object_ptr<td_api::messageViewers> MessageViewers::get_message_viewers_object(
    ContactsManager *contacts_manager) const {
  return td_api::make_object<td_api::messageViewers>(
      transform(message_viewers_, [contacts_manager](const MessageViewer &message_viewer) {
        return message_viewer.get_message_viewer_object(contacts_manager);
      }));
}

StringBuilder &operator<<(StringBuilder &string_builder, const MessageViewers &viewers) {
  return string_builder << viewers.message_viewers_;
}

}  // namespace td
