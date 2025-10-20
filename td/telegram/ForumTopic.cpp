//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ForumTopic.h"

#include "td/telegram/DialogDate.h"
#include "td/telegram/DialogManager.h"
#include "td/telegram/DraftMessage.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

ForumTopic::ForumTopic(Td *td, tl_object_ptr<telegram_api::ForumTopic> &&forum_topic_ptr,
                       const DialogNotificationSettings *current_notification_settings) {
  CHECK(forum_topic_ptr != nullptr);
  if (forum_topic_ptr->get_id() != telegram_api::forumTopic::ID) {
    LOG(INFO) << "Receive " << to_string(forum_topic_ptr);
    return;
  }

  auto *forum_topic = static_cast<telegram_api::forumTopic *>(forum_topic_ptr.get());
  is_short_ = forum_topic->short_;
  is_pinned_ = forum_topic->pinned_;
  notification_settings_ =
      get_dialog_notification_settings(std::move(forum_topic->notify_settings_), current_notification_settings);
  draft_message_ = ::td::get_draft_message(td, std::move(forum_topic->draft_));

  if (is_short_) {
    return;
  }

  last_message_id_ = MessageId(ServerMessageId(forum_topic->top_message_));
  unread_count_ = forum_topic->unread_count_;
  last_read_inbox_message_id_ = MessageId(ServerMessageId(forum_topic->read_inbox_max_id_));
  last_read_outbox_message_id_ = MessageId(ServerMessageId(forum_topic->read_outbox_max_id_));
  unread_mention_count_ = forum_topic->unread_mentions_count_;
  unread_reaction_count_ = forum_topic->unread_reactions_count_;
}

bool ForumTopic::update_last_read_outbox_message_id(MessageId last_read_outbox_message_id) {
  if (last_read_outbox_message_id <= last_read_outbox_message_id_) {
    return false;
  }
  last_read_outbox_message_id_ = last_read_outbox_message_id;
  return true;
}

bool ForumTopic::update_last_read_inbox_message_id(MessageId last_read_inbox_message_id, int32 unread_count) {
  if (last_read_inbox_message_id <= last_read_inbox_message_id_) {
    return false;
  }
  last_read_inbox_message_id_ = last_read_inbox_message_id;
  if (unread_count >= 0) {
    unread_count_ = unread_count;
  }
  return true;
}

bool ForumTopic::update_unread_mention_count(int32 count, bool is_relative) {
  auto new_unread_mention_count = is_relative ? unread_mention_count_ + count : count;
  if (new_unread_mention_count < 0) {
    LOG(ERROR) << "Tried to change unread mention count to " << new_unread_mention_count;
    new_unread_mention_count = 0;
  }
  if (unread_mention_count_ == new_unread_mention_count) {
    return false;
  }
  unread_mention_count_ = new_unread_mention_count;
  return true;
}

bool ForumTopic::update_unread_reaction_count(int32 count, bool is_relative) {
  auto new_unread_reaction_count = is_relative ? unread_reaction_count_ + count : count;
  if (new_unread_reaction_count < 0) {
    LOG(ERROR) << "Tried to change unread reaction count to " << new_unread_reaction_count;
    new_unread_reaction_count = 0;
  }
  if (unread_reaction_count_ == new_unread_reaction_count) {
    return false;
  }
  unread_reaction_count_ = new_unread_reaction_count;
  return true;
}

bool ForumTopic::set_draft_message(unique_ptr<DraftMessage> &&draft_message, bool from_update) {
  if (!need_update_draft_message(draft_message_, draft_message, from_update)) {
    LOG(DEBUG) << "Don't need to update draft message";
    return false;
  }

  draft_message_ = std::move(draft_message);
  return true;
}

int64 ForumTopic::get_forum_topic_order(Td *td, DialogId dialog_id) const {
  int64 order = DEFAULT_ORDER;
  if (last_message_id_ != MessageId()) {
    int64 last_message_order = td->messages_manager_->get_message_order(dialog_id, last_message_id_);
    if (last_message_order > order) {
      order = last_message_order;
    }
  }
  // TODO && can_send_message(dialog_id, info_.get_top_thread_message_id()).is_ok();
  if (draft_message_ != nullptr) {
    auto draft_message_date = draft_message_->get_date();
    int64 draft_order = DialogDate::get_dialog_order(MessageId(), draft_message_date);
    if (draft_order > order) {
      order = draft_order;
    }
  }
  return order <= 0 ? 0 : order;
}

td_api::object_ptr<td_api::forumTopic> ForumTopic::get_forum_topic_object(Td *td, DialogId dialog_id,
                                                                          const ForumTopicInfo &info) const {
  if (info.is_empty()) {
    return nullptr;
  }
  CHECK(info.get_dialog_id() == dialog_id);

  // TODO draft_message = can_send_message(dialog_id, info_.get_top_thread_message_id()).is_ok() ? ... : nullptr;
  auto last_message =
      td->messages_manager_->get_message_object({dialog_id, last_message_id_}, "get_forum_topic_object");
  auto draft_message = get_draft_message_object(td, draft_message_);
  return td_api::make_object<td_api::forumTopic>(
      info.get_forum_topic_info_object(td), std::move(last_message), get_forum_topic_order(td, dialog_id), is_pinned_,
      unread_count_, last_read_inbox_message_id_.get(), last_read_outbox_message_id_.get(), unread_mention_count_,
      unread_reaction_count_, get_chat_notification_settings_object(&notification_settings_), std::move(draft_message));
}

td_api::object_ptr<td_api::updateForumTopic> ForumTopic::get_update_forum_topic_object(
    Td *td, DialogId dialog_id, ForumTopicId forum_topic_id) const {
  return td_api::make_object<td_api::updateForumTopic>(
      td->dialog_manager_->get_chat_id_object(dialog_id, "updateForumTopic"), forum_topic_id.get(), is_pinned_,
      last_read_inbox_message_id_.get(), last_read_outbox_message_id_.get(), unread_mention_count_,
      unread_reaction_count_, get_chat_notification_settings_object(&notification_settings_));
}

}  // namespace td
