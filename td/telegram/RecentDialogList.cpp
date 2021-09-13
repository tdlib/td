//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/RecentDialogList.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/TdParameters.h"

#include "td/utils/algorithm.h"
#include "td/utils/misc.h"
#include "td/utils/SliceBuilder.h"

namespace td {

RecentDialogList::RecentDialogList(Td *td, const char *name, size_t max_size)
    : td_(td), name_(name), max_size_(max_size) {
}

string RecentDialogList::get_binlog_key() const {
  return PSTRING() << name_ << "_dialog_usernames_and_ids";
}

void RecentDialogList::save_dialogs() const {
  if (dialogs_loaded_ < 2) {
    return;
  }
  CHECK(removed_dialog_ids_.empty());

  string value;
  for (auto &dialog_id : dialog_ids_) {
    if (!value.empty()) {
      value += ',';
    }
    if (!G()->parameters().use_message_db) {
      // if there is no dialog database, prefer to save dialogs by username
      string username;
      switch (dialog_id.get_type()) {
        case DialogType::User:
          username = td_->contacts_manager_->get_user_username(dialog_id.get_user_id());
          break;
        case DialogType::Chat:
          break;
        case DialogType::Channel:
          username = td_->contacts_manager_->get_channel_username(dialog_id.get_channel_id());
          break;
        case DialogType::SecretChat:
          break;
        case DialogType::None:
        default:
          UNREACHABLE();
      }
      if (!username.empty()) {
        value += '@';
        value += username;
        continue;
      }
    }
    value += to_string(dialog_id.get());
  }
  G()->td_db()->get_binlog_pmc()->set(get_binlog_key(), value);
}

bool RecentDialogList::load_dialogs(Promise<Unit> &&promise) {
  if (dialogs_loaded_ >= 2) {
    promise.set_value(Unit());
    return true;
  }

  string found_dialogs_str = G()->td_db()->get_binlog_pmc()->get(get_binlog_key());
  if (found_dialogs_str.empty()) {
    dialogs_loaded_ = 2;
    removed_dialog_ids_.clear();
    if (!dialog_ids_.empty()) {
      save_dialogs();
    }
    promise.set_value(Unit());
    return true;
  }

  auto found_dialogs = full_split(found_dialogs_str, ',');
  if (dialogs_loaded_ == 1 && resolve_dialogs_multipromise_.promise_count() == 0) {
    // queries was sent and have already been finished
    auto newly_found_dialogs = std::move(dialog_ids_);
    dialog_ids_.clear();

    for (auto it = found_dialogs.rbegin(); it != found_dialogs.rend(); ++it) {
      DialogId dialog_id;
      if ((*it)[0] == '@') {
        dialog_id = td_->messages_manager_->resolve_dialog_username(it->substr(1));
      } else {
        dialog_id = DialogId(to_integer<int64>(*it));
      }
      if (dialog_id.is_valid() && removed_dialog_ids_.count(dialog_id) == 0 &&
          td_->messages_manager_->have_input_peer(dialog_id, AccessRights::Read)) {
        td_->messages_manager_->force_create_dialog(dialog_id, "recent dialog");
        do_add_dialog(dialog_id);
      }
    }
    for (auto it = newly_found_dialogs.rbegin(); it != newly_found_dialogs.rend(); ++it) {
      do_add_dialog(*it);
    }
    dialogs_loaded_ = 2;
    removed_dialog_ids_.clear();
    if (!newly_found_dialogs.empty()) {
      save_dialogs();
    }
    promise.set_value(Unit());
    return true;
  }

  resolve_dialogs_multipromise_.add_promise(std::move(promise));
  if (dialogs_loaded_ == 0) {
    dialogs_loaded_ = 1;

    resolve_dialogs_multipromise_.set_ignore_errors(true);
    auto lock = resolve_dialogs_multipromise_.get_promise();

    vector<DialogId> dialog_ids;
    for (auto &found_dialog : found_dialogs) {
      if (found_dialog[0] == '@') {
        td_->messages_manager_->search_public_dialog(found_dialog, false, resolve_dialogs_multipromise_.get_promise());
      } else {
        dialog_ids.push_back(DialogId(to_integer<int64>(found_dialog)));
      }
    }
    if (!dialog_ids.empty()) {
      if (G()->parameters().use_message_db) {
        td_->messages_manager_->load_dialogs(std::move(dialog_ids), resolve_dialogs_multipromise_.get_promise());
      } else {
        td_->messages_manager_->get_dialogs_from_list(
            DialogListId(FolderId::main()), 102,
            PromiseCreator::lambda(
                [promise = resolve_dialogs_multipromise_.get_promise()](
                    td_api::object_ptr<td_api::chats> &&chats) mutable { promise.set_value(Unit()); }));
        td_->contacts_manager_->search_contacts("", 1, resolve_dialogs_multipromise_.get_promise());
      }
    }

    lock.set_value(Unit());
  }
  return false;
}

void RecentDialogList::add_dialog(DialogId dialog_id) {
  if (dialogs_loaded_ != 2) {
    load_dialogs(Promise<Unit>());
  }
  if (do_add_dialog(dialog_id)) {
    save_dialogs();
  }
}

bool RecentDialogList::do_add_dialog(DialogId dialog_id) {
  if (!dialog_ids_.empty() && dialog_ids_[0] == dialog_id) {
    return false;
  }

  // TODO create function
  auto it = std::find(dialog_ids_.begin(), dialog_ids_.end(), dialog_id);
  if (it == dialog_ids_.end()) {
    if (dialog_ids_.size() == max_size_) {
      CHECK(!dialog_ids_.empty());
      dialog_ids_.back() = dialog_id;
    } else {
      dialog_ids_.push_back(dialog_id);
    }
    it = dialog_ids_.end() - 1;
  }
  std::rotate(dialog_ids_.begin(), it, it + 1);
  removed_dialog_ids_.erase(dialog_id);
  return true;
}

void RecentDialogList::remove_dialog(DialogId dialog_id) {
  if (dialogs_loaded_ != 2) {
    load_dialogs(Promise<Unit>());
  }
  if (td::remove(dialog_ids_, dialog_id)) {
    save_dialogs();
  } else if (dialogs_loaded_ != 2) {
    removed_dialog_ids_.insert(dialog_id);
  }
}

void RecentDialogList::update_dialogs() {
  CHECK(dialogs_loaded_ == 2);
  vector<DialogId> dialog_ids;
  for (auto dialog_id : dialog_ids_) {
    if (!td_->messages_manager_->have_dialog(dialog_id)) {
      continue;
    }
    switch (dialog_id.get_type()) {
      case DialogType::User:
        // always keep
        break;
      case DialogType::Chat: {
        auto channel_id = td_->contacts_manager_->get_chat_migrated_to_channel_id(dialog_id.get_chat_id());
        if (channel_id.is_valid() && td_->messages_manager_->have_dialog(DialogId(channel_id))) {
          dialog_id = DialogId(channel_id);
        }
        break;
      }
      case DialogType::Channel:
        // always keep
        break;
      case DialogType::SecretChat:
        if (td_->messages_manager_->is_deleted_secret_chat(dialog_id)) {
          dialog_id = DialogId();
        }
        break;
      case DialogType::None:
      default:
        UNREACHABLE();
        break;
    }
    if (dialog_id.is_valid()) {
      dialog_ids.push_back(dialog_id);
    }
  }

  if (dialog_ids != dialog_ids_) {
    dialog_ids_ = std::move(dialog_ids);
    save_dialogs();
  }
}

std::pair<int32, vector<DialogId>> RecentDialogList::get_dialogs(int32 limit, Promise<Unit> &&promise) {
  if (!load_dialogs(std::move(promise))) {
    return {};
  }

  update_dialogs();

  size_t result_size = min(static_cast<size_t>(limit), dialog_ids_.size());
  return {narrow_cast<int32>(dialog_ids_.size()),
          vector<DialogId>(dialog_ids_.begin(), dialog_ids_.begin() + result_size)};
}

void RecentDialogList::clear_dialogs() {
  if (dialog_ids_.empty() && dialogs_loaded_ == 2) {
    return;
  }

  dialogs_loaded_ = 2;
  dialog_ids_.clear();
  removed_dialog_ids_.clear();
  save_dialogs();
}

}  // namespace td
