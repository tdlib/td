//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/MessageEntity.h"
#include "td/telegram/td_api.h"
#include "td/telegram/telegram_api.h"
#include "td/telegram/ToDoCompletion.h"

#include "td/utils/common.h"

namespace td {

class Td;
class UserManager;

class ToDoItem {
  int32 id_ = 0;
  FormattedText title_;

  friend bool operator==(const ToDoItem &lhs, const ToDoItem &rhs);

 public:
  ToDoItem() = default;

  ToDoItem(const UserManager *user_manager, telegram_api::object_ptr<telegram_api::todoItem> &&item);

  void validate(const char *source);

  td_api::object_ptr<td_api::toDoListTask> get_to_do_list_task_object(Td *td,
                                                                      const vector<ToDoCompletion> &completions) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ToDoItem &lhs, const ToDoItem &rhs);
bool operator!=(const ToDoItem &lhs, const ToDoItem &rhs);

}  // namespace td
