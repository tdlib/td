//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ForumTopic.h"

#include "td/telegram/DraftMessage.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

ForumTopic::ForumTopic(Td *td, tl_object_ptr<telegram_api::ForumTopic> &&forum_topic_ptr) {
  CHECK(forum_topic_ptr != nullptr);
  if (forum_topic_ptr->get_id() != telegram_api::forumTopic::ID) {
    LOG(INFO) << "Receive " << to_string(forum_topic_ptr);
    return;
  }
  info_ = ForumTopicInfo(forum_topic_ptr);
  auto *forum_topic = static_cast<telegram_api::forumTopic *>(forum_topic_ptr.get());

  last_message_id_ = MessageId(ServerMessageId(forum_topic->top_message_));
  is_pinned_ = forum_topic->pinned_;
  unread_count_ = forum_topic->unread_count_;
  last_read_inbox_message_id_ = MessageId(ServerMessageId(forum_topic->read_inbox_max_id_));
  last_read_outbox_message_id_ = MessageId(ServerMessageId(forum_topic->read_outbox_max_id_));
  unread_mention_count_ = forum_topic->unread_mentions_count_;
  unread_reaction_count_ = forum_topic->unread_reactions_count_;
  notification_settings_ =
      get_dialog_notification_settings(std::move(forum_topic->notify_settings_), false, false, false, false);
  draft_message_ = get_draft_message(td->contacts_manager_.get(), std::move(forum_topic->draft_));
}

td_api::object_ptr<td_api::forumTopic> ForumTopic::get_forum_topic_object(Td *td) const {
  if (is_empty()) {
    return nullptr;
  }

  // TODO draft_message = can_send_message(dialog_id, info_.get_top_thread_message_id()).is_ok() ? ... : nullptr;
  // TODO last_message
  auto draft_message = get_draft_message_object(draft_message_);
  return td_api::make_object<td_api::forumTopic>(
      info_.get_forum_topic_info_object(td), nullptr, is_pinned_, unread_count_, last_read_inbox_message_id_.get(),
      last_read_outbox_message_id_.get(), unread_mention_count_, unread_reaction_count_,
      get_chat_notification_settings_object(&notification_settings_), std::move(draft_message));
}

}  // namespace td
