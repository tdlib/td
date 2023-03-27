//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogFilterManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/ContactsManager.h"
#include "td/telegram/DialogFilter.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"

namespace td {

class GetDialogsQuery final : public Td::ResultHandler {
  Promise<Unit> promise_;
  bool is_single_ = false;

 public:
  explicit GetDialogsQuery(Promise<Unit> &&promise) : promise_(std::move(promise)) {
  }

  void send(vector<InputDialogId> input_dialog_ids) {
    CHECK(!input_dialog_ids.empty());
    CHECK(input_dialog_ids.size() <= 100);
    is_single_ = input_dialog_ids.size() == 1;
    auto input_dialog_peers = InputDialogId::get_input_dialog_peers(input_dialog_ids);
    CHECK(input_dialog_peers.size() == input_dialog_ids.size());
    send_query(G()->net_query_creator().create(telegram_api::messages_getPeerDialogs(std::move(input_dialog_peers))));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getPeerDialogs>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    auto result = result_ptr.move_as_ok();
    LOG(INFO) << "Receive result for GetDialogsQuery: " << to_string(result);

    td_->contacts_manager_->on_get_users(std::move(result->users_), "GetDialogsQuery");
    td_->contacts_manager_->on_get_chats(std::move(result->chats_), "GetDialogsQuery");
    td_->messages_manager_->on_get_dialogs(FolderId(), std::move(result->dialogs_), -1, std::move(result->messages_),
                                           std::move(promise_));
  }

  void on_error(Status status) final {
    if (is_single_ && status.code() == 400) {
      return promise_.set_value(Unit());
    }
    promise_.set_error(std::move(status));
  }
};

class GetSuggestedDialogFiltersQuery final : public Td::ResultHandler {
  Promise<vector<tl_object_ptr<telegram_api::dialogFilterSuggested>>> promise_;

 public:
  explicit GetSuggestedDialogFiltersQuery(Promise<vector<tl_object_ptr<telegram_api::dialogFilterSuggested>>> &&promise)
      : promise_(std::move(promise)) {
  }

  void send() {
    send_query(G()->net_query_creator().create(telegram_api::messages_getSuggestedDialogFilters()));
  }

  void on_result(BufferSlice packet) final {
    auto result_ptr = fetch_result<telegram_api::messages_getSuggestedDialogFilters>(packet);
    if (result_ptr.is_error()) {
      return on_error(result_ptr.move_as_error());
    }

    promise_.set_value(result_ptr.move_as_ok());
  }

  void on_error(Status status) final {
    promise_.set_error(std::move(status));
  }
};

DialogFilterManager::DialogFilterManager(Td *td, ActorShared<> parent) : td_(td), parent_(std::move(parent)) {
}

void DialogFilterManager::tear_down() {
  parent_.reset();
}

bool DialogFilterManager::is_recommended_dialog_filter(const DialogFilter *dialog_filter) {
  for (const auto &recommended_dialog_filter : recommended_dialog_filters_) {
    if (DialogFilter::are_similar(*recommended_dialog_filter.dialog_filter, *dialog_filter)) {
      return true;
    }
  }
  return false;
}

td_api::object_ptr<td_api::chatFilter> DialogFilterManager::get_chat_filter_object(DialogFilterId dialog_filter_id) {
  CHECK(!td_->auth_manager_->is_bot());
  auto dialog_filter = td_->messages_manager_->get_dialog_filter(dialog_filter_id);
  if (dialog_filter == nullptr) {
    return nullptr;
  }

  return get_chat_filter_object(dialog_filter);
}

td_api::object_ptr<td_api::chatFilter> DialogFilterManager::get_chat_filter_object(const DialogFilter *dialog_filter) {
  DialogFilterId dialog_filter_id = dialog_filter->get_dialog_filter_id();

  vector<DialogId> left_dialog_ids;
  vector<DialogId> unknown_dialog_ids;
  dialog_filter->for_each_dialog([&](const InputDialogId &input_dialog_id) {
    auto dialog_id = input_dialog_id.get_dialog_id();
    if (td_->messages_manager_->is_dialog_in_dialog_list(dialog_id)) {
      return;
    }
    if (td_->messages_manager_->have_dialog(dialog_id)) {
      LOG(INFO) << "Skip nonjoined " << dialog_id << " from " << dialog_filter_id;
      unknown_dialog_ids.push_back(dialog_id);
      left_dialog_ids.push_back(dialog_id);
    } else {
      LOG(ERROR) << "Can't find " << dialog_id << " from " << dialog_filter_id;
      unknown_dialog_ids.push_back(dialog_id);
    }
  });

  auto result = dialog_filter->get_chat_filter_object(unknown_dialog_ids);

  if (dialog_filter_id.is_valid()) {
    delete_dialogs_from_filter(dialog_filter, std::move(left_dialog_ids), "get_chat_filter_object");
  }
  return result;
}

void DialogFilterManager::get_recommended_dialog_filters(
    Promise<td_api::object_ptr<td_api::recommendedChatFilters>> &&promise) {
  CHECK(!td_->auth_manager_->is_bot());
  auto query_promise = PromiseCreator::lambda(
      [actor_id = actor_id(this), promise = std::move(promise)](
          Result<vector<telegram_api::object_ptr<telegram_api::dialogFilterSuggested>>> result) mutable {
        send_closure(actor_id, &DialogFilterManager::on_get_recommended_dialog_filters, std::move(result),
                     std::move(promise));
      });
  td_->create_handler<GetSuggestedDialogFiltersQuery>(std::move(query_promise))->send();
}

void DialogFilterManager::on_get_recommended_dialog_filters(
    Result<vector<telegram_api::object_ptr<telegram_api::dialogFilterSuggested>>> result,
    Promise<td_api::object_ptr<td_api::recommendedChatFilters>> &&promise) {
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }
  CHECK(!td_->auth_manager_->is_bot());
  auto suggested_filters = result.move_as_ok();

