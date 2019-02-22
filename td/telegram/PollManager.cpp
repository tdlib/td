//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2019
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/PollManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Global.h"
#include "td/telegram/logevent/LogEvent.h"
#include "td/telegram/logevent/LogEventHelper.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/net/NetActor.h"
#include "td/telegram/PollId.hpp"
#include "td/telegram/PollManager.hpp"
#include "td/telegram/SequenceDispatcher.h"
#include "td/telegram/StateManager.h"
#include "td/telegram/TdDb.h"
#include "td/telegram/Td.h"
#include "td/telegram/TdParameters.h"
#include "td/telegram/UpdatesManager.h"

#include "td/db/binlog/BinlogEvent.h"
#include "td/db/binlog/BinlogHelper.h"
#include "td/db/SqliteKeyValue.h"
#include "td/db/SqliteKeyValueAsync.h"

#include "td/utils/buffer.h"
#include "td/utils/format.h"
#include "td/utils/logging.h"
#include "td/utils/misc.h"
#include "td/utils/Random.h"
#include "td/utils/Status.h"

#include <algorithm>

namespace td {

class GetPollResultsQuery : public Td::ResultHandler {
  PollId poll_id_;
  DialogId dialog_id_;

 public:
  void send(PollId poll_id, FullMessageId full_message_id) {
    poll_id_ = poll_id;
    dialog_id_ = full_message_id.get_dialog_id();
    auto input_peer = td->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      LOG(INFO) << "Can't reget poll, because have no read access to " << dialog_id_;
      // do not signal error to PollManager
      return;
    }

    auto message_id = full_message_id.get_message_id().get_server_message_id().get();
    send_query(G()->net_query_creator().create(
        create_storer(telegram_api::messages_getPollResults(std::move(input_peer), message_id))));
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_getPollResults>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive poll results: " << to_string(result);

    td->updates_manager_->on_get_updates(std::move(result));
  }

  void on_error(uint64 id, Status status) override {
    if (!td->messages_manager_->on_get_dialog_error(dialog_id_, status, "GetPollResultsQuery")) {
      LOG(ERROR) << "Receive " << status << ", while trying to get results of " << poll_id_;
      td->poll_manager_->on_get_poll_results_failed(poll_id_);
    }
  }
};

class SetPollAnswerActor : public NetActorOnce {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit SetPollAnswerActor(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FullMessageId full_message_id, vector<BufferSlice> &&options, uint64 generation, NetQueryRef *query_ref) {
    dialog_id_ = full_message_id.get_dialog_id();
    auto input_peer = td->messages_manager_->get_input_peer(dialog_id_, AccessRights::Read);
    if (input_peer == nullptr) {
      LOG(INFO) << "Can't set poll answer, because have no read access to " << dialog_id_;
      return on_error(0, Status::Error(400, "Can't access the chat"));
    }

    auto message_id = full_message_id.get_message_id().get_server_message_id().get();
    auto query = G()->net_query_creator().create(
        create_storer(telegram_api::messages_sendVote(std::move(input_peer), message_id, std::move(options))));
    *query_ref = query.get_weak();
    auto sequence_id = -1;
    send_closure(td->messages_manager_->sequence_dispatcher_, &MultiSequenceDispatcher::send_with_callback,
                 std::move(query), actor_shared(this), sequence_id);
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_sendVote>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive sendVote result: " << to_string(result);

    td->updates_manager_->on_get_updates(std::move(result));
    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    td->messages_manager_->on_get_dialog_error(dialog_id_, status, "SetPollAnswerActor");
    promise_.set_error(std::move(status));
  }
};

class StopPollActor : public NetActorOnce {
  Promise<Unit> promise_;
  DialogId dialog_id_;

