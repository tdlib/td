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
#include "td/telegram/ToDoItem.h"

namespace td {

class UserManager;

class ToDoList {
  FormattedText title_;
  vector<ToDoItem> items_;
  bool others_can_append_ = false;
  bool others_can_complete_ = false;

  void validate(const char *source);

  friend bool operator==(const ToDoList &lhs, const ToDoList &rhs);

 public:
  ToDoList() = default;

  ToDoList(const UserManager *user_manager, telegram_api::object_ptr<telegram_api::todoList> &&list);

  td_api::object_ptr<td_api::toDoList> get_to_do_list_object(Td *td, const vector<ToDoCompletion> &completions) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ToDoList &lhs, const ToDoList &rhs);
bool operator!=(const ToDoList &lhs, const ToDoList &rhs);

}  // namespace td
