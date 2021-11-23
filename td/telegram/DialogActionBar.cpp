//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2021
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/telegram/DialogActionBar.h"

#include "td/telegram/ContactsManager.h"
#include "td/telegram/Td.h"

namespace td {

unique_ptr<DialogActionBar> DialogActionBar::create(bool can_report_spam, bool can_add_contact, bool can_block_user,
                                                    bool can_share_phone_number, bool can_report_location,
                                                    bool can_unarchive, int32 distance, bool can_invite_members) {
  if (!can_report_spam && !can_add_contact && !can_block_user && !can_share_phone_number && !can_report_location &&
      !can_invite_members) {
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

void DialogActionBar::fix(Td *td, DialogId dialog_id, bool is_dialog_blocked, FolderId folder_id) {
  auto dialog_type = dialog_id.get_type();
  if (distance >= 0 && dialog_type != DialogType::User) {
    LOG(ERROR) << "Receive distance " << distance << " to " << dialog_id;
    distance = -1;
  }

  if (can_report_location) {
    if (dialog_type != DialogType::Channel) {
      LOG(ERROR) << "Receive can_report_location in " << dialog_id;
      can_report_location = false;
    } else if (can_report_spam || can_add_contact || can_block_user || can_share_phone_number || can_unarchive ||
               can_invite_members) {
      LOG(ERROR) << "Receive action bar " << can_report_spam << "/" << can_add_contact << "/" << can_block_user << "/"
                 << can_share_phone_number << "/" << can_report_location << "/" << can_unarchive << "/"
                 << can_invite_members;
      can_report_spam = false;
      can_add_contact = false;
      can_block_user = false;
      can_share_phone_number = false;
      can_unarchive = false;
      can_invite_members = false;
      CHECK(distance == -1);
    }
  }
  if (can_invite_members) {
    if (dialog_type != DialogType::Chat &&
        (dialog_type != DialogType::Channel || td->contacts_manager_->get_channel_type(dialog_id.get_channel_id()) ==
                                                   ContactsManager::ChannelType::Broadcast)) {
      LOG(ERROR) << "Receive can_invite_members in " << dialog_id;
      can_invite_members = false;
    } else if (can_report_spam || can_add_contact || can_block_user || can_share_phone_number || can_unarchive) {
      LOG(ERROR) << "Receive action bar " << can_report_spam << "/" << can_add_contact << "/" << can_block_user << "/"
                 << can_share_phone_number << "/" << can_unarchive << "/" << can_invite_members;
      can_report_spam = false;
      can_add_contact = false;
      can_block_user = false;
      can_share_phone_number = false;
      can_unarchive = false;
      CHECK(distance == -1);
    }
  }
  if (dialog_type == DialogType::User) {
    auto user_id = dialog_id.get_user_id();
    bool is_me = user_id == td->contacts_manager_->get_my_id();
    bool is_deleted = td->contacts_manager_->is_user_deleted(user_id);
    bool is_contact = td->contacts_manager_->is_user_contact(user_id);
    if (is_me || is_dialog_blocked) {
      can_report_spam = false;
      can_unarchive = false;
    }
    if (is_me || is_dialog_blocked || is_deleted) {
      can_share_phone_number = false;
    }
    if (is_me || is_dialog_blocked || is_deleted || is_contact) {
      can_block_user = false;
      can_add_contact = false;
    }
  }
  if (folder_id != FolderId::archive()) {
    can_unarchive = false;
  }
  if (can_share_phone_number) {
    CHECK(!can_report_location);
    CHECK(!can_invite_members);
    if (dialog_type != DialogType::User) {
      LOG(ERROR) << "Receive can_share_phone_number in " << dialog_id;
      can_share_phone_number = false;
    } else if (can_report_spam || can_add_contact || can_block_user || can_unarchive || distance >= 0) {
      LOG(ERROR) << "Receive action bar " << can_report_spam << "/" << can_add_contact << "/" << can_block_user << "/"
                 << can_share_phone_number << "/" << can_unarchive << "/" << distance;
      can_report_spam = false;
      can_add_contact = false;
      can_block_user = false;
      can_unarchive = false;
    }
  }
  if (can_block_user) {
    CHECK(!can_report_location);
    CHECK(!can_invite_members);
    CHECK(!can_share_phone_number);
    if (dialog_type != DialogType::User) {
      LOG(ERROR) << "Receive can_block_user in " << dialog_id;
      can_block_user = false;
    } else if (!can_report_spam || !can_add_contact) {
      LOG(ERROR) << "Receive action bar " << can_report_spam << "/" << can_add_contact << "/" << can_block_user;
      can_report_spam = true;
      can_add_contact = true;
    }
  }
  if (can_add_contact) {
    CHECK(!can_report_location);
    CHECK(!can_invite_members);
    CHECK(!can_share_phone_number);
    if (dialog_type != DialogType::User) {
      LOG(ERROR) << "Receive can_add_contact in " << dialog_id;
      can_add_contact = false;
    } else if (can_report_spam != can_block_user) {
      LOG(ERROR) << "Receive action bar " << can_report_spam << "/" << can_add_contact << "/" << can_block_user;
      can_report_spam = false;
      can_block_user = false;
      can_unarchive = false;
    }
  }
  if (!can_block_user) {
    distance = -1;
  }
  if (!can_report_spam) {
    can_unarchive = false;
  }
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

bool DialogActionBar::on_dialog_unarchived() {
  if (!can_unarchive) {
    return false;
  }

  can_unarchive = false;
  can_report_spam = false;
  can_block_user = false;
  // keep can_add_contact
  return true;
}

bool DialogActionBar::on_user_contact_added() {
  if (!can_block_user && !can_add_contact) {
    return false;
  }

  can_block_user = false;
  can_add_contact = false;
  // keep can_unarchive
  distance = -1;
  return true;
}

bool DialogActionBar::on_user_deleted() {
  if (!can_share_phone_number && !can_block_user && !can_add_contact && distance < 0) {
    return false;
  }

  can_share_phone_number = false;
  can_block_user = false;
  can_add_contact = false;
  distance = -1;
  return true;
}

bool DialogActionBar::on_outgoing_message() {
  if (distance < 0) {
    return false;
  }

  distance = -1;
  return true;
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