 public:
  explicit StopPollActor(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(FullMessageId full_message_id) {
    dialog_id_ = full_message_id.get_dialog_id();
    auto input_peer = td->messages_manager_->get_input_peer(dialog_id_, AccessRights::Edit);
    if (input_peer == nullptr) {
      LOG(INFO) << "Can't close poll, because have no edit access to " << dialog_id_;
      return on_error(0, Status::Error(400, "Can't access the chat"));
    }

    auto message_id = full_message_id.get_message_id().get_server_message_id().get();
    auto poll = telegram_api::make_object<telegram_api::poll>();
    poll->flags_ |= telegram_api::poll::CLOSED_MASK;
    auto input_media = telegram_api::make_object<telegram_api::inputMediaPoll>(std::move(poll));
    auto query = G()->net_query_creator().create(create_storer(telegram_api::messages_editMessage(
        telegram_api::messages_editMessage::MEDIA_MASK, false /*ignored*/, std::move(input_peer), message_id, string(),
        std::move(input_media), nullptr, vector<tl_object_ptr<telegram_api::MessageEntity>>())));
    auto sequence_id = -1;
    send_closure(td->messages_manager_->sequence_dispatcher_, &MultiSequenceDispatcher::send_with_callback,
                 std::move(query), actor_shared(this), sequence_id);
  }

  void on_result(uint64 id, BufferSlice packet) override {
    auto result_ptr = fetch_result<telegram_api::messages_editMessage>(packet);
    if (result_ptr.is_error()) {
      return on_error(id, result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for stopPoll: " << to_string(result);
    td->updates_manager_->on_get_updates(std::move(result));

    promise_.set_value(Unit());
  }

  void on_error(uint64 id, Status status) override {
    if (!td->auth_manager_->is_bot() && status.message() == "MESSAGE_NOT_MODIFIED") {
      return promise_.set_value(Unit());
    }
    td->messages_manager_->on_get_dialog_error(dialog_id_, status, "StopPollActor");
    promise_.set_error(std::move(status));
  }
};

PollManager::PollManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
  update_poll_timeout_.set_callback(on_update_poll_timeout_callback);
  update_poll_timeout_.set_callback_data(static_cast<void *>(this));
}

void PollManager::start_up() {
  class StateCallback : public StateManager::Callback {
   public:
    explicit StateCallback(ActorId<PollManager> parent) : parent_(std::move(parent)) {
    }
    bool on_online(bool is_online) override {
      if (is_online) {
        send_closure(parent_, &PollManager::on_online);
      }
      return parent_.is_alive();
    }

   private:
    ActorId<PollManager> parent_;
  };
  send_closure(G()->state_manager(), &StateManager::add_callback, make_unique<StateCallback>(actor_id(this)));
}

void PollManager::tear_down() {
  parent_.reset();
}

PollManager::~PollManager() = default;

void PollManager::on_update_poll_timeout_callback(void *poll_manager_ptr, int64 poll_id_int) {
  if (G()->close_flag()) {
    return;
  }

  auto poll_manager = static_cast<PollManager *>(poll_manager_ptr);
  send_closure_later(poll_manager->actor_id(poll_manager), &PollManager::on_update_poll_timeout, PollId(poll_id_int));
}

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
  vector<td_api::object_ptr<td_api::pollOption>> poll_options;
  auto it = pending_answers_.find(poll_id);
  int32 voter_count_diff = 0;
  if (it == pending_answers_.end()) {
    poll_options = transform(poll->options, get_poll_option_object);
  } else {
    auto &chosen_options = it->second.options_;
    for (auto &poll_option : poll->options) {
      auto is_chosen =
          std::find(chosen_options.begin(), chosen_options.end(), poll_option.data) != chosen_options.end();
      if (poll_option.is_chosen) {
        voter_count_diff = -1;
      }
      poll_options.push_back(td_api::make_object<td_api::pollOption>(
          poll_option.text,
          poll_option.voter_count - static_cast<int32>(poll_option.is_chosen) + static_cast<int32>(is_chosen),
          is_chosen));
    }
    if (!chosen_options.empty()) {
      voter_count_diff++;
    }
  }
  return td_api::make_object<td_api::poll>(poll->question, std::move(poll_options),
                                           poll->total_voter_count + voter_count_diff, poll->is_closed);
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
    option.data = string(1, narrow_cast<char>(pos++));
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
  if (!td_->auth_manager_->is_bot() && !is_local_poll_id(poll_id) && !get_poll_is_closed(poll_id)) {
    update_poll_timeout_.add_timeout_in(poll_id.get(), 0);
  }
}

void PollManager::unregister_poll(PollId poll_id, FullMessageId full_message_id) {
  CHECK(have_poll(poll_id));
  auto &message_ids = poll_messages_[poll_id];
  message_ids.erase(full_message_id);
  if (message_ids.empty()) {
    poll_messages_.erase(poll_id);
    update_poll_timeout_.cancel_timeout(poll_id.get());
  }
}

bool PollManager::get_poll_is_closed(PollId poll_id) const {
  auto poll = get_poll(poll_id);
  CHECK(poll != nullptr);
  return poll->is_closed;
}

void PollManager::set_poll_answer(PollId poll_id, FullMessageId full_message_id, vector<int32> &&option_ids,
                                  Promise<Unit> &&promise) {
  if (option_ids.size() > 1) {
    return promise.set_error(Status::Error(400, "Can't choose more than 1 option"));
  }
  if (is_local_poll_id(poll_id)) {
    return promise.set_error(Status::Error(5, "Poll can't be answered"));
  }

  auto poll = get_poll(poll_id);
  CHECK(poll != nullptr);
  if (poll->is_closed) {
    return promise.set_error(Status::Error(400, "Can't answer closed poll"));
  }
  vector<string> options;
  for (auto &option_id : option_ids) {
    auto index = static_cast<size_t>(option_id);
    if (index >= poll->options.size()) {
      return promise.set_error(Status::Error(400, "Invalid option id specified"));
    }
    options.push_back(poll->options[index].data);
  }

  do_set_poll_answer(poll_id, full_message_id, std::move(options), 0, std::move(promise));
}

class PollManager::SetPollAnswerLogEvent {
 public:
  PollId poll_id_;
  FullMessageId full_message_id_;
  vector<string> options_;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(poll_id_, storer);
    td::store(full_message_id_, storer);
    td::store(options_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(poll_id_, parser);
    td::parse(full_message_id_, parser);
    td::parse(options_, parser);
  }
};

void PollManager::do_set_poll_answer(PollId poll_id, FullMessageId full_message_id, vector<string> &&options,
                                     uint64 logevent_id, Promise<Unit> &&promise) {
  auto &pending_answer = pending_answers_[poll_id];
  if (!pending_answer.promises_.empty() && pending_answer.options_ == options) {
    pending_answer.promises_.push_back(std::move(promise));
    return;
  }

  CHECK(pending_answer.logevent_id_ == 0 || logevent_id == 0);
  if (logevent_id == 0 && G()->parameters().use_message_db) {
    SetPollAnswerLogEvent logevent;
    logevent.poll_id_ = poll_id;
    logevent.full_message_id_ = full_message_id;
    logevent.options_ = options;
    auto storer = LogEventStorerImpl<SetPollAnswerLogEvent>(logevent);
    if (pending_answer.generation_ == 0) {
      CHECK(pending_answer.logevent_id_ == 0);
      logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::SetPollAnswer, storer);
      LOG(INFO) << "Add set poll answer logevent " << logevent_id;
    } else {
      CHECK(pending_answer.logevent_id_ != 0);
      logevent_id = pending_answer.logevent_id_;
      auto new_logevent_id = binlog_rewrite(G()->td_db()->get_binlog(), pending_answer.logevent_id_,
                                            LogEvent::HandlerType::SetPollAnswer, storer);
      LOG(INFO) << "Rewrite set poll answer logevent " << logevent_id << " with " << new_logevent_id;
    }
  }

