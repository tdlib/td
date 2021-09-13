//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"

#include "td/actor/MultiPromise.h"

#include "td/utils/common.h"

#include <utility>

namespace td {

class Td;

// stores list of Dialog identifiers of a limited size
class RecentDialogList : public Actor {
 public:
  RecentDialogList(Td *td, const char *name, size_t max_size);

  void add_dialog(DialogId dialog_id);

  void remove_dialog(DialogId dialog_id);

  std::pair<int32, vector<DialogId>> get_dialogs(int32 limit, Promise<Unit> &&promise);

  void clear_dialogs();

 private:
  Td *td_;
  const char *name_;
  size_t max_size_;
  vector<DialogId> dialog_ids_;

  int32 dialogs_loaded_ = 0;  // 0 - not loaded, 1 - load request was sent, 2 - loaded
  MultiPromiseActor resolve_dialogs_multipromise_{"ResolveDialogsMultiPromiseActor"};

  bool load_dialogs(Promise<Unit> &&promise);

  bool do_add_dialog(DialogId dialog_id);

  string get_binlog_key() const;

  void update_dialogs();

  void save_dialogs() const;
};

}  // namespace td
