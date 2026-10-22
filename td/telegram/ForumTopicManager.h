//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/CustomEmojiId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogNotificationSettings.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/ForumTopic.h"
#include "td/telegram/ForumTopicEditedData.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/ForumTopicInfo.h"
#include "td/telegram/ForumUnreadCounts.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesInfo.h"
#include "td/telegram/MessageThreadDb.h"
#include "td/telegram/OrderedMessage.h"
#include "td/telegram/OrderedTopics.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"
#include "td/utils/WaitFreeHashSet.h"

namespace td {

class DraftMessage;
class Td;

class ForumTopicManager final : public Actor {
 public:
  ForumTopicManager(Td *td, ActorShared<> parent);
  ForumTopicManager(const ForumTopicManager &) = delete;
  ForumTopicManager &operator=(const ForumTopicManager &) = delete;
  ForumTopicManager(ForumTopicManager &&) = delete;
  ForumTopicManager &operator=(ForumTopicManager &&) = delete;
  ~ForumTopicManager() final;

  int32 get_forum_topic_id_object(DialogId dialog_id, ForumTopicId forum_topic_id);

  bool can_be_forum(DialogId dialog_id) const;

  Status is_forum(DialogId dialog_id, bool allow_bots = false);

  bool can_send_message_to_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  void create_forum_topic(DialogId dialog_id, string &&title, bool title_missing,
                          td_api::object_ptr<td_api::forumTopicIcon> &&icon,
                          Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise);

  void on_forum_topic_created(DialogId dialog_id, unique_ptr<ForumTopicInfo> &&forum_topic_info,
                              Promise<td_api::object_ptr<td_api::forumTopicInfo>> &&promise);

  void edit_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, string &&title, bool edit_icon_custom_emoji,
                        CustomEmojiId icon_custom_emoji_id, Promise<Unit> &&promise);

