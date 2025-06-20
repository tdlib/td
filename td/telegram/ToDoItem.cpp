//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ToDoItem.h"

#include "td/telegram/Td.h"
#include "td/telegram/UserManager.h"

#include "td/utils/utf8.h"

namespace td {

ToDoItem::ToDoItem(const UserManager *user_manager, telegram_api::object_ptr<telegram_api::todoItem> &&item) {
  CHECK(item != nullptr);
  id_ = item->id_;
  title_ = get_formatted_text(user_manager, std::move(item->title_), true, true, "ToDoItem");
  validate("telegram_api::todoItem");
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

bool operator==(const ToDoItem &lhs, const ToDoItem &rhs) {
  return lhs.id_ == rhs.id_ && lhs.title_ == rhs.title_;
}

bool operator!=(const ToDoItem &lhs, const ToDoItem &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
