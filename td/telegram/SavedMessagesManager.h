//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageContentType.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessagesInfo.h"
#include "td/telegram/OrderedMessage.h"
#include "td/telegram/SavedMessagesTopicId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

#include <limits>
#include <set>

namespace td {

class DraftMessage;
class Td;

class SavedMessagesManager final : public Actor {
 public:
  SavedMessagesManager(Td *td, ActorShared<> parent);

  bool have_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) const;

  SavedMessagesTopicId get_topic_id(DialogId dialog_id, int64 topic_id) const;

  vector<SavedMessagesTopicId> get_topic_ids(DialogId dialog_id, const vector<int64> &topic_ids) const;

  int64 get_saved_messages_topic_id_object(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id);

  bool is_last_topic_message(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                             MessageId message_id) const;

  void on_topic_message_added(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, MessageId message_id,
                              int32 message_date, const bool from_update, const bool need_update, const bool is_new,
                              const char *source);

  void on_topic_message_updated(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, MessageId message_id);

  void on_topic_message_deleted(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, MessageId message_id,
                                bool only_from_memory, const char *source);

  void on_all_dialog_messages_deleted(DialogId dialog_id);

  void on_topic_draft_message_updated(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                      int32 draft_message_date);

  void clear_monoforum_topic_draft_by_sent_message(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                   bool message_clear_draft, MessageContentType message_content_type);

  void read_monoforum_topic_messages(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                     MessageId read_inbox_max_message_id);

  void on_update_read_monoforum_inbox(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                      MessageId read_inbox_max_message_id);

  void on_update_read_all_monoforum_inbox(DialogId dialog_id, MessageId read_inbox_max_message_id);

  void on_update_read_monoforum_outbox(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                       MessageId read_outbox_max_message_id);

  void on_update_monoforum_nopaid_messages_exception(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                                     bool nopaid_messages_exception);

  void on_update_topic_draft_message(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                     telegram_api::object_ptr<telegram_api::DraftMessage> &&draft_message,
                                     int32 try_count = 0);

  void on_update_topic_is_marked_as_unread(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                           bool is_marked_as_unread);

  void on_topic_reaction_count_changed(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, int32 count,
                                       bool is_relative);

  void load_saved_messages_topics(int32 limit, Promise<Unit> &&promise);

  void load_monoforum_topics(DialogId dialog_id, int32 limit, Promise<Unit> &&promise);

  void on_get_saved_messages_topics(DialogId dialog_id, uint32 generation,
                                    SavedMessagesTopicId expected_saved_messages_topic_id, bool is_pinned, int32 limit,
                                    telegram_api::object_ptr<telegram_api::messages_SavedDialogs> &&saved_dialogs_ptr,
                                    Promise<Unit> &&promise);

