//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogActionBar.h"

namespace td {

unique_ptr<DialogActionBar> DialogActionBar::create(bool can_report_spam, bool can_add_contact, bool can_block_user,
                                                    bool can_share_phone_number, bool can_report_location,
                                                    bool can_unarchive, int32 distance, bool can_invite_members) {
  if (!can_report_spam && !can_add_contact && !can_block_user && !can_share_phone_number && !can_report_location &&
      !can_unarchive && distance < 0 && !can_invite_members) {
    return nullptr;
  }

  auto action_bar = make_unique<DialogActionBar>();
  action_bar->can_report_spam = can_report_spam;
  action_bar->can_add_contact = can_add_contact;
  action_bar->can_block_user = can_block_user;
  action_bar->can_share_phone_number = can_share_phone_number;
  action_bar->can_report_location = can_report_location;
  action_bar->can_unarchive = can_unarchive;
  action_bar->distance = distance >= 0 ? distance : -1;
  action_bar->can_invite_members = can_invite_members;
  return action_bar;
}

td_api::object_ptr<td_api::ChatActionBar> DialogActionBar::get_chat_action_bar_object(DialogType dialog_type,
                                                                                      bool hide_unarchive) const {
  if (can_report_location) {
    CHECK(dialog_type == DialogType::Channel);
    CHECK(!can_share_phone_number && !can_block_user && !can_add_contact && !can_report_spam && !can_invite_members);
    return td_api::make_object<td_api::chatActionBarReportUnrelatedLocation>();
  }
  if (can_invite_members) {
    CHECK(!can_share_phone_number && !can_block_user && !can_add_contact && !can_report_spam);
    return td_api::make_object<td_api::chatActionBarInviteMembers>();
  }
  if (can_share_phone_number) {
    CHECK(dialog_type == DialogType::User);
    CHECK(!can_block_user && !can_add_contact && !can_report_spam);
    return td_api::make_object<td_api::chatActionBarSharePhoneNumber>();
  }
  if (hide_unarchive) {
    if (can_add_contact) {
      return td_api::make_object<td_api::chatActionBarAddContact>();
    } else {
      return nullptr;
    }
  }
  if (can_block_user) {
    CHECK(dialog_type == DialogType::User);
    CHECK(can_report_spam && can_add_contact);
    return td_api::make_object<td_api::chatActionBarReportAddBlock>(can_unarchive, distance);
  }
  if (can_add_contact) {
    CHECK(dialog_type == DialogType::User);
    CHECK(!can_report_spam);
    return td_api::make_object<td_api::chatActionBarAddContact>();
  }
  if (can_report_spam) {
    return td_api::make_object<td_api::chatActionBarReportSpam>(can_unarchive);
  }
  return nullptr;
}

bool operator==(const unique_ptr<DialogActionBar> &lhs, const unique_ptr<DialogActionBar> &rhs) {
  if (lhs == nullptr) {
    return rhs == nullptr;
  }
  if (rhs == nullptr) {
    return false;
  }
  return lhs->can_report_spam == rhs->can_report_spam && lhs->can_add_contact == rhs->can_add_contact &&
         lhs->can_block_user == rhs->can_block_user && lhs->can_share_phone_number == rhs->can_share_phone_number &&
         lhs->can_report_location == rhs->can_report_location && lhs->can_unarchive == rhs->can_unarchive &&
         lhs->distance == rhs->distance && lhs->can_invite_members == rhs->can_invite_members;
}

}  // namespace td
