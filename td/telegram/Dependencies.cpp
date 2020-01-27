//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/Dependencies.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/WebPagesManager.h"

#include "td/utils/logging.h"

namespace td {

void resolve_dependencies_force(Td *td, const Dependencies &dependencies) {
  for (auto user_id : dependencies.user_ids) {
    if (user_id.is_valid() && !td->contacts_manager_->have_user_force(user_id)) {
      LOG(ERROR) << "Can't find " << user_id;
    }
  }
  for (auto chat_id : dependencies.chat_ids) {
    if (chat_id.is_valid() && !td->contacts_manager_->have_chat_force(chat_id)) {
      LOG(ERROR) << "Can't find " << chat_id;
    }
  }
  for (auto channel_id : dependencies.channel_ids) {
    if (channel_id.is_valid() && !td->contacts_manager_->have_channel_force(channel_id)) {
      LOG(ERROR) << "Can't find " << channel_id;
    }
  }
  for (auto secret_chat_id : dependencies.secret_chat_ids) {
    if (secret_chat_id.is_valid() && !td->contacts_manager_->have_secret_chat_force(secret_chat_id)) {
      LOG(ERROR) << "Can't find " << secret_chat_id;
    }
  }
  for (auto dialog_id : dependencies.dialog_ids) {
    if (dialog_id.is_valid() && !td->messages_manager_->have_dialog_force(dialog_id)) {
      LOG(ERROR) << "Can't find " << dialog_id;
      td->messages_manager_->force_create_dialog(dialog_id, "resolve_dependencies_force");
    }
  }
  for (auto web_page_id : dependencies.web_page_ids) {
    if (web_page_id.is_valid()) {
      td->web_pages_manager_->have_web_page_force(web_page_id);
    }
  }
}

}  // namespace td
