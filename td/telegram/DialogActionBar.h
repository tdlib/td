//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once

#include "td/telegram/DialogId.h"
#include "td/telegram/FolderId.h"
#include "td/telegram/td_api.h"

#include "td/utils/common.h"

namespace td {

class Td;

struct DialogActionBar {
  int32 distance = -1;  // distance to the peer

  bool can_report_spam = false;
  bool can_add_contact = false;
  bool can_block_user = false;
  bool can_share_phone_number = false;
  bool can_report_location = false;
  bool can_unarchive = false;
  bool can_invite_members = false;

  static unique_ptr<DialogActionBar> create(bool can_report_spam, bool can_add_contact, bool can_block_user,
                                            bool can_share_phone_number, bool can_report_location, bool can_unarchive,
                                            int32 distance, bool can_invite_members);

  td_api::object_ptr<td_api::ChatActionBar> get_chat_action_bar_object(DialogType dialog_type,
                                                                       bool hide_unarchive) const;

  void fix(Td *td, DialogId dialog_id, bool is_dialog_blocked, FolderId folder_id);
};

bool operator==(const unique_ptr<DialogActionBar> &lhs, const unique_ptr<DialogActionBar> &rhs);

}  // namespace td
