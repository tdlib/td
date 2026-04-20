//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/MessagesManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogDb.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessageContent.h"
#include "td/telegram/NotificationManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdDb.h"

namespace td {

void MessagesManager::on_channel_get_difference_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::on_channel_get_difference_timeout,
                     DialogId(dialog_id_int));
}

void MessagesManager::on_pending_message_views_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::on_pending_message_views_timeout,
                     DialogId(dialog_id_int));
}

void MessagesManager::on_pending_message_live_location_view_timeout_callback(void *messages_manager_ptr,
                                                                             int64 task_id) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager),
                     &MessagesManager::view_message_live_location_on_server, task_id);
}

void MessagesManager::on_pending_draft_message_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager),
                     &MessagesManager::save_dialog_draft_message_on_server, DialogId(dialog_id_int));
}

void MessagesManager::on_pending_read_history_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::do_read_history_on_server,
                     DialogId(dialog_id_int));
}

void MessagesManager::on_pending_updated_dialog_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  // no check for G()->close_flag() to save dialogs even while closing

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  // TODO it is unsafe to save dialog to database before binlog is flushed

  // no send_closure_later, because messages_manager can be not an actor while closing
  messages_manager->save_dialog_to_database(DialogId(dialog_id_int));
}

void MessagesManager::on_pending_unload_dialog_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::unload_dialog,
                     DialogId(dialog_id_int), -1);
}

void MessagesManager::on_dialog_unmute_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::on_dialog_unmute,
                     DialogId(dialog_id_int));
}

void MessagesManager::on_pending_send_dialog_action_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::on_send_dialog_action_timeout,
                     DialogId(dialog_id_int));
}

void MessagesManager::on_preload_folder_dialog_list_timeout_callback(void *messages_manager_ptr, int64 folder_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::preload_folder_dialog_list,
                     FolderId(narrow_cast<int32>(folder_id_int)));
}

void MessagesManager::on_update_viewed_messages_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::on_update_viewed_messages_timeout,
                     DialogId(dialog_id_int));
}

void MessagesManager::on_send_update_chat_read_inbox_timeout_callback(void *messages_manager_ptr, int64 dialog_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager),
                     &MessagesManager::on_send_update_chat_read_inbox_timeout, DialogId(dialog_id_int));
}

void MessagesManager::on_live_location_expire_timeout_callback(void *messages_manager_ptr) {
  if (G()->close_flag()) {
    return;
  }

  auto messages_manager = static_cast<MessagesManager *>(messages_manager_ptr);
  send_closure_later(messages_manager->actor_id(messages_manager), &MessagesManager::on_live_location_expire_timeout);
}

void MessagesManager::on_live_location_expire_timeout() {
  if (G()->close_flag() || !td_->auth_manager_->is_authorized()) {
    return;
  }
  vector<MessageFullId> to_delete_message_full_ids;
  for (const auto &message_full_id : active_live_location_message_full_ids_) {
    auto m = get_message(message_full_id);
    CHECK(m != nullptr);
    auto live_period = get_message_content_live_location_period(m->content.get());
    if (live_period <= G()->unix_time() - m->date) {
      to_delete_message_full_ids.push_back(message_full_id);
    }
  }
  if (to_delete_message_full_ids.empty()) {
    LOG(INFO) << "Have no messages to delete";
    schedule_active_live_location_expiration();
  } else {
    for (const auto &message_full_id : to_delete_message_full_ids) {
      bool is_deleted = delete_active_live_location(message_full_id);
      CHECK(is_deleted);
    }
    send_update_active_live_location_messages();
    save_active_live_locations();
  }
}

void MessagesManager::save_dialog_to_database(DialogId dialog_id) {
  CHECK(G()->use_message_database());
  auto d = get_dialog(dialog_id);
  CHECK(d != nullptr);
  LOG(INFO) << "Save " << dialog_id << " to database";
  vector<NotificationGroupKey> changed_group_keys;
  if (d->notification_info != nullptr) {
    d->notification_info->message_notification_group_.add_group_key_if_changed(changed_group_keys, dialog_id);
    d->notification_info->mention_notification_group_.add_group_key_if_changed(changed_group_keys, dialog_id);
  }
  bool can_reuse_notification_group = false;
  for (auto &group_key : changed_group_keys) {
    if (group_key.dialog_id == DialogId()) {
      can_reuse_notification_group = true;
    }
  }
  G()->td_db()->get_dialog_db_async()->add_dialog(
      dialog_id, d->folder_id, d->order, get_dialog_database_value(d), std::move(changed_group_keys),
      PromiseCreator::lambda([dialog_id, can_reuse_notification_group](Result<> result) {
        send_closure(G()->messages_manager(), &MessagesManager::on_save_dialog_to_database, dialog_id,
                     can_reuse_notification_group, result.is_ok());
      }));
}