  MultiPromiseActorSafe mpas{"LoadRecommendedFiltersMultiPromiseActor"};
  mpas.add_promise(Promise<Unit>());
  auto lock = mpas.get_promise();

  vector<RecommendedDialogFilter> filters;
  for (auto &suggested_filter : suggested_filters) {
    RecommendedDialogFilter recommended_dialog_filter;
    recommended_dialog_filter.dialog_filter =
        DialogFilter::get_dialog_filter(std::move(suggested_filter->filter_), false);
    CHECK(recommended_dialog_filter.dialog_filter != nullptr);
    load_dialog_filter(recommended_dialog_filter.dialog_filter.get(), false, mpas.get_promise());

    recommended_dialog_filter.description = std::move(suggested_filter->description_);
    filters.push_back(std::move(recommended_dialog_filter));
  }

  mpas.add_promise(PromiseCreator::lambda([actor_id = actor_id(this), filters = std::move(filters),
                                           promise = std::move(promise)](Result<Unit> &&result) mutable {
    send_closure(actor_id, &DialogFilterManager::on_load_recommended_dialog_filters, std::move(result),
                 std::move(filters), std::move(promise));
  }));
  lock.set_value(Unit());
}

void DialogFilterManager::on_load_recommended_dialog_filters(
    Result<Unit> &&result, vector<RecommendedDialogFilter> &&filters,
    Promise<td_api::object_ptr<td_api::recommendedChatFilters>> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());
  if (result.is_error()) {
    return promise.set_error(result.move_as_error());
  }
  CHECK(!td_->auth_manager_->is_bot());

  auto chat_filters = transform(filters, [this](const RecommendedDialogFilter &recommended_dialog_filter) {
    return td_api::make_object<td_api::recommendedChatFilter>(
        get_chat_filter_object(recommended_dialog_filter.dialog_filter.get()), recommended_dialog_filter.description);
  });
  recommended_dialog_filters_ = std::move(filters);
  promise.set_value(td_api::make_object<td_api::recommendedChatFilters>(std::move(chat_filters)));
}

void DialogFilterManager::load_dialog_filter(DialogFilterId dialog_filter_id, bool force, Promise<Unit> &&promise) {
  CHECK(!td_->auth_manager_->is_bot());
  if (!dialog_filter_id.is_valid()) {
    return promise.set_error(Status::Error(400, "Invalid chat filter identifier specified"));
  }

  auto dialog_filter = td_->messages_manager_->get_dialog_filter(dialog_filter_id);
  if (dialog_filter == nullptr) {
    return promise.set_value(Unit());
  }

  load_dialog_filter(dialog_filter, force, std::move(promise));
}

