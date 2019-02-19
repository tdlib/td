//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PollManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/PollManager.hpp"
#include "td/telegram/TdDb.h"
#include "td/telegram/Td.h"

#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Status.h"

namespace td {

PollManager::PollManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void PollManager::tear_down() {
  parent_.reset();
}

PollManager::~PollManager() = default;

bool PollManager::is_local_poll_id(PollId poll_id) {
  return poll_id.get() < 0 && poll_id.get() > std::numeric_limits<int32>::min();
}

const PollManager::Poll *PollManager::get_poll(PollId poll_id) const {
  auto p = polls_.find(poll_id);
  if (p == polls_.end()) {
    return nullptr;
  } else {
    return p->second.get();
  }
}

PollManager::Poll *PollManager::get_poll_editable(PollId poll_id) {
  auto p = polls_.find(poll_id);
  if (p == polls_.end()) {
    return nullptr;
  } else {
    return p->second.get();
  }
}

bool PollManager::have_poll(PollId poll_id) const {
  return get_poll(poll_id) != nullptr;
}

string PollManager::get_poll_database_key(PollId poll_id) {
  return PSTRING() << "poll" << poll_id.get();
}

void PollManager::save_poll(const Poll *poll, PollId poll_id) {
  if (!G()->parameters().use_message_db) {
    return;
  }
  CHECK(!is_local_poll_id(poll_id));

  LOG(INFO) << "Save " << poll_id << " to database";
  CHECK(poll != nullptr);
  // G()->td_db()->get_sqlite_pmc()->set(get_poll_database_key(poll_id), log_event_store(*poll).as_slice().str(), Auto());
}

void PollManager::on_load_poll_from_database(PollId poll_id, string value) {
  loaded_from_database_polls_.insert(poll_id);

  LOG(INFO) << "Successfully loaded " << poll_id << " of size " << value.size() << " from database";
  //  G()->td_db()->get_sqlite_pmc()->erase(get_poll_database_key(poll_id), Auto());
  //  return;

  CHECK(!have_poll(poll_id));
  if (!value.empty()) {
    auto result = make_unique<Poll>();
    auto status = log_event_parse(*result, value);
    if (status.is_error()) {
      LOG(FATAL) << status << ": " << format::as_hex_dump<4>(Slice(value));
    }
    polls_[poll_id] = std::move(result);
  }
}

bool PollManager::have_poll_force(PollId poll_id) {
  return get_poll_force(poll_id) != nullptr;
}

PollManager::Poll *PollManager::get_poll_force(PollId poll_id) {
  auto poll = get_poll_editable(poll_id);
  if (poll != nullptr) {
    return poll;
  }
  if (!G()->parameters().use_message_db) {
    return nullptr;
  }
  if (loaded_from_database_polls_.count(poll_id)) {
    return nullptr;
  }

  LOG(INFO) << "Trying to load " << poll_id << " from database";
  on_load_poll_from_database(poll_id, G()->td_db()->get_sqlite_sync_pmc()->get(get_poll_database_key(poll_id)));
  return get_poll_editable(poll_id);
}

td_api::object_ptr<td_api::pollAnswer> PollManager::get_poll_answer_object(const PollAnswer &poll_answer) {
  return td_api::make_object<td_api::pollAnswer>(poll_answer.text, poll_answer.voter_count, poll_answer.is_chosen);
}

td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id) const {
  auto poll = get_poll(poll_id);
  CHECK(poll != nullptr);
  return td_api::make_object<td_api::poll>(poll->question, transform(poll->answers, get_poll_answer_object),
                                           poll->total_voter_count, poll->is_closed);
}

telegram_api::object_ptr<telegram_api::pollAnswer> PollManager::get_input_poll_answer(const PollAnswer &poll_answer) {
  return telegram_api::make_object<telegram_api::pollAnswer>(poll_answer.text, BufferSlice(poll_answer.data));
}

PollId PollManager::create_poll(string &&question, vector<string> &&answers) {
  auto poll = make_unique<Poll>();
  poll->question = std::move(question);
  int pos = 0;
  for (auto &answer_text : answers) {
    PollAnswer answer;
    answer.text = std::move(answer_text);
    answer.data = to_string(pos++);
    poll->answers.push_back(std::move(answer));
  }

  PollId poll_id(--current_local_poll_id_);
  CHECK(is_local_poll_id(poll_id));
  bool is_inserted = polls_.emplace(poll_id, std::move(poll)).second;
  CHECK(is_inserted);
  return poll_id;
}

