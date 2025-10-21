//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/ToDoCompletion.h"

#include "td/telegram/Dependencies.h"

namespace td {

ToDoCompletion::ToDoCompletion(telegram_api::object_ptr<telegram_api::todoCompletion> &&completion) {
  CHECK(completion != nullptr);
  id_ = completion->id_;
  completed_by_dialog_id_ = DialogId(completion->completed_by_);
  date_ = completion->date_;
}

void ToDoCompletion::add_dependencies(Dependencies &dependencies) const {
  dependencies.add_message_sender_dependencies(completed_by_dialog_id_);
}

bool operator==(const ToDoCompletion &lhs, const ToDoCompletion &rhs) {
  return lhs.id_ == rhs.id_ && lhs.completed_by_dialog_id_ == rhs.completed_by_dialog_id_ && lhs.date_ == rhs.date_;
}

bool operator!=(const ToDoCompletion &lhs, const ToDoCompletion &rhs) {
  return !(lhs == rhs);
}

}  // namespace td
