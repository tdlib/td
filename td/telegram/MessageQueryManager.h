//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/AffectedHistory.h"
#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/DialogListId.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MessageSearchFilter.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"

namespace td {

struct BinlogEvent;
struct MessageSearchOffset;
class Td;

class MessageQueryManager final : public Actor {
 public:
  MessageQueryManager(Td *td, ActorShared<> parent);

  using AffectedHistoryQuery = std::function<void(DialogId, Promise<AffectedHistory>)>;

  void run_affected_history_query_until_complete(DialogId dialog_id, AffectedHistoryQuery query,
                                                 bool get_affected_messages, Promise<Unit> &&promise);

  void report_message_delivery(MessageFullId message_full_id, int32 until_date, bool from_push);

  void search_messages(DialogListId dialog_list_id, bool ignore_folder_id, const string &query,
                       const string &offset_str, int32 limit, MessageSearchFilter filter,
                       td_api::object_ptr<td_api::SearchMessagesChatTypeFilter> &&dialog_type_filter, int32 min_date,
                       int32 max_date, Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void on_get_messages_search_result(const string &query, int32 offset_date, DialogId offset_dialog_id,
                                     MessageId offset_message_id, int32 limit, MessageSearchFilter filter,
                                     int32 min_date, int32 max_date, int32 total_count,
                                     vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
                                     int32 next_rate, Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void search_outgoing_document_messages(const string &query, int32 limit,
                                         Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void on_get_outgoing_document_messages(vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
                                         Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void search_hashtag_posts(string hashtag, string offset_str, int32 limit,
                            Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void on_get_hashtag_search_result(const string &hashtag, const MessageSearchOffset &old_offset, int32 limit,
                                    int32 total_count,
                                    vector<telegram_api::object_ptr<telegram_api::Message>> &&messages, int32 next_rate,
                                    Promise<td_api::object_ptr<td_api::foundMessages>> &&promise);

  void search_dialog_recent_location_messages(DialogId dialog_id, int32 limit,
                                              Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void on_get_recent_locations(DialogId dialog_id, int32 limit, int32 total_count,
                               vector<telegram_api::object_ptr<telegram_api::Message>> &&messages,
                               Promise<td_api::object_ptr<td_api::messages>> &&promise);

  void delete_all_call_messages_on_server(bool revoke, uint64 log_event_id, Promise<Unit> &&promise);

  void delete_all_channel_messages_by_sender_on_server(ChannelId channel_id, DialogId sender_dialog_id,
                                                       uint64 log_event_id, Promise<Unit> &&promise);

  void delete_dialog_history_on_server(DialogId dialog_id, MessageId max_message_id, bool remove_from_dialog_list,
                                       bool revoke, bool allow_error, uint64 log_event_id, Promise<Unit> &&promise);

  void delete_dialog_messages_by_date_on_server(DialogId dialog_id, int32 min_date, int32 max_date, bool revoke,
                                                uint64 log_event_id, Promise<Unit> &&promise);

  void delete_topic_history_on_server(DialogId dialog_id, MessageId top_thread_message_id, uint64 log_event_id,
                                      Promise<Unit> &&promise);

  void read_all_dialog_mentions_on_server(DialogId dialog_id, uint64 log_event_id, Promise<Unit> &&promise);

  void read_all_dialog_reactions_on_server(DialogId dialog_id, uint64 log_event_id, Promise<Unit> &&promise);

  void read_all_topic_mentions_on_server(DialogId dialog_id, MessageId top_thread_message_id, uint64 log_event_id,
                                         Promise<Unit> &&promise);

  void read_all_topic_reactions_on_server(DialogId dialog_id, MessageId top_thread_message_id, uint64 log_event_id,
                                          Promise<Unit> &&promise);

  void on_binlog_events(vector<BinlogEvent> &&events);

 private:
  class DeleteAllCallMessagesOnServerLogEvent;
  class DeleteAllChannelMessagesFromSenderOnServerLogEvent;
  class DeleteDialogHistoryOnServerLogEvent;
  class DeleteDialogMessagesByDateOnServerLogEvent;
  class DeleteTopicHistoryOnServerLogEvent;
  class ReadAllDialogMentionsOnServerLogEvent;
  class ReadAllDialogReactionsOnServerLogEvent;

  static constexpr int32 MAX_SEARCH_MESSAGES = 100;  // server-side limit

  void tear_down() final;

  void on_get_affected_history(DialogId dialog_id, AffectedHistoryQuery query, bool get_affected_messages,
                               AffectedHistory affected_history, Promise<Unit> &&promise);

  static uint64 save_delete_all_call_messages_on_server_log_event(bool revoke);

  static uint64 save_delete_all_channel_messages_by_sender_on_server_log_event(ChannelId channel_id,
                                                                               DialogId sender_dialog_id);

  static uint64 save_delete_dialog_history_on_server_log_event(DialogId dialog_id, MessageId max_message_id,
                                                               bool remove_from_dialog_list, bool revoke);

  static uint64 save_delete_dialog_messages_by_date_on_server_log_event(DialogId dialog_id, int32 min_date,
                                                                        int32 max_date, bool revoke);

  static uint64 save_delete_topic_history_on_server_log_event(DialogId dialog_id, MessageId top_thread_message_id);

  static uint64 save_read_all_dialog_mentions_on_server_log_event(DialogId dialog_id);

  static uint64 save_read_all_dialog_reactions_on_server_log_event(DialogId dialog_id);

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
