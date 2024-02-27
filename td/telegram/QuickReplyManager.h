//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/QuickReplyShortcutId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"

#include <utility>

namespace td {

class Dependencies;
class MessageContent;
class Td;

class QuickReplyManager final : public Actor {
 public:
  QuickReplyManager(Td *td, ActorShared<> parent);

  void get_quick_reply_shortcuts(Promise<Unit> &&promise);

  void delete_quick_reply_shortcut(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise);

  void reorder_quick_reply_shortcuts(const vector<QuickReplyShortcutId> &shortcut_ids, Promise<Unit> &&promise);

  void reload_quick_reply_shortcuts();

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  struct QuickReplyMessage {
    QuickReplyMessage() = default;
    QuickReplyMessage(const QuickReplyMessage &) = delete;
    QuickReplyMessage &operator=(const QuickReplyMessage &) = delete;
    QuickReplyMessage(QuickReplyMessage &&) = delete;
    QuickReplyMessage &operator=(QuickReplyMessage &&) = delete;
    ~QuickReplyMessage();

    MessageId message_id;
    QuickReplyShortcutId shortcut_id;
    int32 sending_id = 0;  // for yet unsent messages
    int32 edit_date = 0;

    int64 random_id = 0;  // for send_message

    MessageId reply_to_message_id;

    string send_emoji;  // for send_message

    UserId via_bot_user_id;

    bool is_failed_to_send = false;
    bool disable_notification = false;
    bool noforwards = false;
    bool invert_media = false;

    bool from_background = false;           // for send_message
    bool disable_web_page_preview = false;  // for send_message
    bool hide_via_bot = false;              // for resend_message

    int32 legacy_layer = 0;

    int32 send_error_code = 0;
    string send_error_message;
    double try_resend_at = 0;

    int64 media_album_id = 0;

    unique_ptr<MessageContent> content;

    mutable uint64 send_message_log_event_id = 0;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Shortcut {
    Shortcut() = default;
    Shortcut(const Shortcut &) = delete;
    Shortcut &operator=(const Shortcut &) = delete;
    Shortcut(Shortcut &&) = delete;
    Shortcut &operator=(Shortcut &&) = delete;
    ~Shortcut();

    string name_;
    QuickReplyShortcutId shortcut_id_;
    int32 server_total_count_ = 0;
    int32 local_total_count_ = 0;
    vector<unique_ptr<QuickReplyMessage>> messages_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Shortcuts {
    vector<unique_ptr<Shortcut>> shortcuts_;
    bool are_inited_ = false;

    vector<Promise<Unit>> load_queries_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  void tear_down() final;

  void add_quick_reply_message_dependencies(Dependencies &dependencies, const QuickReplyMessage *m) const;

  unique_ptr<QuickReplyMessage> create_message(telegram_api::object_ptr<telegram_api::Message> message_ptr,
                                               const char *source) const;

  bool can_edit_quick_reply_message(const QuickReplyMessage *m) const;

  bool can_resend_quick_reply_message(const QuickReplyMessage *m) const;

  td_api::object_ptr<td_api::MessageSendingState> get_message_sending_state_object(const QuickReplyMessage *m) const;

  td_api::object_ptr<td_api::MessageContent> get_quick_reply_message_message_content_object(
      const QuickReplyMessage *m) const;

  td_api::object_ptr<td_api::quickReplyMessage> get_quick_reply_message_object(const QuickReplyMessage *m,
                                                                               const char *source) const;

  td_api::object_ptr<td_api::quickReplyShortcut> get_quick_reply_shortcut_object(const Shortcut *s,
                                                                                 const char *source) const;

  static int32 get_shortcut_message_count(const Shortcut *s);

  void load_quick_reply_shortcuts(Promise<Unit> &&promise);

  void on_reload_quick_reply_shortcuts(
      Result<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> r_shortcuts);

  void on_load_quick_reply_success();

  void on_load_quick_reply_fail(Status error);

  int64 get_shortcuts_hash() const;

  Shortcut *get_shortcut(QuickReplyShortcutId shortcut_id);

  Shortcut *get_shortcut(const string &name);

  vector<unique_ptr<Shortcut>>::iterator get_shortcut_it(QuickReplyShortcutId shortcut_id);

  vector<unique_ptr<Shortcut>>::iterator get_shortcut_it(const string &name);

  bool is_shortcut_list_changed(const vector<unique_ptr<Shortcut>> &new_shortcuts) const;

  vector<QuickReplyShortcutId> get_shortcut_ids() const;

  vector<QuickReplyShortcutId> get_server_shortcut_ids() const;

  static void sort_quick_reply_messages(vector<unique_ptr<QuickReplyMessage>> &messages);

  using QuickReplyMessageUniqueId = std::pair<MessageId, int32>;

  static QuickReplyMessageUniqueId get_quick_reply_unique_id(const QuickReplyMessage *m);

  static vector<QuickReplyMessageUniqueId> get_quick_reply_unique_ids(
      const vector<unique_ptr<QuickReplyMessage>> &messages);

  static vector<QuickReplyMessageUniqueId> get_server_quick_reply_unique_ids(
      const vector<unique_ptr<QuickReplyMessage>> &messages);

  static bool update_shortcut_from(Shortcut *new_shortcut, Shortcut *old_shortcut, bool is_partial,
                                   bool *is_object_changed);

  td_api::object_ptr<td_api::updateQuickReplyShortcut> get_update_quick_reply_shortcut_object(const Shortcut *s,
                                                                                              const char *source) const;

  void send_update_quick_reply_shortcut(const Shortcut *s, const char *source);

  td_api::object_ptr<td_api::updateQuickReplyShortcutDeleted> get_update_quick_reply_shortcut_deleted_object(
      const Shortcut *s) const;

  void send_update_quick_reply_shortcut_deleted(const Shortcut *s);

  td_api::object_ptr<td_api::updateQuickReplyShortcuts> get_update_quick_reply_shortcuts_object() const;

  void send_update_quick_reply_shortcuts();

  void delete_quick_reply_shortcut_from_server(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise);

  void reorder_quick_reply_shortcuts_on_server(vector<QuickReplyShortcutId> shortcut_ids, Promise<Unit> &&promise);

  string get_quick_reply_shortcuts_database_key();

  void save_quick_reply_shortcuts();

  Shortcuts shortcuts_;

  FlatHashSet<QuickReplyShortcutId, QuickReplyShortcutIdHash> deleted_shortcut_ids_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td