  void get_monoforum_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                           Promise<td_api::object_ptr<td_api::directMessagesChatTopic>> &&promise);

  void get_monoforum_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                   MessageId from_message_id, int32 offset, int32 limit,
                                   Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void get_saved_messages_topic_history(SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id,
                                        int32 offset, int32 limit,
                                        Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void delete_monoforum_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                      Promise<Unit> &&promise);

  void delete_saved_messages_topic_history(SavedMessagesTopicId saved_messages_topic_id, Promise<Unit> &&promise);

  void get_monoforum_topic_message_by_date(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, int32 date,
                                           Promise<td_api::object_ptr<td_api::message>> &&promise);

  void get_saved_messages_topic_message_by_date(SavedMessagesTopicId saved_messages_topic_id, int32 date,
                                                Promise<td_api::object_ptr<td_api::message>> &&promise);

  void delete_monoforum_topic_messages_by_date(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                               int32 min_date, int32 max_date, Promise<Unit> &&promise);

  void delete_saved_messages_topic_messages_by_date(SavedMessagesTopicId saved_messages_topic_id, int32 min_date,
                                                    int32 max_date, Promise<Unit> &&promise);

  void toggle_saved_messages_topic_is_pinned(SavedMessagesTopicId saved_messages_topic_id, bool is_pinned,
                                             Promise<Unit> &&promise);

  void set_pinned_saved_messages_topics(vector<SavedMessagesTopicId> saved_messages_topic_ids, Promise<Unit> &&promise);

  void reload_pinned_saved_messages_topics();

  void set_monoforum_topic_is_marked_as_unread(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                               bool is_marked_as_unread, Promise<Unit> &&promise);

  Status set_monoforum_topic_draft_message(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                           unique_ptr<DraftMessage> &&draft_message);

  void unpin_all_monoforum_topic_messages(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                          Promise<Unit> &&promise);

  void read_all_monoforum_topic_reactions(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                          Promise<Unit> &&promise);

  void get_monoforum_topic_revenue(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                                   Promise<td_api::object_ptr<td_api::starCount>> &&promise);

  void toggle_monoforum_topic_nopaid_messages_exception(DialogId dialog_id,
                                                        SavedMessagesTopicId saved_messages_topic_id,
                                                        bool nopaid_messages_exception, bool refund_payments,
                                                        Promise<Unit> &&promise);

  void get_monoforum_message_author(MessageFullId message_full_id, Promise<td_api::object_ptr<td_api::user>> &&promise);

  void get_current_state(vector<td_api::object_ptr<td_api::Update>> &updates) const;

 private:
  static constexpr int32 MAX_GET_HISTORY = 100;  // server-side limit

  static constexpr int64 MIN_PINNED_TOPIC_ORDER = static_cast<int64>(2147000000) << 32;

  struct SavedMessagesTopicInfo {
    DialogId peer_dialog_id_;
    MessageId last_topic_message_id_;

    unique_ptr<DraftMessage> draft_message_;
    MessageId read_inbox_max_message_id_;
    MessageId read_outbox_max_message_id_;
    int32 unread_count_ = 0;
    int32 unread_reaction_count_ = 0;
    bool is_marked_as_unread_ = false;
    bool nopaid_messages_exception_ = false;

    bool is_pinned_ = false;
  };

  struct SavedMessagesTopic {
    DialogId dialog_id_;
    SavedMessagesTopicId saved_messages_topic_id_;
    OrderedMessages ordered_messages_;
    MessageId last_message_id_;
    MessageId read_inbox_max_message_id_;
    MessageId read_outbox_max_message_id_;
    unique_ptr<DraftMessage> draft_message_;
    int32 local_message_count_ = 0;
    int32 server_message_count_ = 0;
    int32 sent_message_count_ = -1;
    int32 unread_count_ = 0;
    int32 unread_reaction_count_ = 0;
    int32 last_message_date_ = 0;
    int32 draft_message_date_ = 0;
    int64 pinned_order_ = 0;
    int64 private_order_ = 0;
    bool is_server_message_count_inited_ = false;
    bool is_marked_as_unread_ = false;
    bool nopaid_messages_exception_ = false;
    bool is_received_from_server_ = false;
    bool need_repair_unread_count_ = false;
    bool is_changed_ = false;
  };

  class TopicDate {
    int64 order_;
    SavedMessagesTopicId topic_id_;

   public:
    TopicDate(int64 order, SavedMessagesTopicId topic_id) : order_(order), topic_id_(topic_id) {
    }

    bool operator<(const TopicDate &other) const {
      return order_ > other.order_ ||
             (order_ == other.order_ && topic_id_.get_unique_id() > other.topic_id_.get_unique_id());
    }

    bool operator<=(const TopicDate &other) const {
      return order_ > other.order_ ||
             (order_ == other.order_ && topic_id_.get_unique_id() >= other.topic_id_.get_unique_id());
    }

    bool operator==(const TopicDate &other) const {
      return order_ == other.order_ && topic_id_ == other.topic_id_;
    }

    bool operator!=(const TopicDate &other) const {
      return !(*this == other);
    }

    SavedMessagesTopicId get_topic_id() const {
      return topic_id_;
    }
  };

  static const TopicDate MIN_TOPIC_DATE;
  static const TopicDate MAX_TOPIC_DATE;

  struct TopicList {
    DialogId dialog_id_;
    int32 server_total_count_ = -1;
    int32 sent_total_count_ = -1;
    uint32 generation_ = 0;

    vector<SavedMessagesTopicId> pinned_saved_messages_topic_ids_;
    bool are_pinned_saved_messages_topics_inited_ = false;

    std::set<TopicDate> ordered_topics_;

    TopicDate last_topic_date_ = MIN_TOPIC_DATE;  // in memory

    vector<Promise<Unit>> load_pinned_queries_;
    vector<Promise<Unit>> load_queries_;

    int32 offset_date_ = std::numeric_limits<int32>::max();
    DialogId offset_dialog_id_;
    MessageId offset_message_id_;

    FlatHashMap<SavedMessagesTopicId, unique_ptr<SavedMessagesTopic>, SavedMessagesTopicIdHash> topics_;
    FlatHashMap<SavedMessagesTopicId, vector<Promise<td_api::object_ptr<td_api::directMessagesChatTopic>>>,
                SavedMessagesTopicIdHash>
        get_topic_queries_;
  };

  void tear_down() final;

  SavedMessagesTopic *get_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id);

  const SavedMessagesTopic *get_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id) const;

  static SavedMessagesTopic *get_topic(TopicList *topic_list, SavedMessagesTopicId saved_messages_topic_id);

  static const SavedMessagesTopic *get_topic(const TopicList *topic_list, SavedMessagesTopicId saved_messages_topic_id);

  SavedMessagesTopic *add_topic(TopicList *topic_list, SavedMessagesTopicId saved_messages_topic_id, bool from_server);

  void get_pinned_saved_dialogs(int32 limit, Promise<Unit> &&promise);

  void on_get_pinned_saved_dialogs(Result<Unit> &&result);

  void get_saved_dialogs(TopicList *topic_list, int32 limit, Promise<Unit> &&promise);

  static SavedMessagesTopicInfo get_saved_messages_topic_info(
      Td *td, telegram_api::object_ptr<telegram_api::SavedDialog> &&dialog_ptr, bool is_saved_messages);

  void process_saved_messages_topics(DialogId dialog_id, uint32 generation,
                                     SavedMessagesTopicId expected_saved_messages_topic_id, bool is_pinned, int32 limit,
                                     int32 total_count,
                                     vector<telegram_api::object_ptr<telegram_api::SavedDialog>> &&dialogs,
                                     vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, bool is_last,
                                     Promise<Unit> &&promise);

  void on_get_saved_dialogs(TopicList *topic_list, Result<Unit> &&result);

  void on_get_monoforum_topic(DialogId dialog_id, uint32 generation, SavedMessagesTopicId saved_messages_topic_id,
                              Result<Unit> &&result);

  void on_get_topic_history(DialogId dialog_id, uint32 generation, SavedMessagesTopicId saved_messages_topic_id,
                            MessageId from_message_id, int32 offset, int32 limit, int32 left_tries,
                            Result<MessagesInfo> &&r_info, Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void reload_monoforum_topic(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id,
                              Promise<td_api::object_ptr<td_api::directMessagesChatTopic>> &&promise);

  void repair_topic_unread_count(const SavedMessagesTopic *topic);

  void read_topic_messages(SavedMessagesTopic *topic, MessageId read_inbox_max_message_id, int32 hint_unread_count);

  void do_set_topic_last_message_id(SavedMessagesTopic *topic, MessageId last_message_id, int32 last_message_date);

  void do_set_topic_read_inbox_max_message_id(SavedMessagesTopic *topic, MessageId read_inbox_max_message_id,
                                              int32 unread_count, const char *source);

  void do_set_topic_read_outbox_max_message_id(SavedMessagesTopic *topic, MessageId read_outbox_max_message_id);

  void do_set_topic_is_marked_as_unread(SavedMessagesTopic *topic, bool is_marked_as_unread);

  void do_set_topic_nopaid_messages_exception(SavedMessagesTopic *topic, bool nopaid_messages_exception);

  void do_set_topic_unread_reaction_count(SavedMessagesTopic *topic, int32 unread_reaction_count);

  void do_set_topic_draft_message(SavedMessagesTopic *topic, unique_ptr<DraftMessage> &&draft_message,
                                  bool from_update);

  void load_topics(TopicList *topic_list, int32 limit, Promise<Unit> &&promise);

  int64 get_next_pinned_saved_messages_topic_order();

  bool set_pinned_saved_messages_topics(vector<SavedMessagesTopicId> added_saved_messages_topic_ids);

  bool set_saved_messages_topic_is_pinned(SavedMessagesTopicId saved_messages_topic_id, bool is_pinned,
                                          const char *source);

  bool set_saved_messages_topic_is_pinned(SavedMessagesTopic *topic, bool is_pinned, const char *source);

  int32 get_pinned_saved_messages_topic_limit() const;

  int64 get_topic_order(int32 message_date, MessageId message_id);

  static int64 get_topic_public_order(const TopicList *topic_list, const SavedMessagesTopic *topic);

  void set_last_topic_date(TopicList *topic_list, TopicDate topic_date);

  void on_topic_changed(TopicList *topic_list, SavedMessagesTopic *topic, const char *source);

  void on_topic_message_count_changed(const SavedMessagesTopic *topic, const char *source);

  void update_topic_message_count(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id);

  void get_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id,
                         int32 offset, int32 limit, int32 left_tries,
                         Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void do_get_topic_history(const TopicList *topic_list, const SavedMessagesTopic *topic, DialogId dialog_id,
                            SavedMessagesTopicId saved_messages_topic_id, MessageId from_message_id, int32 offset,
                            int32 limit, int32 left_tries, Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void get_topic_message_by_date(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, int32 date,
                                 Promise<td_api::object_ptr<td_api::message>> &&promise);

  void delete_topic_history(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, Promise<Unit> &&promise);

  void delete_topic_messages_by_date(DialogId dialog_id, SavedMessagesTopicId saved_messages_topic_id, int32 min_date,
                                     int32 max_date, Promise<Unit> &&promise);

  td_api::object_ptr<td_api::savedMessagesTopic> get_saved_messages_topic_object(const SavedMessagesTopic *topic) const;

  td_api::object_ptr<td_api::updateSavedMessagesTopic> get_update_saved_messages_topic_object(
      const SavedMessagesTopic *topic) const;

  void send_update_saved_messages_topic(const TopicList *topic_list, const SavedMessagesTopic *topic,
                                        const char *source) const;

  td_api::object_ptr<td_api::directMessagesChatTopic> get_direct_messages_chat_topic_object(
      const TopicList *topic_list, const SavedMessagesTopic *topic) const;

  td_api::object_ptr<td_api::updateDirectMessagesChatTopic> get_update_direct_messages_chat_topic_object(
      const TopicList *topic_list, const SavedMessagesTopic *topic) const;

  td_api::object_ptr<td_api::updateSavedMessagesTopicCount> get_update_saved_messages_topic_count_object() const;

  void update_saved_messages_topic_sent_total_count(TopicList *topic_list, const char *source);

  td_api::object_ptr<td_api::updateTopicMessageCount> get_update_topic_message_count_object(
      const SavedMessagesTopic *topic) const;

  Status check_monoforum_dialog_id(DialogId dialog_id) const;

  Result<TopicList *> get_monoforum_topic_list(DialogId dialog_id);

  TopicList *get_topic_list(DialogId dialog_id);

  const TopicList *get_topic_list(DialogId dialog_id) const;

  TopicList *add_topic_list(DialogId dialog_id);

  Td *td_;
  ActorShared<> parent_;

  int64 current_pinned_saved_messages_topic_order_ = MIN_PINNED_TOPIC_ORDER;

  TopicList topic_list_;

  FlatHashMap<DialogId, unique_ptr<TopicList>, DialogIdHash> monoforum_topic_lists_;

  uint32 current_topic_list_generation_ = 0;
};

}  // namespace td
