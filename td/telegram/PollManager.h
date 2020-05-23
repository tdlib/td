//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/FullMessageId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/PollId.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/UserId.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/buffer.h"
#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace td {

struct BinlogEvent;

class Td;

class PollManager : public Actor {
 public:
  PollManager(Td *td, ActorShared<> parent);

  PollManager(const PollManager &) = delete;
  PollManager &operator=(const PollManager &) = delete;
  PollManager(PollManager &&) = delete;
  PollManager &operator=(PollManager &&) = delete;
  ~PollManager() override;

  static bool is_local_poll_id(PollId poll_id);

  PollId create_poll(string &&question, vector<string> &&options, bool is_anonymous, bool allow_multiple_answers,
                     bool is_quiz, int32 correct_option_id, FormattedText &&explanation, int32 open_period,
                     int32 close_date, bool is_closed);

  void register_poll(PollId poll_id, FullMessageId full_message_id, const char *source);

  void unregister_poll(PollId poll_id, FullMessageId full_message_id, const char *source);

  bool get_poll_is_closed(PollId poll_id) const;

  bool get_poll_is_anonymous(PollId poll_id) const;

  string get_poll_search_text(PollId poll_id) const;

  void set_poll_answer(PollId poll_id, FullMessageId full_message_id, vector<int32> &&option_ids,
                       Promise<Unit> &&promise);

  void get_poll_voters(PollId poll_id, FullMessageId full_message_id, int32 option_id, int32 offset, int32 limit,
                       Promise<std::pair<int32, vector<UserId>>> &&promise);

  void stop_poll(PollId poll_id, FullMessageId full_message_id, unique_ptr<ReplyMarkup> &&reply_markup,
                 Promise<Unit> &&promise);

  void stop_local_poll(PollId poll_id);

  bool has_input_media(PollId poll_id) const;

  tl_object_ptr<telegram_api::InputMedia> get_input_media(PollId poll_id) const;

  PollId on_get_poll(PollId poll_id, tl_object_ptr<telegram_api::poll> &&poll_server,
                     tl_object_ptr<telegram_api::pollResults> &&poll_results);

  void on_get_poll_vote(PollId poll_id, UserId user_id, vector<BufferSlice> &&options);

  td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id) const;

  void on_binlog_events(vector<BinlogEvent> &&events);

  static vector<int32> get_vote_percentage(const vector<int32> &voter_counts, int32 total_voter_count);

  template <class StorerT>
  void store_poll(PollId poll_id, StorerT &storer) const;

  template <class ParserT>
  PollId parse_poll(ParserT &parser);

 private:
  struct PollOption {
    string text;
    string data;
    int32 voter_count = 0;
    bool is_chosen = false;

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct Poll {
    string question;
    vector<PollOption> options;
    vector<UserId> recent_voter_user_ids;
    FormattedText explanation;
    int32 total_voter_count = 0;
    int32 correct_option_id = -1;
    int32 open_period = 0;
    int32 close_date = 0;
    bool is_anonymous = true;
    bool allow_multiple_answers = false;
    bool is_quiz = false;
    bool is_closed = false;
    bool is_updated_after_close = false;
    mutable bool was_saved = false;

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
  };

  struct PollOptionVoters {
    vector<UserId> voter_user_ids;
    string next_offset;
    vector<Promise<std::pair<int32, vector<UserId>>>> pending_queries;
    bool was_invalidated = false;  // the list needs to be invalidated when voters are changed
  };

  static constexpr int32 MAX_GET_POLL_VOTERS = 50;  // server side limit

  class SetPollAnswerLogEvent;
  class StopPollLogEvent;

  void start_up() override;
  void tear_down() override;

  static void on_update_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int);

  static void on_close_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int);

  static td_api::object_ptr<td_api::pollOption> get_poll_option_object(const PollOption &poll_option);

  static telegram_api::object_ptr<telegram_api::pollAnswer> get_input_poll_option(const PollOption &poll_option);

  static vector<PollOption> get_poll_options(vector<tl_object_ptr<telegram_api::pollAnswer>> &&poll_options);

  bool have_poll(PollId poll_id) const;

  bool have_poll_force(PollId poll_id);

  const Poll *get_poll(PollId poll_id) const;

  Poll *get_poll_editable(PollId poll_id);

  void notify_on_poll_update(PollId poll_id);

  static string get_poll_database_key(PollId poll_id);

  void save_poll(const Poll *poll, PollId poll_id);

  void on_load_poll_from_database(PollId poll_id, string value);

  double get_polling_timeout() const;

  void on_update_poll_timeout(PollId poll_id);

  void on_close_poll_timeout(PollId poll_id);

  void on_online();

  Poll *get_poll_force(PollId poll_id);

  td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id, const Poll *poll) const;

  void on_get_poll_results(PollId poll_id, uint64 generation, Result<tl_object_ptr<telegram_api::Updates>> result);

  void do_set_poll_answer(PollId poll_id, FullMessageId full_message_id, vector<string> &&options, uint64 logevent_id,
                          Promise<Unit> &&promise);

  void on_set_poll_answer(PollId poll_id, uint64 generation, Result<tl_object_ptr<telegram_api::Updates>> &&result);

  void invalidate_poll_voters(const Poll *poll, PollId poll_id);

  void invalidate_poll_option_voters(const Poll *poll, PollId poll_id, size_t option_index);

  PollOptionVoters &get_poll_option_voters(const Poll *poll, PollId poll_id, int32 option_id);

  void on_get_poll_voters(PollId poll_id, int32 option_id, string offset, int32 limit,
                          Result<tl_object_ptr<telegram_api::messages_votesList>> &&result);

  void do_stop_poll(PollId poll_id, FullMessageId full_message_id, unique_ptr<ReplyMarkup> &&reply_markup,
                    uint64 logevent_id, Promise<Unit> &&promise);

  MultiTimeout update_poll_timeout_{"UpdatePollTimeout"};
  MultiTimeout close_poll_timeout_{"ClosePollTimeout"};

  Td *td_;
  ActorShared<> parent_;
  std::unordered_map<PollId, unique_ptr<Poll>, PollIdHash> polls_;

  std::unordered_map<PollId, std::unordered_set<FullMessageId, FullMessageIdHash>, PollIdHash> poll_messages_;

  struct PendingPollAnswer {
    vector<string> options_;
    vector<Promise<Unit>> promises_;
    uint64 generation_ = 0;
    uint64 logevent_id_ = 0;
    NetQueryRef query_ref_;
  };
  std::unordered_map<PollId, PendingPollAnswer, PollIdHash> pending_answers_;

  std::unordered_map<PollId, vector<PollOptionVoters>, PollIdHash> poll_voters_;

  int64 current_local_poll_id_ = 0;

  uint64 current_generation_ = 0;

  std::unordered_set<PollId, PollIdHash> loaded_from_database_polls_;

  std::unordered_set<PollId, PollIdHash> being_closed_polls_;
};

}  // namespace td