void MessagesManager::on_save_dialog_to_database(DialogId dialog_id, bool can_reuse_notification_group, bool success) {
  LOG(INFO) << "Successfully saved " << dialog_id << " to database";

  if (success && can_reuse_notification_group && !G()->close_flag()) {
    auto d = get_dialog(dialog_id);
    CHECK(d != nullptr);
    if (d->notification_info != nullptr) {
      try_reuse_notification_group(d->notification_info->message_notification_group_);
      try_reuse_notification_group(d->notification_info->mention_notification_group_);
    }
  }

  // TODO erase some events from binlog
}

void MessagesManager::try_reuse_notification_group(NotificationGroupInfo &group_info) {
  auto group_id = group_info.get_reused_group_id();
  if (group_id.is_valid()) {
    send_closure_later(G()->notification_manager(), &NotificationManager::try_reuse_notification_group_id, group_id);
    notification_group_id_to_dialog_id_.erase(group_id);
  }
}

void MessagesManager::invalidate_message_indexes(Dialog *d) {
  CHECK(d != nullptr);
  bool is_secret = d->dialog_id.get_type() == DialogType::SecretChat;
  for (size_t i = 0; i < d->message_count_by_index.size(); i++) {
    if (is_secret || i == static_cast<size_t>(message_search_filter_index(MessageSearchFilter::FailedToSend))) {
      // always know all messages
      d->first_database_message_id_by_index[i] = MessageId::min();
      // keep the count
    } else {
      // some messages are unknown; drop first_database_message_id and count
      d->first_database_message_id_by_index[i] = MessageId();
      d->message_count_by_index[i] = -1;
    }
  }
}

void MessagesManager::update_message_count_by_index(Dialog *d, int diff, const Message *m) {
  auto index_mask = get_message_index_mask(d->dialog_id, m);
  index_mask &= ~message_search_filter_index_mask(
      MessageSearchFilter::UnreadMention);  // unread mention count has been already manually updated
  index_mask &= ~message_search_filter_index_mask(
      MessageSearchFilter::UnreadReaction);  // unread reaction count has been already manually updated
  index_mask &= ~message_search_filter_index_mask(
      MessageSearchFilter::UnreadPollVote);  // unread poll vote count has been already manually updated

  update_message_count_by_index(d, diff, index_mask);
}

void MessagesManager::update_message_count_by_index(Dialog *d, int diff, int32 index_mask) {
  if (index_mask == 0) {
    return;
  }

  LOG(INFO) << "Update message count by " << diff << " in index mask " << index_mask;
  int i = 0;
  for (auto &message_count : d->message_count_by_index) {
    if (((index_mask >> i) & 1) != 0 && message_count != -1) {
      message_count += diff;
      if (message_count < 0) {
        if (d->dialog_id.get_type() == DialogType::SecretChat ||
            i == message_search_filter_index(MessageSearchFilter::FailedToSend)) {
          message_count = 0;
        } else {
          message_count = -1;
        }
      }
      on_dialog_updated(d->dialog_id, "update_message_count_by_index");
    }
    i++;
  }

  i = static_cast<int>(MessageSearchFilter::Call) - 1;
  for (auto &message_count : calls_db_state_.message_count_by_index) {
    if (((index_mask >> i) & 1) != 0 && message_count != -1) {
      message_count += diff;
      if (message_count < 0) {
        if (d->dialog_id.get_type() == DialogType::SecretChat) {
          message_count = 0;
        } else {
          message_count = -1;
        }
      }
      save_calls_db_state();
    }
    i++;
  }
}

int32 MessagesManager::get_message_index_mask(DialogId dialog_id, const Message *m) const {
  CHECK(m != nullptr);
  if (td_->auth_manager_->is_bot() || m->message_id.is_scheduled() || m->message_id.is_yet_unsent()) {
    return 0;
  }
  if (m->is_failed_to_send) {
    return message_search_filter_index_mask(MessageSearchFilter::FailedToSend);
  }
  bool is_secret = dialog_id.get_type() == DialogType::SecretChat;
  if (!m->message_id.is_server() && !is_secret) {
    return 0;
  }

  int32 index_mask = 0;
  if (m->is_pinned) {
    index_mask |= message_search_filter_index_mask(MessageSearchFilter::Pinned);
  }
  // retain second condition just in case
  if (m->is_content_secret || (!m->ttl.is_empty() && !is_secret)) {
    return index_mask;
  }
  index_mask |= get_message_content_index_mask(m->content.get(), td_, m->is_outgoing);
  if (m->contains_mention) {
    index_mask |= message_search_filter_index_mask(MessageSearchFilter::Mention);
    if (m->contains_unread_mention) {
      index_mask |= message_search_filter_index_mask(MessageSearchFilter::UnreadMention);
    }
  }
  if (has_unread_message_reactions(dialog_id, m)) {
    index_mask |= message_search_filter_index_mask(MessageSearchFilter::UnreadReaction);
  }
  if (has_unread_poll_votes(dialog_id, m)) {
    index_mask |= message_search_filter_index_mask(MessageSearchFilter::UnreadPollVote);
  }
  LOG(INFO) << "Have index mask " << index_mask << " for " << m->message_id << " in " << dialog_id;
  return index_mask;
}

}  // namespace td