tl_object_ptr<telegram_api::InputMedia> PollManager::get_input_media(PollId poll_id) const {
  auto poll = get_poll(poll_id);
  CHECK(poll != nullptr);
  return telegram_api::make_object<telegram_api::inputMediaPoll>(telegram_api::make_object<telegram_api::poll>(
      0, 0, false /* ignored */, poll->question, transform(poll->answers, get_input_poll_answer)));
}

vector<PollManager::PollAnswer> PollManager::get_poll_answers(
    vector<tl_object_ptr<telegram_api::pollAnswer>> &&poll_answers) {
  return transform(std::move(poll_answers), [](tl_object_ptr<telegram_api::pollAnswer> &&poll_answer) {
    PollAnswer answer;
    answer.text = std::move(poll_answer->text_);
    answer.data = poll_answer->option_.as_slice().str();
    return answer;
  });
}

PollId PollManager::on_get_poll(PollId poll_id, tl_object_ptr<telegram_api::poll> &&poll_server,
                                tl_object_ptr<telegram_api::pollResults> &&poll_results) {
  if (!poll_id.is_valid() && poll_server != nullptr) {
    poll_id = PollId(poll_server->id_);
  }
  if (!poll_id.is_valid() || is_local_poll_id(poll_id)) {
    LOG(ERROR) << "Receive " << poll_id << " from server";
    return PollId();
  }
  if (poll_server != nullptr && poll_server->id_ != poll_id.get()) {
    LOG(ERROR) << "Receive poll " << poll_server->id_ << " instead of " << poll_id;
    return PollId();
  }

  auto poll = get_poll_force(poll_id);
  bool is_changed = false;
  if (poll == nullptr) {
    if (poll_server == nullptr) {
      LOG(INFO) << "Ignore " << poll_id << ", because have no data about it";
      return PollId();
    }

    auto p = make_unique<Poll>();
    poll = p.get();
    bool is_inserted = polls_.emplace(poll_id, std::move(p)).second;
    CHECK(is_inserted);
  }
  CHECK(poll != nullptr);

  if (poll_server != nullptr) {
    if (poll->question != poll_server->question_) {
      poll->question = std::move(poll_server->question_);
      is_changed = true;
    }
    if (poll->answers.size() != poll_server->answers_.size()) {
      poll->answers = get_poll_answers(std::move(poll_server->answers_));
      is_changed = true;
    } else {
      for (size_t i = 0; i < poll->answers.size(); i++) {
        if (poll->answers[i].text != poll_server->answers_[i]->text_) {
          poll->answers[i].text = std::move(poll_server->answers_[i]->text_);
          is_changed = true;
        }
        if (poll->answers[i].data != poll_server->answers_[i]->option_.as_slice()) {
          poll->answers[i].data = poll_server->answers_[i]->option_.as_slice().str();
          poll->answers[i].voter_count = 0;
          poll->answers[i].is_chosen = false;
          is_changed = true;
        }
      }
    }
    bool is_closed = (poll_server->flags_ & telegram_api::poll::CLOSED_MASK) != 0;
    if (is_closed != poll->is_closed) {
      poll->is_closed = is_closed;
      is_changed = true;
    }
  }

  CHECK(poll_results != nullptr);
  bool is_min = (poll_results->flags_ & telegram_api::pollResults::MIN_MASK) != 0;
  if ((poll_results->flags_ & telegram_api::pollResults::TOTAL_VOTERS_MASK) != 0 &&
      poll_results->total_voters_ != poll->total_voter_count) {
    poll->total_voter_count = poll_results->total_voters_;
    is_changed = true;
  }
  for (auto &poll_result : poll_results->results_) {
    Slice data = poll_result->option_.as_slice();
    for (auto &answer : poll->answers) {
      if (answer.data != data) {
        continue;
      }
      if (!is_min) {
        bool is_chosen = (poll_result->flags_ & telegram_api::pollAnswerVoters::CHOSEN_MASK) != 0;
        if (is_chosen != answer.is_chosen) {
          answer.is_chosen = is_chosen;
          is_changed = true;
        }
      }
      if (poll_result->voters_ != answer.voter_count) {
        answer.voter_count = poll_result->voters_;
        is_changed = true;
      }
    }
  }

  if (is_changed) {
    save_poll(poll, poll_id);
  }
  return poll_id;
}

}  // namespace td
