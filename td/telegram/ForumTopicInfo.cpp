//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2022
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ForumTopicInfo.h"

#include "td/telegram/MessageSender.h"
#include "td/telegram/ServerMessageId.h"
#include "td/telegram/Td.h"

#include "td/utils/logging.h"

namespace td {

ForumTopicInfo::ForumTopicInfo(Td *td, const tl_object_ptr<telegram_api::ForumTopic> &forum_topic_ptr) {
  CHECK(forum_topic_ptr != nullptr);
  if (forum_topic_ptr->get_id() != telegram_api::forumTopic::ID) {
    LOG(ERROR) << "Receive " << to_string(forum_topic_ptr);
    return;
  }
  const telegram_api::forumTopic *forum_topic = static_cast<const telegram_api::forumTopic *>(forum_topic_ptr.get());

  top_thread_message_id_ = MessageId(ServerMessageId(forum_topic->id_));
  title_ = forum_topic->title_;
  icon_color_ = (forum_topic->icon_color_ & 0xFFFFFF);
  if ((forum_topic->flags_ & telegram_api::forumTopic::ICON_EMOJI_ID_MASK) != 0) {
    icon_custom_emoji_id_ = CustomEmojiId(forum_topic->icon_emoji_id_);
  }
  creation_date_ = forum_topic->date_;
  creator_dialog_id_ = DialogId(forum_topic->from_id_);
  is_outgoing_ = forum_topic->my_;
  is_closed_ = forum_topic->closed_;
  if (creation_date_ <= 0 || !top_thread_message_id_.is_valid() || !creator_dialog_id_.is_valid()) {
    LOG(ERROR) << "Receive " << to_string(forum_topic_ptr);
    top_thread_message_id_ = MessageId();
    return;
  }
}

td_api::object_ptr<td_api::forumTopicInfo> ForumTopicInfo::get_forum_topic_info_object(Td *td) const {
  if (is_empty()) {
    return nullptr;
  }

  auto creator_id = get_message_sender_object_const(td, creator_dialog_id_, "get_forum_topic_info_object");
  return td_api::make_object<td_api::forumTopicInfo>(top_thread_message_id_.get(), title_, icon_color_,
                                                     icon_custom_emoji_id_.get(), creation_date_, std::move(creator_id),
                                                     is_outgoing_, is_closed_);
}

StringBuilder &operator<<(StringBuilder &string_builder, const ForumTopicInfo &topic_info) {
  return string_builder << "Forum topic " << topic_info.top_thread_message_id_.get() << '/' << topic_info.title_
                        << " by " << topic_info.creator_dialog_id_;
}

}  // namespace td
