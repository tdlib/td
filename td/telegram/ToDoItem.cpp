//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ToDoItem.h"

#include "td/telegram/AuthManager.h"
#include "td/telegram/Dependencies.h"
#include "td/telegram/OptionManager.h"
#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/SliceBuilder.h"
#include "td/utils/utf8.h"

namespace td {

ToDoItem::ToDoItem(const UserManager *user_manager, telegram_api::object_ptr<telegram_api::todoItem> &&item) {
  CHECK(item != nullptr);
  id_ = item->id_;
  title_ = get_formatted_text(user_manager, std::move(item->title_), true, true, "ToDoItem");
  validate("telegram_api::todoItem");
}

Result<ToDoItem> ToDoItem::get_to_do_item(const Td *td, DialogId dialog_id,
                                          td_api::object_ptr<td_api::inputToDoListTask> &&task) {
  if (task == nullptr) {
    return Status::Error(400, "To do list task must be non-empty");
  }
  TRY_RESULT(title, get_formatted_text(td, dialog_id, std::move(task->text_), td->auth_manager_->is_bot(), false, true,
                                       false));
  auto max_length = td->option_manager_->get_option_integer("to_do_list_task_text_length_max", 12);
  if (static_cast<int64>(utf8_length(title.text)) > max_length) {
    return Status::Error(400, PSLICE() << "To do list task text length must not exceed " << max_length);
  }
  if (task->id_ <= 0) {
    return Status::Error(400, "To do list task identifier must be positive");
  }
  keep_only_custom_emoji(title);
  ToDoItem result;
  result.id_ = task->id_;
  result.title_ = std::move(title);
  return result;
}

telegram_api::object_ptr<telegram_api::todoItem> ToDoItem::get_input_todo_item(const UserManager *user_manager) const {
  return telegram_api::make_object<telegram_api::todoItem>(
      id_, get_input_text_with_entities(user_manager, title_, "get_input_todo_item"));
}

void ToDoItem::validate(const char *source) {
  if (keep_only_custom_emoji(title_)) {
    LOG(ERROR) << "Receive unexpected to do list task entities from " << source;
  }
  if (!check_utf8(title_.text)) {
    LOG(ERROR) << "Receive invalid to do list task from " << source;
    title_ = {};
  }
}

td_api::object_ptr<td_api::toDoListTask> ToDoItem::get_to_do_list_task_object(
    Td *td, const vector<ToDoCompletion> &completions) const {
  auto result = td_api::make_object<td_api::toDoListTask>(
      id_, get_formatted_text_object(td->user_manager_.get(), title_, true, -1), 0, 0);
  for (auto &completion : completions) {
    if (completion.id_ == id_) {
      result->completed_by_user_id_ =
          td->user_manager_->get_user_id_object(completion.completed_by_user_id_, "toDoListTask");
      result->completion_date_ = completion.date_;
    }
  }
  return result;
}

void ToDoItem::add_dependencies(Dependencies &dependencies) const {
  add_formatted_text_dependencies(dependencies, &title_);
}

bool operator==(const ToDoItem &lhs, const ToDoItem &rhs) {
  return lhs.id_ == rhs.id_ && lhs.title_ == rhs.title_;
}

bool operator!=(const ToDoItem &lhs, const ToDoItem &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
