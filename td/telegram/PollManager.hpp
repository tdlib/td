//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageContent.h"
#include "td/telegram/MessageEntity.hpp"
#include "td/telegram/MinChannel.hpp"
#include "td/telegram/PollManager.h"
#include "td/telegram/PollOption.hpp"
#include "td/telegram/UserId.h"
#include "td/telegram/Version.h"

#include "td/utils/algorithm.h"
#include "td/utils/common.h"
#include "td/utils/tl_helpers.h"

namespace td {

template <class StorerT>
void PollManager::Poll::store(StorerT &storer) const {
  using ::td::store;
  bool is_public = !is_anonymous_;
  bool has_open_period = open_period_ != 0;
  bool has_close_date = close_date_ != 0;
  bool has_explanation = !explanation_.text.empty();
  bool has_recent_voter_dialog_ids = !recent_voter_dialog_ids_.empty();
  bool has_recent_voter_min_channels = !recent_voter_min_channels_.empty();
  bool has_question_entities = !question_.entities.empty();
  bool has_multiple_correct_option_ids = correct_option_ids_.size() > 1u;
  bool know_revoting_disabled = true;
  bool has_hash = hash_ != 0;
  bool has_explanation_media = explanation_media_ != nullptr;
  bool has_option_min_channels = !option_min_channels_.empty();
  bool has_recent_option_voter_min_channels = !recent_option_voter_min_channels_.empty();
  BEGIN_STORE_FLAGS();
  STORE_FLAG(is_closed_);
  STORE_FLAG(is_public);
  STORE_FLAG(allow_multiple_answers_);
  STORE_FLAG(is_quiz_);
  STORE_FLAG(false);
  STORE_FLAG(has_open_period);
  STORE_FLAG(has_close_date);
  STORE_FLAG(has_explanation);
  STORE_FLAG(is_updated_after_close_);
  STORE_FLAG(has_recent_voter_dialog_ids);
  STORE_FLAG(has_recent_voter_min_channels);
  STORE_FLAG(has_question_entities);
  STORE_FLAG(has_multiple_correct_option_ids);
  STORE_FLAG(has_open_answers_);
  STORE_FLAG(know_revoting_disabled);
  STORE_FLAG(has_revoting_disabled_);
  STORE_FLAG(shuffle_answers_);
  STORE_FLAG(hide_results_until_close_);
  STORE_FLAG(is_creator_);
  STORE_FLAG(has_hash);
  STORE_FLAG(has_unread_votes_);
  STORE_FLAG(has_explanation_media);
  STORE_FLAG(has_option_min_channels);
  STORE_FLAG(has_recent_option_voter_min_channels);
  END_STORE_FLAGS();

  store(question_.text, storer);
  store(options_, storer);
  store(total_voter_count_, storer);
  if (is_quiz_) {
    if (has_multiple_correct_option_ids) {
      store(correct_option_ids_, storer);
    } else {
      store(correct_option_ids_.empty() ? -1 : correct_option_ids_[0], storer);
    }
  }
  if (has_open_period) {
    store(open_period_, storer);
  }
  if (has_close_date) {
    store(close_date_, storer);
  }
  if (has_explanation) {
    store(explanation_, storer);
  }
  if (has_recent_voter_dialog_ids) {
    store(recent_voter_dialog_ids_, storer);
  }
  if (has_recent_voter_min_channels) {
    store(recent_voter_min_channels_, storer);
  }
  if (has_question_entities) {
    store(question_.entities, storer);
  }
  if (has_hash) {
    store(hash_, storer);
  }
  if (has_explanation_media) {
    store_message_content(explanation_media_.get(), storer);
  }
  if (has_option_min_channels) {
    store(option_min_channels_, storer);
  }
  if (has_recent_option_voter_min_channels) {
    store(recent_option_voter_min_channels_, storer);
  }
}

template <class ParserT>
void PollManager::Poll::parse(ParserT &parser) {
  using ::td::parse;
  bool is_public;
  bool has_recent_voter_user_ids;
  bool has_open_period;
  bool has_close_date;
  bool has_explanation;
  bool has_recent_voter_dialog_ids;
  bool has_recent_voter_min_channels;
  bool has_question_entities;
  bool has_multiple_correct_option_ids;
  bool know_revoting_disabled;
  bool has_hash;
  bool has_explanation_media;
  bool has_option_min_channels;
  bool has_recent_option_voter_min_channels;
  BEGIN_PARSE_FLAGS();
  PARSE_FLAG(is_closed_);
  PARSE_FLAG(is_public);
  PARSE_FLAG(allow_multiple_answers_);
  PARSE_FLAG(is_quiz_);
  PARSE_FLAG(has_recent_voter_user_ids);
  PARSE_FLAG(has_open_period);
  PARSE_FLAG(has_close_date);
  PARSE_FLAG(has_explanation);
  PARSE_FLAG(is_updated_after_close_);
  PARSE_FLAG(has_recent_voter_dialog_ids);
  PARSE_FLAG(has_recent_voter_min_channels);
  PARSE_FLAG(has_question_entities);
  PARSE_FLAG(has_multiple_correct_option_ids);
  PARSE_FLAG(has_open_answers_);
  PARSE_FLAG(know_revoting_disabled);
  PARSE_FLAG(has_revoting_disabled_);
  PARSE_FLAG(shuffle_answers_);
  PARSE_FLAG(hide_results_until_close_);
  PARSE_FLAG(is_creator_);
  PARSE_FLAG(has_hash);
  PARSE_FLAG(has_unread_votes_);
  PARSE_FLAG(has_explanation_media);
  PARSE_FLAG(has_option_min_channels);
  PARSE_FLAG(has_recent_option_voter_min_channels);
  END_PARSE_FLAGS();
  is_anonymous_ = !is_public;

  parse(question_.text, parser);
  parse(options_, parser);
  parse(total_voter_count_, parser);
  if (is_quiz_) {
    if (has_multiple_correct_option_ids) {
      parse(correct_option_ids_, parser);
    } else {
      int32 correct_option_id;
      parse(correct_option_id, parser);
      if (correct_option_id != -1) {
        correct_option_ids_.push_back(correct_option_id);
      }
    }
    auto status = check_quiz_correct_option_ids(correct_option_ids_, options_.size(), true);
    if (status.is_error()) {
      parser.set_error(status.message().str());
      return;
    }
  }
  if (has_recent_voter_user_ids) {
    vector<UserId> recent_voter_user_ids;
    parse(recent_voter_user_ids, parser);
    recent_voter_dialog_ids_ = DialogId::get_dialog_ids(recent_voter_user_ids);
  }
  if (has_open_period) {
    parse(open_period_, parser);
  }
  if (has_close_date) {
    parse(close_date_, parser);
  }
  if (has_explanation) {
    parse(explanation_, parser);
  }
  if (has_recent_voter_dialog_ids) {
    parse(recent_voter_dialog_ids_, parser);
  }
  if (has_recent_voter_min_channels) {
    parse(recent_voter_min_channels_, parser);
  }
  if (has_question_entities) {
    parse(question_.entities, parser);
  }
  if (!know_revoting_disabled) {
    has_revoting_disabled_ = is_quiz_;
  }
  if (has_hash) {
    parse(hash_, parser);
  }
  if (has_explanation_media) {
    parse_message_content(explanation_media_, parser);
  }
  if (has_option_min_channels) {
    parse(option_min_channels_, parser);
  }
  if (has_recent_option_voter_min_channels) {
    parse(recent_option_voter_min_channels_, parser);
  }
}

template <class StorerT>
void PollManager::store_poll(PollId poll_id, StorerT &storer) const {
  td::store(poll_id.get(), storer);
  if (is_local_poll_id(poll_id)) {
    auto poll = get_poll(poll_id);
    CHECK(poll != nullptr);
    bool has_open_period = poll->open_period_ != 0;
    bool has_close_date = poll->close_date_ != 0;
    bool has_explanation = !poll->explanation_.text.empty();
    bool has_question_entities = !poll->question_.entities.empty();
    bool has_option_entities =
        any_of(poll->options_, [](const auto &option) { return !option.text_.entities.empty(); });
    bool has_multiple_correct_option_ids = poll->correct_option_ids_.size() > 1u;
    bool know_revoting_disabled = true;
    bool has_explanation_media = poll->explanation_media_ != nullptr;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(poll->is_closed_);
    STORE_FLAG(poll->is_anonymous_);
    STORE_FLAG(poll->allow_multiple_answers_);
    STORE_FLAG(poll->is_quiz_);
    STORE_FLAG(has_open_period);
    STORE_FLAG(has_close_date);
    STORE_FLAG(has_explanation);
    STORE_FLAG(has_question_entities);
    STORE_FLAG(has_option_entities);
    STORE_FLAG(has_multiple_correct_option_ids);
    STORE_FLAG(poll->has_open_answers_);
    STORE_FLAG(know_revoting_disabled);
    STORE_FLAG(poll->has_revoting_disabled_);
    STORE_FLAG(poll->shuffle_answers_);
    STORE_FLAG(poll->hide_results_until_close_);
    STORE_FLAG(has_explanation_media);
    END_STORE_FLAGS();
    store(poll->question_.text, storer);
    vector<string> options = transform(poll->options_, [](const PollOption &option) { return option.text_.text; });
    store(options, storer);
    if (poll->is_quiz_) {
      if (has_multiple_correct_option_ids) {
        store(poll->correct_option_ids_, storer);
      } else {
        store(poll->correct_option_ids_.empty() ? -1 : poll->correct_option_ids_[0], storer);
      }
    }
    if (has_open_period) {
      store(poll->open_period_, storer);
    }
    if (has_close_date) {
      store(poll->close_date_, storer);
    }
    if (has_explanation) {
      store(poll->explanation_, storer);
    }
    if (has_question_entities) {
      store(poll->question_.entities, storer);
    }
    if (has_option_entities) {
      auto option_entities = transform(poll->options_, [](const PollOption &option) { return option.text_.entities; });
      store(option_entities, storer);
    }
    if (has_explanation_media) {
      store_message_content(poll->explanation_media_.get(), storer);
    }
  }
}

template <class ParserT>
PollId PollManager::parse_poll(ParserT &parser) {
  int64 poll_id_int;
  td::parse(poll_id_int, parser);
  PollId poll_id(poll_id_int);
  if (is_local_poll_id(poll_id)) {
    FormattedText question;
    FormattedText explanation;
    unique_ptr<MessageContent> explanation_media;
    int32 open_period = 0;
    int32 close_date = 0;
    bool is_closed = false;
    bool is_anonymous = true;
    bool allow_multiple_answers = false;
    bool is_quiz = false;
    bool has_open_period = false;
    bool has_close_date = false;
    bool has_explanation = false;
    bool has_question_entities = false;
    bool has_option_entities = false;
    bool has_multiple_correct_option_ids = false;
    vector<int32> correct_option_ids;
    bool has_open_answers = false;
    bool know_revoting_disabled = false;
    bool has_revoting_disabled = false;
    bool shuffle_answers = false;
    bool hide_results_until_close = false;
    bool has_explanation_media = false;

    if (parser.version() >= static_cast<int32>(Version::SupportPolls2_0)) {
      BEGIN_PARSE_FLAGS();
      PARSE_FLAG(is_closed);
      PARSE_FLAG(is_anonymous);
      PARSE_FLAG(allow_multiple_answers);
      PARSE_FLAG(is_quiz);
      PARSE_FLAG(has_open_period);
      PARSE_FLAG(has_close_date);
      PARSE_FLAG(has_explanation);
      PARSE_FLAG(has_question_entities);
      PARSE_FLAG(has_option_entities);
      PARSE_FLAG(has_multiple_correct_option_ids);
      PARSE_FLAG(has_open_answers);
      PARSE_FLAG(know_revoting_disabled);
      PARSE_FLAG(has_revoting_disabled);
      PARSE_FLAG(shuffle_answers);
      PARSE_FLAG(hide_results_until_close);
      PARSE_FLAG(has_explanation_media);
      END_PARSE_FLAGS();
    }
    parse(question.text, parser);
    vector<string> option_texts;
    parse(option_texts, parser);
    if (is_quiz) {
      if (has_multiple_correct_option_ids) {
        parse(correct_option_ids, parser);
      } else {
        int32 correct_option_id;
        parse(correct_option_id, parser);
        if (correct_option_id != -1) {
          correct_option_ids.push_back(correct_option_id);
        }
      }
      auto status = check_quiz_correct_option_ids(correct_option_ids, option_texts.size(), false);
      if (status.is_error()) {
        parser.set_error(status.message().str());
        return PollId();
      }
    }
    if (has_open_period) {
      parse(open_period, parser);
    }
    if (has_close_date) {
      parse(close_date, parser);
    }
    if (has_explanation) {
      parse(explanation, parser);
    }
    if (has_question_entities) {
      parse(question.entities, parser);
    }
    vector<vector<MessageEntity>> option_entities;
    if (has_option_entities) {
      parse(option_entities, parser);
      CHECK(option_entities.size() == option_texts.size());
    } else {
      option_entities.resize(option_texts.size());
    }
    vector<FormattedText> options;
    for (size_t i = 0; i < option_texts.size(); i++) {
      options.push_back({std::move(option_texts[i]), std::move(option_entities[i])});
    }
    if (!know_revoting_disabled) {
      has_revoting_disabled = is_quiz;
    }
    if (has_explanation_media) {
      parse_message_content(explanation_media, parser);
    }

    if (parser.get_error() != nullptr) {
      return PollId();
    }
    return create_poll(std::move(question), std::move(options), is_anonymous, allow_multiple_answers, has_open_answers,
                       has_revoting_disabled, shuffle_answers, hide_results_until_close, is_quiz,
                       std::move(correct_option_ids), std::move(explanation), std::move(explanation_media), open_period,
                       close_date, is_closed);
  }

  auto poll = get_poll_force(poll_id);
  if (poll == nullptr) {
    return PollId();
  }
  return poll_id;
}

}  // namespace td
