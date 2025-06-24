//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ToDoList.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"

#include "td/utils/algorithm.h"
#include "td/utils/utf8.h"

namespace td {

ToDoList::ToDoList(const UserManager *user_manager, telegram_api::object_ptr<telegram_api::todoList> &&list) {
  CHECK(list != nullptr);
  others_can_append_ = list->others_can_append_;
  others_can_complete_ = list->others_can_complete_;
  title_ = get_formatted_text(user_manager, std::move(list->title_), true, true, "ToDoList");
  for (auto &item : list->list_) {
    items_.push_back(ToDoItem(user_manager, std::move(item)));
  }
  validate("telegram_api::todoList");
}

Result<ToDoList> ToDoList::get_to_do_list(const Td *td, DialogId dialog_id,
                                          td_api::object_ptr<td_api::inputToDoList> &&list) {
  if (list == nullptr) {
    return Status::Error(400, "To do list must be non-empty");
  }
  TRY_RESULT(title, get_formatted_text(td, dialog_id, std::move(list->title_), td->auth_manager_->is_bot(), false, true,
                                       false));
  auto max_length = td->option_manager_->get_option_integer("to_do_list_title_length_max", 0);
  if (static_cast<int64>(utf8_length(title.text)) > max_length) {
    return Status::Error(400, PSLICE() << "To do list title length must not exceed " << max_length);
  }
  keep_only_custom_emoji(title);

  ToDoList result;
  result.title_ = std::move(title);
  for (auto &task : list->tasks_) {
    TRY_RESULT(item, ToDoItem::get_to_do_item(td, dialog_id, std::move(task)));
    result.items_.push_back(std::move(item));
  }
  result.others_can_append_ = list->others_can_add_tasks_;
  result.others_can_complete_ = list->others_can_mark_tasks_as_done_;
  return result;
}

telegram_api::object_ptr<telegram_api::todoList> ToDoList::get_input_todo_list(const UserManager *user_manager) const {
  auto items = transform(items_, [user_manager](const auto &item) { return item.get_input_todo_item(user_manager); });
  return telegram_api::make_object<telegram_api::todoList>(
      0, others_can_append_, others_can_complete_,
      get_input_text_with_entities(user_manager, title_, "get_input_todo_list"), std::move(items));
}

void ToDoList::validate(const char *source) {
  if (keep_only_custom_emoji(title_)) {
    LOG(ERROR) << "Receive unexpected to do list title entities from " << source;
  }
  for (auto &item : items_) {
    item.validate(source);
  }
}

td_api::object_ptr<td_api::toDoList> ToDoList::get_to_do_list_object(Td *td, const vector<ToDoCompletion> &completions,
                                                                     MessageId message_id, bool is_outgoing) const {
  auto tasks = transform(
      items_, [td, &completions](const auto &item) { return item.get_to_do_list_task_object(td, completions); });
  bool can_complete = !td->auth_manager_->is_bot() && message_id.is_server() && (is_outgoing || others_can_complete_);
  bool can_add_tasks = message_id.is_server() && (is_outgoing || others_can_append_);
  return td_api::make_object<td_api::toDoList>(get_formatted_text_object(td->user_manager_.get(), title_, true, -1),
                                               std::move(tasks), others_can_append_, can_add_tasks,
                                               others_can_complete_, can_complete);
}

void ToDoList::add_dependencies(Dependencies &dependencies) const {
  add_formatted_text_dependencies(dependencies, &title_);
  for (auto &item : items_) {
    item.add_dependencies(dependencies);
  }
}

bool operator==(const ToDoList &lhs, const ToDoList &rhs) {
  return lhs.title_ == rhs.title_ && lhs.items_ == rhs.items_ && lhs.others_can_append_ == rhs.others_can_append_ &&
         lhs.others_can_complete_ == rhs.others_can_complete_;
}

bool operator!=(const ToDoList &lhs, const ToDoList &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
