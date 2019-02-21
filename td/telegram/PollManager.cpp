//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PollManager.h"

#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/MessagesManager.h"
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

void PollManager::notify_on_poll_update(PollId poll_id) {
  auto it = poll_messages_.find(poll_id);
  if (it == poll_messages_.end()) {
    return;
  }

  for (auto full_message_id : it->second) {
    td_->messages_manager_->on_update_message_content(full_message_id);
  }
}

string PollManager::get_poll_database_key(PollId poll_id) {
  return PSTRING() << "poll" << poll_id.get();
}

void PollManager::save_poll(const Poll *poll, PollId poll_id) {
  CHECK(!is_local_poll_id(poll_id));

  if (!G()->parameters().use_message_db) {
    return;
  }

  LOG(INFO) << "Save " << poll_id << " to database";
  CHECK(poll != nullptr);
  G()->td_db()->get_sqlite_pmc()->set(get_poll_database_key(poll_id), log_event_store(*poll).as_slice().str(), Auto());
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

td_api::object_ptr<td_api::pollOption> PollManager::get_poll_option_object(const PollOption &poll_option) {
  return td_api::make_object<td_api::pollOption>(poll_option.text, poll_option.voter_count, poll_option.is_chosen);
}

td_api::object_ptr<td_api::poll> PollManager::get_poll_object(PollId poll_id) const {
  auto poll = get_poll(poll_id);
  CHECK(poll != nullptr);
  return td_api::make_object<td_api::poll>(poll->question, transform(poll->options, get_poll_option_object),
                                           poll->total_voter_count, poll->is_closed);
}

telegram_api::object_ptr<telegram_api::pollAnswer> PollManager::get_input_poll_option(const PollOption &poll_option) {
  return telegram_api::make_object<telegram_api::pollAnswer>(poll_option.text, BufferSlice(poll_option.data));
}

PollId PollManager::create_poll(string &&question, vector<string> &&options) {
  auto poll = make_unique<Poll>();
  poll->question = std::move(question);
  int pos = 0;
  for (auto &option_text : options) {
    PollOption option;
    option.text = std::move(option_text);
    option.data = to_string(pos++);
    poll->options.push_back(std::move(option));
  }

  PollId poll_id(--current_local_poll_id_);
  CHECK(is_local_poll_id(poll_id));
  bool is_inserted = polls_.emplace(poll_id, std::move(poll)).second;
  CHECK(is_inserted);
  return poll_id;
}

void PollManager::register_poll(PollId poll_id, FullMessageId full_message_id) {
  CHECK(have_poll(poll_id));
  poll_messages_[poll_id].insert(full_message_id);
}

void PollManager::unregister_poll(PollId poll_id, FullMessageId full_message_id) {
  CHECK(have_poll(poll_id));
  poll_messages_[poll_id].erase(full_message_id);
}

void PollManager::close_poll(PollId poll_id) {
  auto poll = get_poll_editable(poll_id);
  CHECK(poll != nullptr);
  if (poll->is_closed) {
    return;
  }

  poll->is_closed = true;
  notify_on_poll_update(poll_id);
  if (!is_local_poll_id(poll_id)) {
    // TODO send poll close request to the server + LogEvent
    save_poll(poll, poll_id);
  }
}

tl_object_ptr<telegram_api::InputMedia> PollManager::get_input_media(PollId poll_id) const {
  auto poll = get_poll(poll_id);
  CHECK(poll != nullptr);
  return telegram_api::make_object<telegram_api::inputMediaPoll>(telegram_api::make_object<telegram_api::poll>(
      0, 0, false /* ignored */, poll->question, transform(poll->options, get_input_poll_option)));
}

vector<PollManager::PollOption> PollManager::get_poll_options(
    vector<tl_object_ptr<telegram_api::pollAnswer>> &&poll_options) {
  return transform(std::move(poll_options), [](tl_object_ptr<telegram_api::pollAnswer> &&poll_option) {
    PollOption option;
    option.text = std::move(poll_option->text_);
    option.data = poll_option->option_.as_slice().str();
    return option;
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
    if (poll->options.size() != poll_server->answers_.size()) {
      poll->options = get_poll_options(std::move(poll_server->answers_));
      is_changed = true;
    } else {
      for (size_t i = 0; i < poll->options.size(); i++) {
        if (poll->options[i].text != poll_server->answers_[i]->text_) {
          poll->options[i].text = std::move(poll_server->answers_[i]->text_);
          is_changed = true;
        }
        if (poll->options[i].data != poll_server->answers_[i]->option_.as_slice()) {
          poll->options[i].data = poll_server->answers_[i]->option_.as_slice().str();
          poll->options[i].voter_count = 0;
          poll->options[i].is_chosen = false;
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
    for (auto &option : poll->options) {
      if (option.data != data) {
        continue;
      }
      if (!is_min) {
        bool is_chosen = (poll_result->flags_ & telegram_api::pollAnswerVoters::CHOSEN_MASK) != 0;
        if (is_chosen != option.is_chosen) {
          option.is_chosen = is_chosen;
          is_changed = true;
        }
      }
      if (poll_result->voters_ != option.voter_count) {
        option.voter_count = poll_result->voters_;
        is_changed = true;
      }
    }
  }

  if (is_changed) {
    notify_on_poll_update(poll_id);
    save_poll(poll, poll_id);
  }
  return poll_id;
}

}  // namespace td
