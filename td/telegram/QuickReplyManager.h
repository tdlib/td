//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/files/FileSourceId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/QuickReplyMessageFullId.h"
#include "td/telegram/QuickReplyShortcutId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

#include <utility>

namespace td {

class Dependencies;
class MessageContent;
struct ReplyMarkup;
class Td;

class QuickReplyManager final : public Actor {
 public:
  QuickReplyManager(Td *td, ActorShared<> parent);

  static Status check_shortcut_name(CSlice name);

  void get_quick_reply_shortcuts(Promise<Unit> &&promise);

  void set_quick_reply_shortcut_name(QuickReplyShortcutId shortcut_id, const string &name, Promise<Unit> &&promise);

  void delete_quick_reply_shortcut(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise);

  void reorder_quick_reply_shortcuts(const vector<QuickReplyShortcutId> &shortcut_ids, Promise<Unit> &&promise);

  void update_quick_reply_message(telegram_api::object_ptr<telegram_api::Message> &&message_ptr);

  void delete_quick_reply_messages_from_updates(QuickReplyShortcutId shortcut_id, const vector<MessageId> &message_ids);

  void get_quick_reply_shortcut_messages(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise);

  void delete_quick_reply_shortcut_messages(QuickReplyShortcutId shortcut_id, const vector<MessageId> &message_ids,
                                            Promise<Unit> &&promise);

  void reload_quick_reply_shortcuts();

  void reload_quick_reply_messages(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise);

  void reload_quick_reply_message(QuickReplyShortcutId shortcut_id, MessageId message_id, Promise<Unit> &&promise);

  struct QuickReplyMessageContent {
    unique_ptr<MessageContent> content_;
    MessageId original_message_id_;
    MessageId original_reply_to_message_id_;
    unique_ptr<ReplyMarkup> reply_markup_;
    int64 media_album_id_;
    bool invert_media_;
    bool disable_web_page_preview_;
  };
  Result<vector<QuickReplyMessageContent>> get_quick_reply_message_contents(DialogId dialog_id,
                                                                            QuickReplyShortcutId shortcut_id) const;

  FileSourceId get_quick_reply_message_file_source_id(QuickReplyMessageFullId message_full_id);

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
    bool invert_media = false;
    bool disable_web_page_preview = false;

    bool from_background = false;  // for send_message
    bool hide_via_bot = false;     // for resend_message

    int32 legacy_layer = 0;

    int32 send_error_code = 0;
    string send_error_message;
    double try_resend_at = 0;

    int64 media_album_id = 0;

    unique_ptr<MessageContent> content;
    unique_ptr<ReplyMarkup> reply_markup;

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
    bool are_loaded_from_database_ = false;

    vector<Promise<Unit>> load_queries_;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);
  };

  void tear_down() final;

  static bool is_shortcut_name_letter(uint32 code);

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

  static bool have_all_shortcut_messages(const Shortcut *s);

  void on_reload_quick_reply_shortcuts(
      Result<telegram_api::object_ptr<telegram_api::messages_QuickReplies>> r_shortcuts);

  void on_load_quick_reply_success();

  void on_load_quick_reply_fail(Status error);

  void on_set_quick_reply_shortcut_name(QuickReplyShortcutId shortcut_id, const string &name, Promise<Unit> &&promise);

  int64 get_shortcuts_hash() const;

  void on_reload_quick_reply_messages(QuickReplyShortcutId shortcut_id,
                                      Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages);

  static int64 get_quick_reply_messages_hash(const Shortcut *s);

  void on_reload_quick_reply_message(QuickReplyShortcutId shortcut_id, MessageId message_id,
                                     Result<telegram_api::object_ptr<telegram_api::messages_Messages>> r_messages,
                                     Promise<Unit> &&promise);

  void on_get_quick_reply_message(Shortcut *s, unique_ptr<QuickReplyMessage> message);

  void update_quick_reply_message(QuickReplyShortcutId shortcut_id, unique_ptr<QuickReplyMessage> &old_message,
                                  unique_ptr<QuickReplyMessage> &&new_message);

  void delete_quick_reply_messages(Shortcut *s, const vector<MessageId> &message_ids, const char *source);

  Shortcut *get_shortcut(QuickReplyShortcutId shortcut_id);

  const Shortcut *get_shortcut(QuickReplyShortcutId shortcut_id) const;

  Shortcut *get_shortcut(const string &name);

  vector<unique_ptr<Shortcut>>::iterator get_shortcut_it(QuickReplyShortcutId shortcut_id);

  vector<unique_ptr<QuickReplyMessage>>::iterator get_message_it(Shortcut *s, MessageId message_id);

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

  void update_shortcut_from(Shortcut *new_shortcut, Shortcut *old_shortcut, bool is_partial, bool *is_shortcut_changed,
                            bool *are_messages_changed);

  td_api::object_ptr<td_api::updateQuickReplyShortcut> get_update_quick_reply_shortcut_object(const Shortcut *s,
                                                                                              const char *source) const;

  void send_update_quick_reply_shortcut(const Shortcut *s, const char *source);

  td_api::object_ptr<td_api::updateQuickReplyShortcutDeleted> get_update_quick_reply_shortcut_deleted_object(
      const Shortcut *s) const;

  void send_update_quick_reply_shortcut_deleted(const Shortcut *s);

  td_api::object_ptr<td_api::updateQuickReplyShortcuts> get_update_quick_reply_shortcuts_object() const;

  void send_update_quick_reply_shortcuts();

  td_api::object_ptr<td_api::updateQuickReplyShortcutMessages> get_update_quick_reply_shortcut_messages_object(
      const Shortcut *s, const char *source) const;

  void send_update_quick_reply_shortcut_messages(const Shortcut *s, const char *source);

  void set_quick_reply_shortcut_name_on_server(QuickReplyShortcutId shortcut_id, const string &name,
                                               Promise<Unit> &&promise);

  void delete_quick_reply_shortcut_from_server(QuickReplyShortcutId shortcut_id, Promise<Unit> &&promise);

  void reorder_quick_reply_shortcuts_on_server(vector<QuickReplyShortcutId> shortcut_ids, Promise<Unit> &&promise);

  void delete_quick_reply_messages_on_server(QuickReplyShortcutId shortcut_id, const vector<MessageId> &message_ids,
                                             Promise<Unit> &&promise);

  string get_quick_reply_shortcuts_database_key();

  void save_quick_reply_shortcuts();

  void load_quick_reply_shortcuts();

  vector<FileId> get_message_file_ids(const QuickReplyMessage *m) const;

  void delete_message_files(QuickReplyShortcutId shortcut_id, const QuickReplyMessage *m) const;

  void change_message_files(QuickReplyMessageFullId message_full_id, const QuickReplyMessage *m,
                            const vector<FileId> &old_file_ids);

  Shortcuts shortcuts_;

  FlatHashSet<QuickReplyShortcutId, QuickReplyShortcutIdHash> deleted_shortcut_ids_;

  FlatHashMap<QuickReplyShortcutId, vector<Promise<Unit>>, QuickReplyShortcutIdHash> get_shortcut_messages_queries_;

  FlatHashSet<QuickReplyMessageFullId, QuickReplyMessageFullIdHash> deleted_message_full_ids_;

  FlatHashMap<QuickReplyMessageFullId, FileSourceId, QuickReplyMessageFullIdHash> message_full_id_to_file_source_id_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