  if (!pending_answer.promises_.empty()) {
    CHECK(!pending_answer.query_ref_.empty());
    cancel_query(pending_answer.query_ref_);
    pending_answer.query_ref_ = NetQueryRef();

    auto promises = std::move(pending_answer.promises_);
    pending_answer.promises_.clear();
    for (auto &old_promise : promises) {
      old_promise.set_value(Unit());
    }
  }

  vector<BufferSlice> sent_options;
  for (auto &option : options) {
    sent_options.emplace_back(option);
  }

  auto generation = ++current_generation_;

  pending_answer.options_ = std::move(options);
  pending_answer.promises_.push_back(std::move(promise));
  pending_answer.generation_ = generation;
  pending_answer.logevent_id_ = logevent_id;

  notify_on_poll_update(poll_id);

  auto query_promise = PromiseCreator::lambda([poll_id, generation, actor_id = actor_id(this)](Result<Unit> &&result) {
    send_closure(actor_id, &PollManager::on_set_poll_answer, poll_id, generation, std::move(result));
  });

  send_closure(td_->create_net_actor<SetPollAnswerActor>(std::move(query_promise)), &SetPollAnswerActor::send,
               full_message_id, std::move(sent_options), generation, &pending_answer.query_ref_);
}

void PollManager::on_set_poll_answer(PollId poll_id, uint64 generation, Result<Unit> &&result) {
  if (G()->close_flag() && result.is_error()) {
    // request will be resent after restart
    return;
  }
  auto it = pending_answers_.find(poll_id);
  if (it == pending_answers_.end()) {
    // can happen if this is an answer with mismatched generation and server has ignored invoke-after
    return;
  }

  auto &pending_answer = it->second;
  CHECK(!pending_answer.promises_.empty());
  if (pending_answer.generation_ != generation) {
    return;
  }

  if (pending_answer.logevent_id_ != 0) {
    LOG(INFO) << "Delete set poll answer logevent " << pending_answer.logevent_id_;
    binlog_erase(G()->td_db()->get_binlog(), pending_answer.logevent_id_);
  }

  auto promises = std::move(pending_answer.promises_);
  for (auto &promise : promises) {
    if (result.is_ok()) {
      promise.set_value(Unit());
    } else {
      promise.set_error(result.error().clone());
    }
  }

  pending_answers_.erase(it);
}

