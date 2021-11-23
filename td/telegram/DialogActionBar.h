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
#include "td/utils/tl_helpers.h"

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

  bool is_empty() const;

  td_api::object_ptr<td_api::ChatActionBar> get_chat_action_bar_object(DialogType dialog_type,
                                                                       bool hide_unarchive) const;

  void fix(Td *td, DialogId dialog_id, bool is_dialog_blocked, FolderId folder_id);

  bool on_dialog_unarchived();

  bool on_user_contact_added();

  bool on_user_deleted();

  bool on_outgoing_message();

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_distance = distance >= 0;
    BEGIN_STORE_FLAGS();
    STORE_FLAG(can_report_spam);
    STORE_FLAG(can_add_contact);
    STORE_FLAG(can_block_user);
    STORE_FLAG(can_share_phone_number);
    STORE_FLAG(can_report_location);
    STORE_FLAG(can_unarchive);
    STORE_FLAG(can_invite_members);
    STORE_FLAG(has_distance);
    END_STORE_FLAGS();
    if (has_distance) {
      td::store(distance, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_distance;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(can_report_spam);
    PARSE_FLAG(can_add_contact);
    PARSE_FLAG(can_block_user);
    PARSE_FLAG(can_share_phone_number);
    PARSE_FLAG(can_report_location);
    PARSE_FLAG(can_unarchive);
    PARSE_FLAG(can_invite_members);
    PARSE_FLAG(has_distance);
    END_PARSE_FLAGS();
    if (has_distance) {
      td::parse(distance, parser);
    }
  }
};

bool operator==(const unique_ptr<DialogActionBar> &lhs, const unique_ptr<DialogActionBar> &rhs);

}  // namespace td