void DialogFilterManager::load_dialog_filter(const DialogFilter *dialog_filter, bool force, Promise<Unit> &&promise) {
  CHECK(!td_->auth_manager_->is_bot());
  vector<InputDialogId> needed_dialog_ids;
  dialog_filter->for_each_dialog([&](const InputDialogId &input_dialog_id) {
    if (!td_->messages_manager_->have_dialog(input_dialog_id.get_dialog_id())) {
      needed_dialog_ids.push_back(input_dialog_id);
    }
  });

  vector<InputDialogId> input_dialog_ids;
  for (const auto &input_dialog_id : needed_dialog_ids) {
    auto dialog_id = input_dialog_id.get_dialog_id();
    // TODO load dialogs asynchronously
    if (!td_->messages_manager_->have_dialog_force(dialog_id, "load_dialog_filter")) {
      if (dialog_id.get_type() == DialogType::SecretChat) {
        if (td_->messages_manager_->have_dialog_info_force(dialog_id)) {
          td_->messages_manager_->force_create_dialog(dialog_id, "load_dialog_filter");
        }
      } else {
        input_dialog_ids.push_back(input_dialog_id);
      }
    }
  }

  if (!input_dialog_ids.empty() && !force) {
    return load_dialog_filter_dialogs(dialog_filter->get_dialog_filter_id(), std::move(input_dialog_ids),
                                      std::move(promise));
  }

  promise.set_value(Unit());
}

void DialogFilterManager::load_dialog_filter_dialogs(DialogFilterId dialog_filter_id,
                                                     vector<InputDialogId> &&input_dialog_ids,
                                                     Promise<Unit> &&promise) {
  const size_t MAX_SLICE_SIZE = 100;  // server side limit
  MultiPromiseActorSafe mpas{"GetFilterDialogsOnServerMultiPromiseActor"};
  mpas.add_promise(std::move(promise));
  auto lock = mpas.get_promise();

  for (size_t i = 0; i < input_dialog_ids.size(); i += MAX_SLICE_SIZE) {
    auto end_i = i + MAX_SLICE_SIZE;
    auto end = end_i < input_dialog_ids.size() ? input_dialog_ids.begin() + end_i : input_dialog_ids.end();
    vector<InputDialogId> slice_input_dialog_ids = {input_dialog_ids.begin() + i, end};
    auto query_promise = PromiseCreator::lambda([actor_id = actor_id(this), dialog_filter_id,
                                                 dialog_ids = InputDialogId::get_dialog_ids(slice_input_dialog_ids),
                                                 promise = mpas.get_promise()](Result<Unit> &&result) mutable {
      if (result.is_error()) {
        return promise.set_error(result.move_as_error());
      }
      send_closure(actor_id, &DialogFilterManager::on_load_dialog_filter_dialogs, dialog_filter_id,
                   std::move(dialog_ids), std::move(promise));
    });
    td_->create_handler<GetDialogsQuery>(std::move(query_promise))->send(std::move(slice_input_dialog_ids));
  }

  lock.set_value(Unit());
}

void DialogFilterManager::on_load_dialog_filter_dialogs(DialogFilterId dialog_filter_id, vector<DialogId> &&dialog_ids,
                                                        Promise<Unit> &&promise) {
  TRY_STATUS_PROMISE(promise, G()->close_status());

  td::remove_if(dialog_ids, [messages_manager = td_->messages_manager_.get()](DialogId dialog_id) {
    return messages_manager->have_dialog_force(dialog_id, "on_load_dialog_filter_dialogs");
  });
  if (dialog_ids.empty()) {
    LOG(INFO) << "All chats from " << dialog_filter_id << " were loaded";
    return promise.set_value(Unit());
  }

  LOG(INFO) << "Failed to load chats " << dialog_ids << " from " << dialog_filter_id;

  auto old_dialog_filter = td_->messages_manager_->get_dialog_filter(dialog_filter_id);
  if (old_dialog_filter == nullptr) {
    return promise.set_value(Unit());
  }
  //TODO CHECK(is_update_chat_filters_sent_);

  delete_dialogs_from_filter(old_dialog_filter, std::move(dialog_ids), "on_load_dialog_filter_dialogs");

  promise.set_value(Unit());
}

void DialogFilterManager::delete_dialogs_from_filter(const DialogFilter *dialog_filter, vector<DialogId> &&dialog_ids,
                                                     const char *source) {
  if (dialog_ids.empty()) {
    return;
  }

  auto new_dialog_filter = td::make_unique<DialogFilter>(*dialog_filter);
  for (auto dialog_id : dialog_ids) {
    new_dialog_filter->remove_dialog_id(dialog_id);
  }
  if (new_dialog_filter->is_empty(false)) {
    td_->messages_manager_->delete_dialog_filter(dialog_filter->get_dialog_filter_id(), Promise<Unit>());
    return;
  }
  CHECK(new_dialog_filter->check_limits().is_ok());

  if (*new_dialog_filter != *dialog_filter) {
    LOG(INFO) << "Update " << *dialog_filter << " to " << *new_dialog_filter;
    td_->messages_manager_->do_edit_dialog_filter(std::move(new_dialog_filter), true, "delete_dialogs_from_filter");
  }
}

}  // namespace td