  void get_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id,
                       Promise<td_api::object_ptr<td_api::forumTopic>> &&promise);

  void reload_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, Promise<Unit> &&promise);

  void on_get_forum_topic(DialogId dialog_id, ForumTopicId expected_forum_topic_id, MessagesInfo &&info,
                          telegram_api::object_ptr<telegram_api::ForumTopic> &&topic,
                          Promise<td_api::object_ptr<td_api::forumTopic>> &&promise);

  void on_get_forum_topics_by_id(DialogId dialog_id, MessagesInfo &&info,
                                 vector<telegram_api::object_ptr<telegram_api::ForumTopic>> &&topics,
                                 Promise<Unit> &&promise);

  void on_get_forum_topics(DialogId dialog_id, bool order_by_creation_date, MessagesInfo &&info,
                           vector<telegram_api::object_ptr<telegram_api::ForumTopic>> &&topics,
                           Promise<td_api::object_ptr<td_api::forumTopics>> &&promise);

  void get_forum_topic_link(DialogId dialog_id, ForumTopicId forum_topic_id,
                            Promise<td_api::object_ptr<td_api::messageLink>> &&promise);

  void load_forum_topics(DialogId dialog_id, int32 limit, Promise<Unit> &&promise);

  void get_forum_topics(DialogId dialog_id, string query, int32 offset_date, MessageId offset_message_id,
                        ForumTopicId offset_forum_topic_id, int32 limit,
                        Promise<td_api::object_ptr<td_api::forumTopics>> promise);

  void toggle_forum_topic_is_closed(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_closed,
                                    Promise<Unit> &&promise);

  Status set_forum_topic_notification_settings(
      DialogId dialog_id, ForumTopicId forum_topic_id,
      td_api::object_ptr<td_api::chatNotificationSettings> &&notification_settings) TD_WARN_UNUSED_RESULT;

  Status set_forum_topic_draft_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                       unique_ptr<DraftMessage> &&draft_message);

  void toggle_forum_topic_is_hidden(DialogId dialog_id, bool is_hidden, Promise<Unit> &&promise);

  void toggle_forum_topic_is_pinned(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_pinned,
                                    Promise<Unit> &&promise);

  void set_pinned_forum_topics(DialogId dialog_id, vector<ForumTopicId> forum_topic_ids, Promise<Unit> &&promise);

  void delete_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, Promise<Unit> &&promise);

  void delete_all_dialog_topics(DialogId dialog_id);

  void on_dialog_is_forum_changed(DialogId dialog_id);

  void read_forum_topic_messages(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId last_read_inbox_message_id);

  void on_update_forum_topic_unread(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId last_message_id,
                                    MessageId last_read_inbox_message_id, MessageId last_read_outbox_message_id,
                                    int32 unread_count);

  void on_update_forum_topic_notify_settings(DialogId dialog_id, ForumTopicId forum_topic_id,
                                             tl_object_ptr<telegram_api::peerNotifySettings> &&peer_notify_settings,
                                             const char *source);

  void on_update_forum_topic_draft_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                           unique_ptr<DraftMessage> &&draft_message);

  void clear_forum_topic_draft_by_sent_message(DialogId dialog_id, ForumTopicId forum_topic_id,
                                               bool message_clear_draft, MessageContentType message_content_type);

  void on_update_forum_topic_is_pinned(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_pinned);

  void on_update_pinned_forum_topics(DialogId dialog_id, vector<ForumTopicId> forum_topic_ids);

  void on_update_read_all_forum_topics(DialogId dialog_id, MessageId read_inbox_max_message_id);

  void preload_forum_topics(DialogId dialog_id);

  void on_dialog_mute_changed(DialogId dialog_id);

  void on_notification_scope_mute_changed();

  // returns the mute_until of the topic, if the topic doesn't use the chat's value
  bool get_forum_topic_mute_until(DialogId dialog_id, ForumTopicId forum_topic_id, int32 &mute_until) const;

  // returns whether the notification settings of the topic are known
  bool have_forum_topic_notification_settings(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  void load_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id);

  void reload_pinned_forum_topics(DialogId dialog_id);

  void on_channel_difference_too_long(DialogId dialog_id);

  void on_forum_topic_edited(DialogId dialog_id, ForumTopicId forum_topic_id, const ForumTopicEditedData &edited_data);

  bool on_get_forum_topic_error(DialogId dialog_id, ForumTopicId forum_topic_id, const Status &status,
                                const char *source);

  void on_get_forum_topic_info(DialogId dialog_id, const ForumTopicInfo &topic_info, const char *source);

  void on_get_forum_topic_infos(DialogId dialog_id, vector<tl_object_ptr<telegram_api::ForumTopic>> &&forum_topics,
                                const char *source);

  bool is_last_topic_message(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId message_id) const;

  void on_topic_message_added(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId message_id, int32 message_date,
                              bool from_update, bool need_update, bool is_new, const char *source);

  void on_topic_message_updated(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId message_id);

  void on_topic_message_deleted(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId message_id,
                                bool is_permanently_deleted, bool only_from_memory, const char *source);

  // returns whether some topics were unloaded, so that their last messages can be unloaded too
  bool unload_forum_topics(DialogId dialog_id);

  void on_all_dialog_messages_deleted(DialogId dialog_id);

  void on_get_topic_history_from_server(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId from_message_id,
                                        int32 offset, int32 limit, const vector<MessageId> &message_ids,
                                        bool are_all_messages_from_topic);

  // the messages must be all messages of the topic between the oldest of them and from_message_id
  void on_get_topic_history_messages(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId from_message_id,
                                     vector<MessageId> message_ids, const char *source);

  // returns the known messages, which follow each other in the topic, and preloads more of them
  vector<MessageId> get_topic_history_from_memory(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                  MessageId from_message_id, int32 offset, int32 limit);

  void preload_topic_history(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId min_message_id,
                             MessageId max_message_id);

  // returns the range of topic messages, which are all stored in the message database
  bool get_topic_database_range(DialogId dialog_id, ForumTopicId forum_topic_id, MessageId &first_message_id,
                                MessageId &last_message_id, bool &is_last) const;

  MessageId get_topic_last_message_id(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  void forget_topic_database_range(DialogId dialog_id, ForumTopicId forum_topic_id);

  void on_topic_mention_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int32 count, bool is_relative);

  void on_topic_reaction_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int32 count, bool is_relative);

  void on_topic_poll_vote_count_changed(DialogId dialog_id, ForumTopicId forum_topic_id, int32 count, bool is_relative);

  void repair_topic_unread_mention_count(DialogId dialog_id, ForumTopicId forum_topic_id);

  void repair_topic_unread_reaction_count(DialogId dialog_id, ForumTopicId forum_topic_id);

  void repair_topic_unread_poll_vote_count(DialogId dialog_id, ForumTopicId forum_topic_id);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  static constexpr size_t MAX_FORUM_TOPIC_TITLE_LENGTH = 128;  // server-side limit for forum topic title
  static constexpr int32 MAX_GET_FORUM_TOPICS = 100;           // server-side limit
  static constexpr size_t MAX_GET_FORUM_TOPICS_BY_ID = 100;    // server-side limit
  static constexpr int32 MAX_GET_HISTORY = 100;                // server-side limit
  static constexpr size_t MAX_KEPT_FORUM_TOPICS = 20;          // non-pinned topics, which stay loaded with the list
  static constexpr int32 MAX_PRELOAD_FORUM_TOPICS = 20;        // forums in chat lists need their unread topics
  static constexpr int32 MAX_LOAD_FORUM_TOPICS_FROM_DATABASE = 1000;

  struct Topic {
    unique_ptr<ForumTopicInfo> info_;
    unique_ptr<ForumTopic> topic_;
    OrderedMessages ordered_messages_;
    int32 receive_date_ = 0;
    int32 last_message_date_ = 0;
    MessageId sent_read_inbox_message_id_;  // the last read position, which was sent to the server
    MessageId last_server_message_id_;      // the last known server message in the topic
    // all messages of the topic between them are stored in the message database; MessageId::min() means from the start
    MessageId first_database_message_id_;
    MessageId last_database_message_id_;
    int64 pinned_order_ = 0;
    int64 private_order_ = 0;
    ForumUnreadCounts unread_counts_;   // the part of unread counts of the forum, which is added by the topic
    bool is_database_checked_ = false;  // whether the topic was looked for in the database before a reload
    bool is_loading_from_database_ = false;
    bool is_preloading_older_messages_ = false;
    bool is_preloading_newer_messages_ = false;
    mutable bool need_save_to_database_ = true;

    template <class StorerT>
    void store(StorerT &storer) const;

    template <class ParserT>
    void parse(ParserT &parser);

    int32 MAGIC = 0x1fac3901;
  };

  struct DialogTopics {
    WaitFreeHashMap<ForumTopicId, unique_ptr<Topic>, ForumTopicIdHash> topics_;
    WaitFreeHashSet<ForumTopicId, ForumTopicIdHash> deleted_topic_ids_;
    OrderedTopics<ForumTopicId> ordered_topics_;

    uint32 generation_ = 0;
    bool is_database_loaded_ = false;
    // the list is loaded up to the nearer of the ends of the parts read from the database and from the server
    OrderedTopics<ForumTopicId>::TopicDate last_database_topic_date_ = OrderedTopics<ForumTopicId>::max_topic_date();
    OrderedTopics<ForumTopicId>::TopicDate last_server_topic_date_ = OrderedTopics<ForumTopicId>::min_topic_date();
    vector<Promise<Unit>> load_queries_;
    int32 offset_date_ = 0;
    MessageId offset_message_id_;
    ForumTopicId offset_forum_topic_id_;

    vector<ForumTopicId> reload_topic_ids_;
    vector<ForumTopicId> reloading_topic_ids_;
    int32 reload_topics_retry_delay_ = 0;      // the delay before the next retry of a failed reload, in seconds
    vector<ForumTopicId> received_topic_ids_;  // the topics, which are received together with the messages being added

    // the pinned topics received from the server, which must be applied after unknown topics are loaded
    vector<ForumTopicId> pending_pinned_topic_ids_;
    bool have_pending_pinned_topic_ids_ = false;
    bool is_reloading_pinned_topics_ = false;
    bool need_reload_pinned_topics_ = false;

    ForumUnreadCounts unread_counts_;
    // the counts aren't sent before the list is loaded, because they would cover only some of the topics
    bool are_unread_counts_known_ = false;
    int32 next_unmute_date_ = 0;  // the earliest date, when an unread topic stops being muted
  };

  void tear_down() final;

  static Status can_be_forum_topic_id(ForumTopicId forum_topic_id);

  DialogTopics *add_dialog_topics(DialogId dialog_id);

  DialogTopics *get_dialog_topics(DialogId dialog_id);

  static Topic *add_topic(DialogTopics *dialog_topics, ForumTopicId forum_topic_id);

  static Topic *get_topic(DialogTopics *dialog_topics, ForumTopicId forum_topic_id);

  Topic *add_topic(DialogId dialog_id, ForumTopicId forum_topic_id);

  Topic *get_topic(DialogId dialog_id, ForumTopicId forum_topic_id);

  const Topic *get_topic(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  ForumTopicInfo *get_topic_info(DialogId dialog_id, ForumTopicId forum_topic_id);

  const ForumTopicInfo *get_topic_info(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  void set_topic_info(DialogId dialog_id, Topic *topic, unique_ptr<ForumTopicInfo> forum_topic_info);

  ForumTopicId on_get_forum_topic_impl(DialogId dialog_id, tl_object_ptr<telegram_api::ForumTopic> &&forum_topic);

  static ForumTopicId get_received_forum_topic_id(const telegram_api::ForumTopic *forum_topic);

  // pinned topics are received with the first page of the list, so updates about them are needed only after it
  static bool is_topic_list_loaded(const DialogTopics *dialog_topics);

  void on_get_topic_messages(DialogId dialog_id, MessagesInfo &&info, vector<ForumTopicId> received_topic_ids,
                             const char *source);

  const DialogNotificationSettings *get_forum_topic_notification_settings(DialogId dialog_id,
                                                                          ForumTopicId forum_topic_id) const;

  DialogNotificationSettings *get_forum_topic_notification_settings(DialogId dialog_id, ForumTopicId forum_topic_id);

  bool update_forum_topic_notification_settings(DialogId dialog_id, ForumTopicId forum_topic_id,
                                                DialogNotificationSettings *current_settings,
                                                DialogNotificationSettings &&new_settings);

  void on_delete_forum_topic(DialogId dialog_id, ForumTopicId forum_topic_id, Promise<Unit> &&promise);

  vector<FileId> get_topic_file_ids(const Topic *topic) const;

  td_api::object_ptr<td_api::forumTopic> get_forum_topic_object(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  td_api::object_ptr<td_api::updateForumTopicInfo> get_update_forum_topic_info_object(
      const ForumTopicInfo *topic_info) const;

  void send_update_forum_topic_info(const ForumTopicInfo *topic_info) const;

  void send_update_forum_topic_deleted(DialogId dialog_id, ForumTopicId forum_topic_id) const;

  td_api::object_ptr<td_api::updateForumTopic> get_update_forum_topic_object(DialogId dialog_id,
                                                                             const Topic *topic) const;

  void send_update_forum_topic(DialogId dialog_id, Topic *topic);

  ForumUnreadCounts get_topic_unread_counts(DialogId dialog_id, const Topic *topic) const;

  static bool update_topic_unread_counts(DialogTopics *dialog_topics, Topic *topic, ForumUnreadCounts unread_counts);

  void recount_unread_counts(DialogId dialog_id, DialogTopics *dialog_topics);

  void schedule_topic_unmute(DialogId dialog_id, DialogTopics *dialog_topics, const Topic *topic);

  static void on_topic_unmute_timeout_callback(void *forum_topic_manager_ptr, int64 dialog_id_int);

  void on_topic_unmute_timeout(DialogId dialog_id);

  void on_topic_list_loaded(DialogId dialog_id, DialogTopics *dialog_topics);

  void send_forum_unread_counts(DialogId dialog_id, const DialogTopics *dialog_topics) const;

  void on_forum_topic_changed(DialogId dialog_id, Topic *topic);

  void update_topic_order(DialogId dialog_id, Topic *topic);

  bool read_topic_messages(DialogId dialog_id, Topic *topic, MessageId read_inbox_max_message_id,
                           int32 hint_unread_count);

  int64 get_topic_public_order(DialogId dialog_id, const Topic *topic) const;

  void reload_topic(DialogId dialog_id, DialogTopics *dialog_topics, ForumTopicId forum_topic_id);

  void reload_topics(DialogId dialog_id, DialogTopics *dialog_topics);

  void on_reload_topics(DialogId dialog_id, uint32 generation, Result<Unit> &&result);

  static void on_reload_topics_timeout_callback(void *forum_topic_manager_ptr, int64 dialog_id_int);

  void on_reload_topics_timeout(DialogId dialog_id);

  void on_reload_pinned_forum_topics(DialogId dialog_id, uint32 generation,
                                     Result<td_api::object_ptr<td_api::forumTopics>> &&result);

  static bool can_unload_topic(const DialogTopics *dialog_topics, const Topic *topic,
                               OrderedTopics<ForumTopicId>::TopicDate last_kept_topic_date);

  void load_forum_topics_from_server(DialogId dialog_id, DialogTopics *dialog_topics, int32 limit);

  bool load_forum_topics_from_database(DialogId dialog_id, DialogTopics *dialog_topics, int32 limit);

  void on_load_forum_topics_from_database(DialogId dialog_id, uint32 generation, string state, int32 limit,
                                          Result<MessageThreadDbMessageThreads> result);

  void load_more_forum_topics_from_database(DialogId dialog_id, DialogTopics *dialog_topics, int32 limit);

  void on_load_more_forum_topics_from_database(DialogId dialog_id, uint32 generation, int32 limit,
                                               Result<MessageThreadDbMessageThreads> result);

  void add_topics_from_database(DialogId dialog_id, DialogTopics *dialog_topics, int32 limit,
                                MessageThreadDbMessageThreads &&message_threads);

  void on_get_topic_from_database(DialogId dialog_id, uint32 generation, ForumTopicId forum_topic_id,
                                  Result<BufferSlice> result);

  static unique_ptr<Topic> parse_topic(DialogId dialog_id, BufferSlice data);

  ForumTopicId add_topic_from_database(DialogId dialog_id, DialogTopics *dialog_topics, unique_ptr<Topic> saved_topic);

  void update_last_topic_date(DialogId dialog_id, DialogTopics *dialog_topics);

  void apply_pending_pinned_forum_topics(DialogId dialog_id, DialogTopics *dialog_topics);

  static void apply_sent_read_inbox_message_id(Topic *topic);

  void on_load_forum_topics(DialogId dialog_id, uint32 generation,
                            Result<td_api::object_ptr<td_api::forumTopics>> &&result);

  void set_local_pinned_forum_topics(DialogId dialog_id, DialogTopics *dialog_topics,
                                     vector<ForumTopicId> forum_topic_ids);

  static string get_topic_list_state_key(DialogId dialog_id);

  void save_topic_list_state(DialogId dialog_id, const DialogTopics *dialog_topics) const;

  void save_topic_to_database(DialogId dialog_id, const Topic *topic);

  void forget_topic_database_range(DialogId dialog_id, Topic *topic);

  void preload_older_topic_messages(DialogId dialog_id, ForumTopicId forum_topic_id, Topic *topic,
                                    MessageId min_message_id);

  void preload_newer_topic_messages(DialogId dialog_id, ForumTopicId forum_topic_id, Topic *topic,
                                    MessageId max_message_id);

  void on_preload_topic_messages(DialogId dialog_id, ForumTopicId forum_topic_id, bool is_older);

  void delete_topic_from_database(DialogId dialog_id, ForumTopicId forum_topic_id, Promise<Unit> &&promise);

  void delete_all_dialog_topics_from_database(DialogId dialog_id);

  Td *td_;
  ActorShared<> parent_;

  WaitFreeHashMap<DialogId, unique_ptr<DialogTopics>, DialogIdHash> dialog_topics_;

  uint32 current_topic_list_generation_ = 0;

  MultiTimeout reload_topics_timeout_{"ForumTopicReloadTimeout"};
  MultiTimeout topic_unmute_timeout_{"ForumTopicUnmuteTimeout"};
};

}  // namespace td