void PollManager::stop_poll(PollId poll_id, FullMessageId full_message_id, Promise<Unit> &&promise) {
  if (is_local_poll_id(poll_id)) {
    LOG(ERROR) << "Receive local " << poll_id << " from " << full_message_id << " in stop_poll";
    stop_local_poll(poll_id);
    promise.set_value(Unit());
    return;
  }
  auto poll = get_poll_editable(poll_id);
  CHECK(poll != nullptr);
  if (poll->is_closed) {
    promise.set_value(Unit());
    return;
  }

  poll->is_closed = true;
  notify_on_poll_update(poll_id);
  save_poll(poll, poll_id);

  do_stop_poll(poll_id, full_message_id, 0, std::move(promise));
}

class PollManager::StopPollLogEvent {
 public:
  PollId poll_id_;
  FullMessageId full_message_id_;

  template <class StorerT>
  void store(StorerT &storer) const {
    td::store(poll_id_, storer);
    td::store(full_message_id_, storer);
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    td::parse(poll_id_, parser);
    td::parse(full_message_id_, parser);
  }
};

void PollManager::do_stop_poll(PollId poll_id, FullMessageId full_message_id, uint64 logevent_id,
                               Promise<Unit> &&promise) {
  if (logevent_id == 0 && G()->parameters().use_message_db) {
    StopPollLogEvent logevent{poll_id, full_message_id};
    auto storer = LogEventStorerImpl<StopPollLogEvent>(logevent);
    logevent_id = binlog_add(G()->td_db()->get_binlog(), LogEvent::HandlerType::StopPoll, storer);
  }

  auto new_promise = get_erase_logevent_promise(logevent_id, std::move(promise));

  send_closure(td_->create_net_actor<StopPollActor>(std::move(new_promise)), &StopPollActor::send, full_message_id);
}

