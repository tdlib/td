//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/ChannelId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/files/FileId.h"
#include "td/telegram/ForumTopicId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/MessageFullId.h"
#include "td/telegram/MessageId.h"
#include "td/telegram/MinChannel.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/PollId.h"
#include "td/telegram/PollOption.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/MultiTimeout.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/FlatHashMap.h"
#include "td/utils/FlatHashSet.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"
#include "td/utils/WaitFreeHashMap.h"
#include "td/utils/WaitFreeHashSet.h"

#include <utility>

namespace td {

struct BinlogEvent;
class MessageContent;
class Td;

class PollManager final : public Actor {
 public:
  PollManager(Td *td, ActorShared<> parent);

  PollManager(const PollManager &) = delete;
  PollManager &operator=(const PollManager &) = delete;
  PollManager(PollManager &&) = delete;
  PollManager &operator=(PollManager &&) = delete;
  ~PollManager() final;

  static bool is_local_poll_id(PollId poll_id);

  static Status check_quiz_correct_option_ids(const vector<int32> &correct_option_ids, size_t option_count,
                                              bool allow_empty);

  PollId create_poll(FormattedText &&question, vector<FormattedText> &&options, bool is_anonymous,
                     bool allow_multiple_answers, bool has_open_answers, bool has_revoting_disabled,
                     bool shuffle_answers, bool hide_results_until_close, bool is_quiz,
                     vector<int32> correct_option_ids, FormattedText &&explanation,
                     unique_ptr<MessageContent> &&explanation_media, int32 open_period, int32 close_date,
                     bool is_closed);

  void register_poll(PollId poll_id, MessageFullId message_full_id, const char *source);

  void unregister_poll(PollId poll_id, MessageFullId message_full_id, const char *source);

  void register_reply_poll(PollId poll_id);

  void unregister_reply_poll(PollId poll_id);

  bool get_poll_is_closed(PollId poll_id) const;

  bool get_poll_is_anonymous(PollId poll_id) const;

  bool get_poll_can_add_option(PollId poll_id) const;

  bool get_poll_has_unread_votes(PollId poll_id) const;

  void remove_poll_has_unread_votes(PollId poll_id);

  void get_poll_option_properties(PollId poll_id, const string &option_id, DialogId dialog_id, MessageId message_id,
                                  bool can_be_replied, bool can_be_replied_in_another_chat, bool can_get_link,
                                  bool is_forward, bool is_outgoing,
                                  Promise<td_api::object_ptr<td_api::pollOptionProperties>> &&promise);

  string get_poll_search_text(PollId poll_id) const;

  vector<FileId> get_poll_file_ids(PollId poll_id) const;

  void add_poll_option(PollId poll_id, MessageFullId message_full_id,
                       td_api::object_ptr<td_api::inputPollOption> &&option, Promise<Unit> &&promise);

  void delete_poll_option(PollId poll_id, MessageFullId message_full_id, const string &option_id,
                          Promise<Unit> &&promise);

  void set_poll_answer(PollId poll_id, MessageFullId message_full_id, vector<int32> &&option_ids,
                       Promise<Unit> &&promise);

  void get_poll_voters(PollId poll_id, MessageFullId message_full_id, int32 option_id, int32 offset, int32 limit,
                       Promise<td_api::object_ptr<td_api::pollVoters>> &&promise);

  void stop_poll(PollId poll_id, MessageFullId message_full_id, unique_ptr<ReplyMarkup> &&reply_markup,
                 Promise<Unit> &&promise);

  void stop_local_poll(PollId poll_id);

  PollId dup_poll(DialogId dialog_id, PollId poll_id);

  bool has_input_media(PollId poll_id) const;

  tl_object_ptr<telegram_api::InputMedia> get_input_media(PollId poll_id) const;

  PollId on_get_poll(PollId poll_id, tl_object_ptr<telegram_api::poll> &&poll_server,
                     tl_object_ptr<telegram_api::pollResults> &&poll_results, const char *source);

  void on_update_poll(PollId poll_id, telegram_api::object_ptr<telegram_api::poll> &&poll_server,
                      telegram_api::object_ptr<telegram_api::pollResults> &&poll_results, MessageFullId message_full_id,
                      ForumTopicId forum_topic_id, const char *source);

  void on_get_poll_vote(PollId poll_id, DialogId dialog_id, vector<BufferSlice> &&options, vector<int32> positions);

  td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id) const;

  void on_binlog_events(vector<BinlogEvent> &&events);

  static vector<int32> get_vote_percentage(const vector<int32> &voter_counts, int32 total_voter_count);

  template <class StorerT>
  void store_poll(PollId poll_id, StorerT &storer) const;

  template <class ParserT>
  PollId parse_poll(ParserT &parser);

