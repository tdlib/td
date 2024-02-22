//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
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

  void get_quick_reply_shortcuts(Promise<td_api::object_ptr<td_api::quickReplyShortcuts>> &&promise);

  void reload_quick_reply_shortcuts();

 private:
  struct QuickReplyMessage {
    QuickReplyMessage() = default;
    QuickReplyMessage(const QuickReplyMessage &) = delete;
    QuickReplyMessage &operator=(const QuickReplyMessage &) = delete;
    QuickReplyMessage(QuickReplyMessage &&) = delete;
    QuickReplyMessage &operator=(QuickReplyMessage &&) = delete;
    ~QuickReplyMessage();

    MessageId message_id;
    int32 shortcut_id = 0;
    int32 sending_id = 0;  // for yet unsent messages

    int64 random_id = 0;  // for send_message

    unique_ptr<MessageForwardInfo> forward_info;

    MessageId reply_to_message_id;

    string send_emoji;  // for send_message

    UserId via_bot_user_id;

    bool is_failed_to_send = false;
    bool disable_notification = false;
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

    int64 media_album_id = 0;

    unique_ptr<MessageContent> content;

    mutable uint64 send_message_log_event_id = 0;
  };

  struct Shortcut {
    Shortcut() = default;
    Shortcut(const Shortcut &) = delete;
    Shortcut &operator=(const Shortcut &) = delete;
    Shortcut(Shortcut &&) = delete;
    Shortcut &operator=(Shortcut &&) = delete;
    ~Shortcut();

    string name_;
    int32 shortcut_id_ = 0;
    int32 total_count_ = 0;
    vector<unique_ptr<QuickReplyMessage>> messages_;
  };

  struct Shortcuts {
    vector<unique_ptr<Shortcut>> shortcuts_;
    bool are_inited_ = false;

    vector<Promise<td_api::object_ptr<td_api::quickReplyShortcuts>>> load_queries_;
  };

  void tear_down() final;

  void add_quick_reply_message_dependencies(Dependencies &dependencies, const QuickReplyMessage *m) const;

  unique_ptr<QuickReplyMessage> create_message(telegram_api::object_ptr<telegram_api::Message> message_ptr,
                                               const char *source) const;

  bool can_resend_message(const QuickReplyMessage *m) const;

  td_api::object_ptr<td_api::MessageSendingState> get_message_sending_state_object(const QuickReplyMessage *m) const;

  td_api::object_ptr<td_api::MessageContent> get_quick_reply_message_message_content_object(
      const QuickReplyMessage *m) const;

  td_api::object_ptr<td_api::quickReplyMessage> get_quick_reply_message_object(const QuickReplyMessage *m,
                                                                               const char *source) const;

  td_api::object_ptr<td_api::quickReplyShortcut> get_quick_reply_shortcut_object(const Shortcut *s,
                                                                                 const char *source) const;

  td_api::object_ptr<td_api::quickReplyShortcuts> get_quick_reply_shortcuts_object(const char *source) const;

  static int32 get_shortcut_message_count(const Shortcut *s);

  void load_quick_reply_shortcuts(Promise<td_api::object_ptr<td_api::quickReplyShortcuts>> &&promise);

  void on_reload_quick_reply_shortcuts(
      Result<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> r_shortcuts);

  void on_load_quick_reply_success();

  void on_load_quick_reply_fail(Status error);

  Shortcut *get_shortcut(int32 shortcut_id);

  static void sort_quick_reply_messages(vector<unique_ptr<QuickReplyMessage>> &messages);

  static vector<MessageId> get_quick_reply_message_ids(const vector<unique_ptr<QuickReplyMessage>> &messages);

  static vector<MessageId> get_server_quick_reply_message_ids(const vector<unique_ptr<QuickReplyMessage>> &messages);

  static bool update_shortcut_from(Shortcut *new_shortcut, Shortcut *old_shortcut, bool is_partial);

  td_api::object_ptr<td_api::updateQuickReplyShortcut> get_update_quick_reply_shortcut_object(const Shortcut *s,
                                                                                              const char *source) const;

  void send_update_quick_reply_shortcut(const Shortcut *s, const char *source);

  td_api::object_ptr<td_api::updateQuickReplyShortcutDeleted> get_update_quick_reply_shortcut_deleted_object(
      const Shortcut *s) const;

  void send_update_quick_reply_shortcut_deleted(const Shortcut *s);

  Shortcuts shortcuts_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
