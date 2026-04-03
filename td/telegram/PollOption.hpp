//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.hpp"
#include "td/telegram/PollOption.h"

#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void PollOption::store(StorerT &storer) const {
  using ::td::store;
  bool has_entities = !text_.entities.empty();
  bool has_added_by_dialog_id = added_by_dialog_id_ != DialogId();
  bool has_added_date = added_date_ != 0;
  bool has_recent_voter_dialog_ids = !recent_voter_dialog_ids_.empty();
  bool has_media = media_ != nullptr;
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_chosen_);
  STORE_FLAG(has_entities);
  STORE_FLAG(has_added_by_dialog_id);
  STORE_FLAG(has_added_date);
  STORE_FLAG(has_recent_voter_dialog_ids);
  STORE_FLAG(has_media);
  END_STORE_FLAGS();

  store(text_.text, storer);
  store(data_, storer);
  store(voter_count_, storer);
  if (has_entities) {
    store(text_.entities, storer);
  }
  if (has_added_by_dialog_id) {
    store(added_by_dialog_id_, storer);
  }
  if (has_added_date) {
    store(added_date_, storer);
  }
  if (has_recent_voter_dialog_ids) {
    store(recent_voter_dialog_ids_, storer);
  }
  if (has_media) {
    store_message_content(media_.get(), storer);
  }
}

template <class ParserT>
void PollOption::parse(ParserT &parser) {
  using ::td::parse;
  bool has_entities;
  bool has_added_by_dialog_id;
  bool has_added_date;
  bool has_recent_voter_dialog_ids;
  bool has_media;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_chosen_);
  PARSE_FLAG(has_entities);
  PARSE_FLAG(has_added_by_dialog_id);
  PARSE_FLAG(has_added_date);
  PARSE_FLAG(has_recent_voter_dialog_ids);
  PARSE_FLAG(has_media);
  END_PARSE_FLAGS();

  parse(text_.text, parser);
  parse(data_, parser);
  parse(voter_count_, parser);
  if (has_entities) {
    parse(text_.entities, parser);
  }
  if (has_added_by_dialog_id) {
    parse(added_by_dialog_id_, parser);
  }
  if (has_added_date) {
    parse(added_date_, parser);
  }
  if (has_recent_voter_dialog_ids) {
    parse(recent_voter_dialog_ids_, parser);
  }
  if (has_media) {
    parse_message_content(media_, parser);
  }
}

}  // namespace td
