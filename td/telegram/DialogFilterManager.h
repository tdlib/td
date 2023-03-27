//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2023
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogFilterId.h"
#include "td/telegram/DialogId.h"
#include "td/telegram/InputDialogId.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"

#include "td/actor/actor.h"

#include "td/utils/common.h"
#include "td/utils/Promise.h"
#include "td/utils/Status.h"

namespace td {

class DialogFilter;
class Td;

class DialogFilterManager final : public Actor {
 public:
  DialogFilterManager(Td *td, ActorShared<> parent);

  td_api::object_ptr<td_api::chatFilter> get_chat_filter_object(DialogFilterId dialog_filter_id);

  td_api::object_ptr<td_api::chatFilter> get_chat_filter_object(const DialogFilter *dialog_filter);

  bool is_recommended_dialog_filter(const DialogFilter *dialog_filter);

  void get_recommended_dialog_filters(Promise<td_api::object_ptr<td_api::recommendedChatFilters>> &&promise);

  void load_dialog_filter(DialogFilterId dialog_filter_id, bool force, Promise<Unit> &&promise);

  void load_dialog_filter_dialogs(DialogFilterId dialog_filter_id, vector<InputDialogId> &&input_dialog_ids,
                                  Promise<Unit> &&promise);

 private:
  void tear_down() final;

  struct RecommendedDialogFilter {
    unique_ptr<DialogFilter> dialog_filter;
    string description;
  };

  void on_get_recommended_dialog_filters(
      Result<vector<telegram_api::object_ptr<telegram_api::dialogFilterSuggested>>> result,
      Promise<td_api::object_ptr<td_api::recommendedChatFilters>> &&promise);

  void on_load_recommended_dialog_filters(Result<Unit> &&result, vector<RecommendedDialogFilter> &&filters,
                                          Promise<td_api::object_ptr<td_api::recommendedChatFilters>> &&promise);

  void load_dialog_filter(const DialogFilter *dialog_filter, bool force, Promise<Unit> &&promise);

  void on_load_dialog_filter_dialogs(DialogFilterId dialog_filter_id, vector<DialogId> &&dialog_ids,
                                     Promise<Unit> &&promise);

  void delete_dialogs_from_filter(const DialogFilter *dialog_filter, vector<DialogId> &&dialog_ids, const char *source);

  vector<RecommendedDialogFilter> recommended_dialog_filters_;

  Td *td_;
  ActorShared<> parent_;
};

}  // namespace td
