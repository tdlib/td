//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ToDoList.h"

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

void ToDoList::validate(const char *source) {
  if (keep_only_custom_emoji(title_)) {
    LOG(ERROR) << "Receive unexpected to do list title entities from " << source;
  }
  for (auto &item : items_) {
    item.validate(source);
  }
}

td_api::object_ptr<td_api::toDoList> ToDoList::get_to_do_list_object(Td *td,
                                                                     const vector<ToDoCompletion> &completions) const {
  auto tasks = transform(
      items_, [td, &completions](const auto &item) { return item.get_to_do_list_task_object(td, completions); });
  return td_api::make_object<td_api::toDoList>(get_formatted_text_object(td->user_manager_.get(), title_, true, -1),
                                               std::move(tasks), others_can_append_, others_can_complete_);
}

bool operator==(const ToDoList &lhs, const ToDoList &rhs) {
  return lhs.title_ == rhs.title_ && lhs.items_ == rhs.items_ && lhs.others_can_append_ == rhs.others_can_append_ &&
         lhs.others_can_complete_ == rhs.others_can_complete_;
}

bool operator!=(const ToDoList &lhs, const ToDoList &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