void PollManager::stop_local_poll(PollId poll_id) {
  CHECK(is_local_poll_id(poll_id));
  auto poll = get_poll_editable(poll_id);
  CHECK(poll != nullptr);
  if (poll->is_closed) {
    return;
  }

  poll->is_closed = true;
  notify_on_poll_update(poll_id);
}

double PollManager::get_polling_timeout() const {
  return td_->is_online() ? 60 : 30 * 60;
}

void PollManager::on_update_poll_timeout(PollId poll_id) {
  CHECK(!td_->auth_manager_->is_bot());
  CHECK(!is_local_poll_id(poll_id));

  if (get_poll_is_closed(poll_id)) {
    return;
  }

  auto it = poll_messages_.find(poll_id);
  if (it == poll_messages_.end()) {
    return;
  }

  auto full_message_id = *it->second.begin();
  LOG(INFO) << "Fetching results of " << poll_id << " from " << full_message_id;
  td_->create_handler<GetPollResultsQuery>()->send(poll_id, full_message_id);
}

void PollManager::on_get_poll_results_failed(PollId poll_id) {
  if (!get_poll_is_closed(poll_id)) {
    auto timeout = get_polling_timeout();
    LOG(INFO) << "Schedule updating of " << poll_id << " in " << timeout;
    update_poll_timeout_.add_timeout_in(poll_id.get(), timeout);
  }
}

void PollManager::on_online() {
  if (td_->auth_manager_->is_bot()) {
    return;
  }

  for (auto &it : poll_messages_) {
    auto poll_id = it.first;
    if (update_poll_timeout_.has_timeout(poll_id.get())) {
      auto timeout = Random::fast(3, 30);
      LOG(INFO) << "Schedule updating of " << poll_id << " in " << timeout;
      update_poll_timeout_.set_timeout_in(poll_id.get(), timeout);
    }
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

  if (!td_->auth_manager_->is_bot() && !poll->is_closed) {
    auto timeout = get_polling_timeout();
    LOG(INFO) << "Schedule updating of " << poll_id << " in " << timeout;
    update_poll_timeout_.set_timeout_in(poll_id.get(), timeout);
  }
  if (is_changed) {
    notify_on_poll_update(poll_id);
    save_poll(poll, poll_id);
  }
  return poll_id;
}

void PollManager::on_binlog_events(vector<BinlogEvent> &&events) {
  for (auto &event : events) {
    switch (event.type_) {
      case LogEvent::HandlerType::SetPollAnswer: {
        if (!G()->parameters().use_message_db) {
          binlog_erase(G()->td_db()->get_binlog(), event.id_);
          break;
        }

        SetPollAnswerLogEvent log_event;
        log_event_parse(log_event, event.data_).ensure();

        auto dialog_id = log_event.full_message_id_.get_dialog_id();

        Dependencies dependencies;
        td_->messages_manager_->add_dialog_dependencies(dependencies, dialog_id);
        td_->messages_manager_->resolve_dependencies_force(dependencies);

        do_set_poll_answer(log_event.poll_id_, log_event.full_message_id_, std::move(log_event.options_), event.id_,
                           Auto());
        break;
      }
      case LogEvent::HandlerType::StopPoll: {
        if (!G()->parameters().use_message_db) {
          binlog_erase(G()->td_db()->get_binlog(), event.id_);
          break;
        }

        StopPollLogEvent log_event;
        log_event_parse(log_event, event.data_).ensure();

        auto dialog_id = log_event.full_message_id_.get_dialog_id();

        Dependencies dependencies;
        td_->messages_manager_->add_dialog_dependencies(dependencies, dialog_id);
        td_->messages_manager_->resolve_dependencies_force(dependencies);

        do_stop_poll(log_event.poll_id_, log_event.full_message_id_, event.id_, Auto());
        break;
      }
      default:
        LOG(FATAL) << "Unsupported logevent type " << event.type_;
    }
  }
}

}  // namespace td
