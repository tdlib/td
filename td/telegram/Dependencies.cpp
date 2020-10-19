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

#include "td/utils/common.h"
#include "td/utils/logging.h"

namespace td {

void add_dialog_and_dependencies(Dependencies &dependencies, DialogId dialog_id) {
  if (dialog_id.is_valid() && dependencies.dialog_ids.insert(dialog_id).second) {
    add_dialog_dependencies(dependencies, dialog_id);
  }
}

void add_dialog_dependencies(Dependencies &dependencies, DialogId dialog_id) {
  switch (dialog_id.get_type()) {
    case DialogType::User:
      dependencies.user_ids.insert(dialog_id.get_user_id());
      break;
    case DialogType::Chat:
      dependencies.chat_ids.insert(dialog_id.get_chat_id());
      break;
    case DialogType::Channel:
      dependencies.channel_ids.insert(dialog_id.get_channel_id());
      break;
    case DialogType::SecretChat:
      dependencies.secret_chat_ids.insert(dialog_id.get_secret_chat_id());
      break;
    case DialogType::None:
      break;
    default:
      UNREACHABLE();
  }
}

void add_message_sender_dependencies(Dependencies &dependencies, DialogId dialog_id) {
  if (dialog_id.get_type() == DialogType::User) {
    dependencies.user_ids.insert(dialog_id.get_user_id());
  } else {
    add_dialog_and_dependencies(dependencies, dialog_id);
  }
}

void resolve_dependencies_force(Td *td, const Dependencies &dependencies, const char *source) {
  for (auto user_id : dependencies.user_ids) {
    if (user_id.is_valid() && !td->contacts_manager_->have_user_force(user_id)) {
      LOG(ERROR) << "Can't find " << user_id << " from " << source;
    }
  }
  for (auto chat_id : dependencies.chat_ids) {
    if (chat_id.is_valid() && !td->contacts_manager_->have_chat_force(chat_id)) {
      LOG(ERROR) << "Can't find " << chat_id << " from " << source;
    }
  }
  for (auto channel_id : dependencies.channel_ids) {
    if (channel_id.is_valid() && !td->contacts_manager_->have_channel_force(channel_id)) {
      LOG(ERROR) << "Can't find " << channel_id << " from " << source;
    }
  }
  for (auto secret_chat_id : dependencies.secret_chat_ids) {
    if (secret_chat_id.is_valid() && !td->contacts_manager_->have_secret_chat_force(secret_chat_id)) {
      LOG(ERROR) << "Can't find " << secret_chat_id << " from " << source;
    }
  }
  for (auto dialog_id : dependencies.dialog_ids) {
    if (dialog_id.is_valid() && !td->messages_manager_->have_dialog_force(dialog_id)) {
      LOG(ERROR) << "Can't find " << dialog_id << " from " << source;
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