 private:
  struct Poll {
    FormattedText question_;
    vector<PollOption> options_;
    vector<std::pair<ChannelId, MinChannel>> option_min_channels_;
    vector<std::pair<ChannelId, MinChannel>> recent_option_voter_min_channels_;
    vector<DialogId> recent_voter_dialog_ids_;
    vector<std::pair<ChannelId, MinChannel>> recent_voter_min_channels_;
    FormattedText explanation_;
    unique_ptr<MessageContent> explanation_media_;
    vector<int32> correct_option_ids_;
    int32 total_voter_count_ = 0;
    int32 open_period_ = 0;
    int32 close_date_ = 0;
    int64 hash_ = 0;
    bool is_anonymous_ = true;
    bool allow_multiple_answers_ = false;
    bool has_open_answers_ = false;
    bool has_revoting_disabled_ = false;
    bool shuffle_answers_ = false;
    bool hide_results_until_close_ = false;
    bool is_quiz_ = false;
    bool is_closed_ = false;
    bool is_updated_after_close_ = false;
    bool is_creator_ = false;
    bool has_unread_votes_ = false;
    mutable bool was_saved_ = false;

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct PollOptionVoters {
    vector<std::pair<DialogId, int32>> voters_;
    string next_offset_;
    vector<Promise<td_api::object_ptr<td_api::pollVoters>>> pending_queries_;
    bool was_invalidated_ = false;  // the list needs to be invalidated when voters are changed
  };

  static constexpr int32 MAX_GET_POLL_VOTERS = 50;  // server-side limit
  static constexpr int32 UNLOAD_POLL_DELAY = 600;   // some reasonable value

  class SetPollAnswerLogEvent;
  class StopPollLogEvent;

  void start_up() final;
  void tear_down() final;

  static void on_update_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int);

  static void on_close_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int);

  static void on_unload_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int);

  bool have_poll(PollId poll_id) const;

  bool have_poll_force(PollId poll_id);

  const Poll *get_poll(PollId poll_id) const;

  const Poll *get_poll(PollId poll_id);

  Poll *get_poll_editable(PollId poll_id);

  bool can_unload_poll(PollId poll_id);

  void schedule_poll_unload(PollId poll_id);

  void notify_on_poll_update(PollId poll_id);

  void notify_on_poll_has_unread_votes_update(PollId poll_id, bool has_unread_votes);

  static string get_poll_database_key(PollId poll_id);

  static void save_poll(const Poll *poll, PollId poll_id);

  void on_load_poll_from_database(PollId poll_id, string value);

  double get_polling_timeout() const;

  void on_update_poll_timeout(PollId poll_id);

  void on_close_poll_timeout(PollId poll_id);

  void on_unload_poll_timeout(PollId poll_id);

  void on_online();

  Poll *get_poll_force(PollId poll_id);

  bool can_delete_poll_option(const Poll *poll, const PollOption *option, MessageId message_id, bool is_forward,
                              bool is_outgoing);

  td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id, const Poll *poll) const;

  void on_get_poll_results(PollId poll_id, uint64 generation, Result<tl_object_ptr<telegram_api::Updates>> result);

  void do_set_poll_answer(PollId poll_id, MessageFullId message_full_id, vector<string> &&options, uint64 log_event_id,
                          Promise<Unit> &&promise);

  void on_set_poll_answer(PollId poll_id, uint64 generation, Result<tl_object_ptr<telegram_api::Updates>> &&result);

  void on_set_poll_answer_finished(PollId poll_id, Result<Unit> &&result, uint64 generation);

  void invalidate_poll_voters(const Poll *poll, PollId poll_id);

  void invalidate_poll_option_voters(const Poll *poll, PollId poll_id, size_t option_index);

  PollOptionVoters &get_poll_option_voters(const Poll *poll, PollId poll_id, int32 option_id);

  td_api::object_ptr<td_api::pollVoters> get_poll_voters_object(int32 total_count,
                                                                const vector<std::pair<DialogId, int32>> &voters) const;

  void on_get_poll_voters(PollId poll_id, int32 option_id, string offset, int32 limit,
                          Result<tl_object_ptr<telegram_api::messages_votesList>> &&result);

  void do_stop_poll(PollId poll_id, MessageFullId message_full_id, unique_ptr<ReplyMarkup> &&reply_markup,
                    uint64 log_event_id, Promise<Unit> &&promise);

  void on_stop_poll_finished(PollId poll_id, MessageFullId message_full_id, uint64 log_event_id, Result<Unit> &&result,
                             Promise<Unit> &&promise);

  void forget_local_poll(PollId poll_id);

  bool can_get_poll_voters(PollId poll_id, const Poll *poll) const;

  MultiTimeout update_poll_timeout_{"UpdatePollTimeout"};
  MultiTimeout close_poll_timeout_{"ClosePollTimeout"};
  MultiTimeout unload_poll_timeout_{"UnloadPollTimeout"};

  WaitFreeHashMap<PollId, unique_ptr<Poll>, PollIdHash> polls_;

  WaitFreeHashMap<PollId, WaitFreeHashSet<MessageFullId, MessageFullIdHash>, PollIdHash> server_poll_messages_;
  WaitFreeHashMap<PollId, WaitFreeHashSet<MessageFullId, MessageFullIdHash>, PollIdHash> other_poll_messages_;

  WaitFreeHashMap<PollId, int32, PollIdHash> reply_poll_counts_;

  struct PendingPollAnswer {
    vector<string> options_;
    vector<Promise<Unit>> promises_;
    uint64 generation_ = 0;
    uint64 log_event_id_ = 0;
    NetQueryRef query_ref_;
    bool is_finished_ = false;
  };
  FlatHashMap<PollId, PendingPollAnswer, PollIdHash> pending_answers_;

  FlatHashMap<PollId, vector<PollOptionVoters>, PollIdHash> poll_voters_;

  int64 current_local_poll_id_ = 0;

  uint64 current_generation_ = 0;

  FlatHashSet<PollId, PollIdHash> loaded_from_database_polls_;

  FlatHashSet<PollId, PollIdHash> being_closed_polls_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
