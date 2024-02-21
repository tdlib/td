//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSelfDestructType.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;
class MessageContent;
class MessageForwardInfo;
class Td;

class QuickReplyManager final : public Actor {
 public:
  QuickReplyManager(Td *td, ActorShared<> parent);

 private:
  struct QuickReplyMessage {
    MessageId message_id;
    int32 sending_id = 0;  // for yet unsent messages

    int64 random_id = 0;  // for send_message

    unique_ptr<MessageForwardInfo> forward_info;

    MessageId reply_to_message_id;

    string send_emoji;  // for send_message

    UserId via_bot_user_id;

    bool is_failed_to_send = false;
    bool disable_notification = false;
    bool is_content_secret = false;  // must be shown only while tapped
    bool noforwards = false;
    bool invert_media = false;

    bool has_explicit_sender = false;       // for send_message
    bool is_copy = false;                   // for send_message
    bool from_background = false;           // for send_message
    bool disable_web_page_preview = false;  // for send_message
    bool hide_via_bot = false;              // for resend_message

    DialogId real_forward_from_dialog_id;    // for resend_message
    MessageId real_forward_from_message_id;  // for resend_message

    int32 legacy_layer = 0;

    int32 send_error_code = 0;
    string send_error_message;
    double try_resend_at = 0;

    MessageSelfDestructType ttl;

    int64 media_album_id = 0;

    unique_ptr<MessageContent> content;

    mutable uint64 send_message_log_event_id = 0;

    QuickReplyMessage() = default;
    QuickReplyMessage(const QuickReplyMessage &) = delete;
    QuickReplyMessage &operator=(const QuickReplyMessage &) = delete;
    QuickReplyMessage(QuickReplyMessage &&) = delete;
    QuickReplyMessage &operator=(QuickReplyMessage &&) = delete;
    ~QuickReplyMessage() = default;
  };

  void tear_down() final;

  td_api::object_ptr<td_api::MessageContent> get_quick_reply_message_message_content_object(
      const QuickReplyMessage *m) const;

  void add_quick_reply_message_dependencies(Dependencies &dependencies, const QuickReplyMessage *m) const;

  unique_ptr<QuickReplyMessage> create_message(telegram_api::object_ptr<telegram_api::Message> message_ptr,
                                               const char *source) const;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
