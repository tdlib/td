//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageId.h"
#include "td/telegram/net/NetQuery.h"
#include "td/telegram/PollId.h"
#include "td/telegram/ReplyMarkup.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"
#include "td/actor/Timeout.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

#include <unordered_map>
#include <unordered_set>

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

  PollId create_poll(string &&question, vector<string> &&options);

  void register_poll(PollId poll_id, FullMessageId full_message_id);

  void unregister_poll(PollId poll_id, FullMessageId full_message_id);

  bool get_poll_is_closed(PollId poll_id) const;

  string get_poll_search_text(PollId poll_id) const;

  void set_poll_answer(PollId poll_id, FullMessageId full_message_id, vector<int32> &&option_ids,
                       Promise<Unit> &&promise);

  void stop_poll(PollId poll_id, FullMessageId full_message_id, unique_ptr<ReplyMarkup> &&reply_markup,
                 Promise<Unit> &&promise);

  void stop_local_poll(PollId poll_id);

  tl_object_ptr<telegram_api::InputMedia> get_input_media(PollId poll_id) const;

  PollId on_get_poll(PollId poll_id, tl_object_ptr<telegram_api::poll> &&poll_server,
                     tl_object_ptr<telegram_api::pollResults> &&poll_results);

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
    int32 total_voter_count = 0;
    bool is_closed = false;

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
  };

  class SetPollAnswerLogEvent;
  class StopPollLogEvent;

  void start_up() override;
  void tear_down() override;

  static void on_update_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int);

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

  void on_online();

  Poll *get_poll_force(PollId poll_id);

  td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id, const Poll *poll) const;

  void on_get_poll_results(PollId poll_id, uint64 generation, Result<tl_object_ptr<telegram_api::Updates>> result);

  void do_set_poll_answer(PollId poll_id, FullMessageId full_message_id, vector<string> &&options, uint64 logevent_id,
                          Promise<Unit> &&promise);

  void on_set_poll_answer(PollId poll_id, uint64 generation, Result<tl_object_ptr<telegram_api::Updates>> &&result);

  void do_stop_poll(PollId poll_id, FullMessageId full_message_id, unique_ptr<ReplyMarkup> &&reply_markup,
                    uint64 logevent_id, Promise<Unit> &&promise);

  MultiTimeout update_poll_timeout_{"UpdatePollTimeout"};

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

  int64 current_local_poll_id_ = 0;

  uint64 current_generation_ = 0;

  std::unordered_set<PollId, PollIdHash> loaded_from_database_polls_;

  std::unordered_set<PollId, PollIdHash> being_closed_polls_;
};

}  // namespace td
