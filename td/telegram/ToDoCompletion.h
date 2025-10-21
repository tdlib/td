//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2025
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/telegram_api.h"

#include "td/utils/common.h"

namespace td {

class Dependencies;

struct ToDoCompletion {
  int32 id_ = 0;
  DialogId completed_by_dialog_id_;
  int32 date_ = 0;

  ToDoCompletion() = default;

  explicit ToDoCompletion(telegram_api::object_ptr<telegram_api::todoCompletion> &&completion);

  bool is_valid() const {
    return completed_by_dialog_id_.is_valid() && date_ > 0;
  }

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const;

  template <class ParserT>
  void parse(ParserT &parser);
};

bool operator==(const ToDoCompletion &lhs, const ToDoCompletion &rhs);
bool operator!=(const ToDoCompletion &lhs, const ToDoCompletion &rhs);

}  // namespace td
