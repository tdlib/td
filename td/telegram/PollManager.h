//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/PollId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"
#include "td/actor/PromiseFuture.h"

#include "td/utils/common.h"

#include <unordered_map>
#include <unordered_set>

namespace td {

class Td;

class PollManager : public Actor {
 public:
  PollManager(Td *td, ActorShared<> parent);

  PollManager(const PollManager &) = delete;
  PollManager &operator=(const PollManager &) = delete;
  PollManager(PollManager &&) = delete;
  PollManager &operator=(PollManager &&) = delete;
  ~PollManager() override;

  PollId create_poll(string &&question, vector<string> &&answers);

  tl_object_ptr<telegram_api::InputMedia> get_input_media(PollId poll_id) const;

  PollId on_get_poll(PollId poll_id, tl_object_ptr<telegram_api::poll> &&poll_server,
                     tl_object_ptr<telegram_api::pollResults> &&poll_results);

  td_api::object_ptr<td_api::poll> get_poll_object(PollId poll_id) const;

  template <class StorerT>
  void store_poll(PollId poll_id, StorerT &storer) const;

  template <class ParserT>
  PollId parse_poll(ParserT &parser);

 private:
  struct PollAnswer {
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
    vector<PollAnswer> answers;
    int32 total_voter_count = 0;
    bool is_closed = false;

    template <class StorerT>
    void store(StorerT &storer) const;
    template <class ParserT>
    void parse(ParserT &parser);
  };

  void tear_down() override;

  static bool is_local_poll_id(PollId poll_id);

  static td_api::object_ptr<td_api::pollAnswer> get_poll_answer_object(const PollAnswer &poll_answer);

  static telegram_api::object_ptr<telegram_api::pollAnswer> get_input_poll_answer(const PollAnswer &poll_answer);

  static vector<PollAnswer> get_poll_answers(vector<tl_object_ptr<telegram_api::pollAnswer>> &&poll_answers);

  bool have_poll(PollId poll_id) const;

  bool have_poll_force(PollId poll_id);

  const Poll *get_poll(PollId poll_id) const;

  Poll *get_poll_editable(PollId poll_id);

  static string get_poll_database_key(PollId poll_id);

  void save_poll(const Poll *poll, PollId poll_id);

  void on_load_poll_from_database(PollId poll_id, string value);

  Poll *get_poll_force(PollId poll_id);

  Td *td_;
  ActorShared<> parent_;
  std::unordered_map<PollId, unique_ptr<Poll>, PollIdHash> polls_;

  int64 current_local_poll_id_ = 0;

  std::unordered_set<PollId, PollIdHash> loaded_from_database_polls_;
};

}  // namespace td
