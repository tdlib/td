//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogFilterManager.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/DialogFilter.h"
#include "td/telegram/Global.h"
#include "td/telegram/MessagesManager.h"
#include "td/telegram/Td.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/algorithm.h"
#include "td/utils/buffer.h"

namespace td {

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
    td_->messages_manager_->load_dialog_filter(recommended_dialog_filter.dialog_filter.get(), false,
                                               mpas.get_promise());

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
        td_->messages_manager_->get_chat_filter_object(recommended_dialog_filter.dialog_filter.get()),
        recommended_dialog_filter.description);
  });
  recommended_dialog_filters_ = std::move(filters);
  promise.set_value(td_api::make_object<td_api::recommendedChatFilters>(std::move(chat_filters)));
}

}  // namespace td
