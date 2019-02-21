//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/PollManager.h"

#include "td/utils/common.h"
#include "td/utils/misc.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void PollManager::PollOption::store(StorerT &storer) const {
  using ::td::store;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_chosen);
  END_STORE_FLAGS();

  store(text, storer);
  store(data, storer);
  store(voter_count, storer);
}

template <class ParserT>
void PollManager::PollOption::parse(ParserT &parser) {
  using ::td::parse;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_chosen);
  END_PARSE_FLAGS();

  parse(text, parser);
  parse(data, parser);
  parse(voter_count, parser);
}

template <class StorerT>
void PollManager::Poll::store(StorerT &storer) const {
  using ::td::store;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_closed);
  END_STORE_FLAGS();

  store(question, storer);
  store(options, storer);
  store(total_voter_count, storer);
}

template <class ParserT>
void PollManager::Poll::parse(ParserT &parser) {
  using ::td::parse;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_closed);
  END_PARSE_FLAGS();

  parse(question, parser);
  parse(options, parser);
  parse(total_voter_count, parser);
}

template <class StorerT>
void PollManager::store_poll(PollId poll_id, StorerT &storer) const {
  td::store(poll_id.get(), storer);
  if (is_local_poll_id(poll_id)) {
    auto poll = get_poll(poll_id);
    CHECK(poll != nullptr);
    store(poll->question, storer);
    vector<string> options = transform(poll->options, [](const PollOption &option) { return option.text; });
    store(options, storer);
  }
}

template <class ParserT>
PollId PollManager::parse_poll(ParserT &parser) {
  int64 poll_id_int;
  td::parse(poll_id_int, parser);
  PollId poll_id(poll_id_int);
  if (is_local_poll_id(poll_id)) {
    string question;
    vector<string> options;
    parse(question, parser);
    parse(options, parser);
    return create_poll(std::move(question), std::move(options));
  }

  auto poll = get_poll_force(poll_id);
  if (poll == nullptr) {
    return PollId();
  }
  return poll_id;
}

}  // namespace td
