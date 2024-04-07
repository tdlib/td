//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2024
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

class Dependencies;
class Td;

class DialogActionBar {
  int32 distance_ = -1;  // distance to the peer
  int32 join_request_date_ = 0;
  string join_request_dialog_title_;
  UserId business_bot_user_id_;
  string business_bot_manage_url_;

  bool can_report_spam_ = false;
  bool can_add_contact_ = false;
  bool can_block_user_ = false;
  bool can_share_phone_number_ = false;
  bool can_report_location_ = false;
  bool can_unarchive_ = false;
  bool can_invite_members_ = false;
  bool is_join_request_broadcast_ = false;
  bool is_business_bot_paused_ = false;
  bool can_business_bot_reply_ = false;

  friend bool operator==(const unique_ptr<DialogActionBar> &lhs, const unique_ptr<DialogActionBar> &rhs);

 public:
  static unique_ptr<DialogActionBar> create(bool can_report_spam, bool can_add_contact, bool can_block_user,
                                            bool can_share_phone_number, bool can_report_location, bool can_unarchive,
                                            int32 distance, bool can_invite_members, string join_request_dialog_title,
                                            bool is_join_request_broadcast, int32 join_request_date,
                                            bool is_business_bot_paused, bool can_business_bot_reply,
                                            UserId business_bot_user_id, string business_bot_manage_url);

  bool is_empty() const;

  bool can_report_spam() const {
    return can_report_spam_;
  }

  bool can_unarchive() const {
    return can_unarchive_;
  }

  td_api::object_ptr<td_api::ChatActionBar> get_chat_action_bar_object(Td *td, DialogType dialog_type,
                                                                       bool hide_unarchive) const;

  void fix(Td *td, DialogId dialog_id, bool is_dialog_blocked, FolderId folder_id);

  bool on_dialog_unarchived();

  bool on_user_contact_added();

  bool on_user_deleted();

  bool on_outgoing_message();

  bool set_business_bot_is_paused(bool is_paused);

  void add_dependencies(Dependencies &dependencies) const;

  template <class StorerT>
  void store(StorerT &storer) const {
    bool has_distance = distance_ >= 0;
    bool has_join_request = !join_request_dialog_title_.empty();
    bool has_business_bot_user_id = business_bot_user_id_.is_valid();
    bool has_business_bot_manage_url = !business_bot_manage_url_.empty();
    BEGIN_STORE_FLAGS();
    STORE_FLAG(can_report_spam_);
    STORE_FLAG(can_add_contact_);
    STORE_FLAG(can_block_user_);
    STORE_FLAG(can_share_phone_number_);
    STORE_FLAG(can_report_location_);
    STORE_FLAG(can_unarchive_);
    STORE_FLAG(can_invite_members_);
    STORE_FLAG(has_distance);
    STORE_FLAG(is_join_request_broadcast_);
    STORE_FLAG(has_join_request);
    STORE_FLAG(is_business_bot_paused_);
    STORE_FLAG(can_business_bot_reply_);
    STORE_FLAG(has_business_bot_user_id);
    STORE_FLAG(has_business_bot_manage_url);
    END_STORE_FLAGS();
    if (has_distance) {
      td::store(distance_, storer);
    }
    if (has_join_request) {
      td::store(join_request_dialog_title_, storer);
      td::store(join_request_date_, storer);
    }
    if (has_business_bot_user_id) {
      td::store(business_bot_user_id_, storer);
    }
    if (has_business_bot_manage_url) {
      td::store(business_bot_manage_url_, storer);
    }
  }

  template <class ParserT>
  void parse(ParserT &parser) {
    bool has_distance;
    bool has_join_request;
    bool has_business_bot_user_id;
    bool has_business_bot_manage_url;
    BEGIN_PARSE_FLAGS();
    PARSE_FLAG(can_report_spam_);
    PARSE_FLAG(can_add_contact_);
    PARSE_FLAG(can_block_user_);
    PARSE_FLAG(can_share_phone_number_);
    PARSE_FLAG(can_report_location_);
    PARSE_FLAG(can_unarchive_);
    PARSE_FLAG(can_invite_members_);
    PARSE_FLAG(has_distance);
    PARSE_FLAG(is_join_request_broadcast_);
    PARSE_FLAG(has_join_request);
    PARSE_FLAG(is_business_bot_paused_);
    PARSE_FLAG(can_business_bot_reply_);
    PARSE_FLAG(has_business_bot_user_id);
    PARSE_FLAG(has_business_bot_manage_url);
    END_PARSE_FLAGS();
    if (has_distance) {
      td::parse(distance_, parser);
    }
    if (has_join_request) {
      td::parse(join_request_dialog_title_, parser);
      td::parse(join_request_date_, parser);
    }
    if (has_business_bot_user_id) {
      td::parse(business_bot_user_id_, parser);
    }
    if (has_business_bot_manage_url) {
      td::parse(business_bot_manage_url_, parser);
    }
  }
};

bool operator==(const unique_ptr<DialogActionBar> &lhs, const unique_ptr<DialogActionBar> &rhs);

}  // namespace td
