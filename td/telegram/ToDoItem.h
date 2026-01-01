//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2026
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/ToDoCompletion.h"

#include "td/utils/common.h"
#include "td/utils/Status.h"

namespace td {

class Dependencies;
class Td;
class UserManager;

class ToDoItem {
  int32 id_ = 0;
  FormattedText title_;

  friend bool operator==(const ToDoItem &lhs, const ToDoItem &rhs);

  static bool remove_unsupported_entities(FormattedText &text);

 public:
  ToDoItem() = default;

  ToDoItem(const UserManager *user_manager, telegram_api::object_ptr<telegram_api::todoItem> &&item);

  static Result<ToDoItem> get_to_do_item(const Td *td, DialogId dialog_id,
                                         td_api::object_ptr<td_api::inputChecklistTask> &&task);

  void validate(const char *source);

  const string &get_search_text() const {
    return title_.text;
  }

  td_api::object_ptr<td_api::checklistTask> get_checklist_task_object(Td *td,
                                                                      const vector<ToDoCompletion> &completions) const;

  telegram_api::object_ptr<telegram_api::todoItem> get_input_todo_item(const UserManager *user_manager) const;

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ToDoItem &lhs, const ToDoItem &rhs);
bool operator!=(const ToDoItem &lhs, const ToDoItem &rhs);

}  // namespace td
